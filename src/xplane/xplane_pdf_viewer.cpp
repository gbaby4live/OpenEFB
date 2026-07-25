#include "xplane_pdf_viewer.hpp"

#include <XPLMGraphics.h>

#if IBM
#include <Windows.h>
#include <GL/gl.h>
#include <wincodec.h>
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Data.Pdf.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>
#elif APL
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace openefb::xplane {
namespace {

struct RenderedPage {
    std::vector<std::uint8_t> pixels;
    int width{};
    int height{};
    int page{};
    int page_count{};
    std::string status;
};

#if IBM
RenderedPage decode_png(const std::vector<std::uint8_t>& png, int page, int page_count) {
    IWICImagingFactory* factory{};
    IWICStream* stream{};
    IWICBitmapDecoder* decoder{};
    IWICBitmapFrameDecode* frame{};
    IWICFormatConverter* converter{};
    RenderedPage rendered;
    rendered.page = page;
    rendered.page_count = page_count;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromMemory(const_cast<BYTE*>(png.data()),
                                               static_cast<DWORD>(png.size()))) &&
        SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad,
                                                   &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) &&
        SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
                                        WICBitmapDitherTypeNone, nullptr, 0.0,
                                        WICBitmapPaletteTypeCustom))) {
        UINT width{};
        UINT height{};
        if (SUCCEEDED(converter->GetSize(&width, &height)) && width > 0 && height > 0 &&
            width <= 4096 && height <= 4096) {
            rendered.width = static_cast<int>(width);
            rendered.height = static_cast<int>(height);
            rendered.pixels.resize(static_cast<std::size_t>(width) * height * 4);
            if (FAILED(converter->CopyPixels(nullptr, width * 4,
                                             static_cast<UINT>(rendered.pixels.size()),
                                             rendered.pixels.data()))) rendered.pixels.clear();
            else for (std::size_t index = 3; index < rendered.pixels.size(); index += 4)
                rendered.pixels[index] = 255;
        }
    }
    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (stream) stream->Release();
    if (factory) factory->Release();
    rendered.status = rendered.pixels.empty() ? "PDF page image could not be decoded" : "PDF page ready";
    return rendered;
}

RenderedPage render_pdf_page(const std::filesystem::path& path, int requested_page) {
    using namespace winrt::Windows::Data::Pdf;
    using namespace winrt::Windows::Storage;
    using namespace winrt::Windows::Storage::Streams;
    try {
        const auto file = StorageFile::GetFileFromPathAsync(path.wstring()).get();
        const auto document = PdfDocument::LoadFromFileAsync(file).get();
        const int count = static_cast<int>(document.PageCount());
        if (count <= 0) return {{}, 0, 0, 0, 0, "PDF has no pages"};
        const int page_index = std::clamp(requested_page, 0, count - 1);
        const auto page = document.GetPage(static_cast<std::uint32_t>(page_index));
        const auto size = page.Size();
        const std::uint32_t width = 1400;
        const std::uint32_t height = std::max<std::uint32_t>(1,
            static_cast<std::uint32_t>(width * size.Height / std::max(1.0F, size.Width)));
        InMemoryRandomAccessStream output;
        PdfPageRenderOptions options;
        options.DestinationWidth(width);
        options.DestinationHeight(height);
        page.RenderToStreamAsync(output, options).get();
        const auto byte_count = output.Size();
        if (byte_count == 0 || byte_count > 64ULL * 1024ULL * 1024ULL)
            return {{}, 0, 0, page_index, count, "PDF page render returned no image"};
        output.Seek(0);
        DataReader reader(output.GetInputStreamAt(0));
        reader.LoadAsync(static_cast<std::uint32_t>(byte_count)).get();
        std::vector<std::uint8_t> png(static_cast<std::size_t>(byte_count));
        reader.ReadBytes(png);
        page.Close();
        return decode_png(png, page_index, count);
    } catch (const winrt::hresult_error& error) {
        return {{}, 0, 0, requested_page, 0,
                "PDF could not open: " + winrt::to_string(error.message())};
    } catch (...) {
        return {{}, 0, 0, requested_page, 0, "PDF could not open"};
    }
}
#endif

} // namespace

class XPlanePdfViewer::Implementation final {
public:
    Implementation() : worker_([this] { work(); }) {}
    ~Implementation() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        condition_.notify_one();
        if (worker_.joinable()) worker_.join();
        if (texture_) glDeleteTextures(1, &texture_);
    }

    void open(std::filesystem::path path) {
        std::lock_guard lock(mutex_);
        path_ = std::move(path);
        requested_page_ = 0;
        visible_ = true;
        status_ = "Loading PDF page...";
        request_pending_ = true;
        condition_.notify_one();
    }

    void close() { visible_ = false; }
    bool visible() const noexcept { return visible_; }
    int page_number() const noexcept { return current_page_ + 1; }
    int page_count() const noexcept { return page_count_; }

    std::string title() const {
        std::lock_guard lock(mutex_);
        return path_.filename().string();
    }

    std::string status() const {
        std::lock_guard lock(mutex_);
        return status_;
    }

    void move(int delta) {
        std::lock_guard lock(mutex_);
        if (!visible_ || page_count_ <= 0) return;
        const int next = std::clamp(current_page_ + delta, 0, page_count_ - 1);
        if (next == current_page_) return;
        requested_page_ = next;
        status_ = "Loading PDF page...";
        request_pending_ = true;
        condition_.notify_one();
    }

    void draw(int left, int top, int right, int bottom) {
        upload_ready();
        if (!texture_ || width_ <= 0 || height_ <= 0) return;
        const double available_width = std::max(1, right - left);
        const double available_height = std::max(1, top - bottom);
        const double scale = std::min(available_width / width_, available_height / height_);
        const double draw_width = width_ * scale;
        const double draw_height = height_ * scale;
        const double draw_left = left + (available_width - draw_width) * 0.5;
        const double draw_right = draw_left + draw_width;
        const double draw_bottom = bottom + (available_height - draw_height) * 0.5;
        const double draw_top = draw_bottom + draw_height;
        XPLMSetGraphicsState(0, 1, 0, 0, 0, 0, 0);
        XPLMBindTexture2d(static_cast<int>(texture_), 0);
        glColor4f(1.0F, 1.0F, 1.0F, 1.0F);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0F, 1.0F); glVertex2d(draw_left, draw_bottom);
        glTexCoord2f(1.0F, 1.0F); glVertex2d(draw_right, draw_bottom);
        glTexCoord2f(1.0F, 0.0F); glVertex2d(draw_right, draw_top);
        glTexCoord2f(0.0F, 0.0F); glVertex2d(draw_left, draw_top);
        glEnd();
        XPLMSetGraphicsState(0, 0, 0, 0, 1, 0, 0);
    }

private:
    void upload_ready() {
        RenderedPage ready;
        {
            std::lock_guard lock(mutex_);
            if (!ready_) return;
            ready = std::move(*ready_);
            ready_.reset();
            status_ = ready.status;
            current_page_ = ready.page;
            page_count_ = ready.page_count;
        }
        if (ready.pixels.empty()) return;
        if (texture_) glDeleteTextures(1, &texture_);
        int texture_number{};
        XPLMGenerateTextureNumbers(&texture_number, 1);
        texture_ = static_cast<GLuint>(texture_number);
        XPLMSetGraphicsState(0, 1, 0, 0, 0, 0, 0);
        XPLMBindTexture2d(texture_number, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ready.width, ready.height, 0,
                     GL_BGRA, GL_UNSIGNED_BYTE, ready.pixels.data());
        width_ = ready.width;
        height_ = ready.height;
    }

    void work() {
#if IBM
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
#endif
        for (;;) {
            std::filesystem::path path;
            int page{};
            {
                std::unique_lock lock(mutex_);
                condition_.wait(lock, [this] { return stopping_ || request_pending_; });
                if (stopping_) return;
                path = path_;
                page = requested_page_;
                request_pending_ = false;
            }
#if IBM
            auto rendered = render_pdf_page(path, page);
#else
            RenderedPage rendered{{}, 0, 0, page, 0,
                                  "In-app PDF viewing is currently available on Windows"};
#endif
            std::lock_guard lock(mutex_);
            ready_ = std::move(rendered);
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::thread worker_;
    std::filesystem::path path_;
    std::optional<RenderedPage> ready_;
    GLuint texture_{};
    int width_{};
    int height_{};
    int requested_page_{};
    int current_page_{};
    int page_count_{};
    std::string status_;
    bool visible_{false};
    bool request_pending_{false};
    bool stopping_{false};
};

XPlanePdfViewer::XPlanePdfViewer() : implementation_(std::make_unique<Implementation>()) {}
XPlanePdfViewer::~XPlanePdfViewer() = default;
void XPlanePdfViewer::open(std::filesystem::path path) { implementation_->open(std::move(path)); }
void XPlanePdfViewer::close() { implementation_->close(); }
void XPlanePdfViewer::previous_page() { implementation_->move(-1); }
void XPlanePdfViewer::next_page() { implementation_->move(1); }
void XPlanePdfViewer::draw(int left, int top, int right, int bottom) {
    implementation_->draw(left, top, right, bottom);
}
bool XPlanePdfViewer::visible() const noexcept { return implementation_->visible(); }
int XPlanePdfViewer::page_number() const noexcept { return implementation_->page_number(); }
int XPlanePdfViewer::page_count() const noexcept { return implementation_->page_count(); }
std::string XPlanePdfViewer::title() const { return implementation_->title(); }
std::string XPlanePdfViewer::status() const { return implementation_->status(); }

} // namespace openefb::xplane
