#include "xplane_map_tiles.hpp"

#include <XPLMGraphics.h>

#if IBM
#include <Windows.h>
#include <GL/gl.h>
#include <Shlwapi.h>
#include <WinHttp.h>
#include <wincodec.h>
#elif APL
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <compare>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace openefb::xplane {

namespace {

constexpr int tile_size = 256;
constexpr double pi = 3.14159265358979323846;

struct TileKey {
    MapStyle style{};
    int zoom{};
    int x{};
    int y{};

    auto operator<=>(const TileKey&) const = default;
};

struct DecodedTile {
    TileKey key;
    std::vector<std::uint8_t> pixels;
};

double clamp_latitude(double latitude) {
    return std::clamp(latitude, -85.05112878, 85.05112878);
}

double world_x(double longitude, int zoom) {
    return (longitude + 180.0) / 360.0 * static_cast<double>(tile_size * (1 << zoom));
}

double world_y(double latitude, int zoom) {
    const double radians = clamp_latitude(latitude) * pi / 180.0;
    return (1.0 - std::asinh(std::tan(radians)) / pi) * 0.5 *
           static_cast<double>(tile_size * (1 << zoom));
}

int tile_zoom(const MapTileViewport& viewport) {
    const double radius_pixels = std::max(40.0, std::min(viewport.right - viewport.left,
                                                         viewport.top - viewport.bottom) * 0.46);
    const double pixels_per_nm = radius_pixels / std::max(1.0, viewport.range_nm);
    const double latitude_scale = std::max(0.05, std::cos(clamp_latitude(viewport.latitude_degrees) * pi / 180.0));
    const double zoom = std::log2(21600.0 * latitude_scale * pixels_per_nm / tile_size);
    return std::clamp(static_cast<int>(std::lround(zoom)), 2, 16);
}

std::filesystem::path cache_path(const std::filesystem::path& root, const TileKey& key) {
    return root / (key.style == MapStyle::street ? "street" : "topo") /
           std::to_string(key.zoom) / std::to_string(key.x) /
           (std::to_string(key.y) + ".png");
}

bool fresh_cache_file(const std::filesystem::path& path) {
    try {
        if (!std::filesystem::is_regular_file(path)) {
            return false;
        }
        return std::filesystem::file_time_type::clock::now() -
                   std::filesystem::last_write_time(path) < std::chrono::hours(24 * 7);
    } catch (...) {
        return false;
    }
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        return {};
    }
    const auto size = input.tellg();
    if (size <= 0 || size > 4 * 1024 * 1024) {
        return {};
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(data.data()), size);
    return input ? data : std::vector<std::uint8_t>{};
}

void save_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& data) {
    try {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    } catch (...) {
    }
}

#if IBM
std::vector<std::uint8_t> download_tile(const TileKey& key) {
    const wchar_t* host = key.style == MapStyle::street
                              ? L"tile.openstreetmap.org"
                              : L"a.tile.opentopomap.org";
    const std::wstring path = L"/" + std::to_wstring(key.zoom) + L"/" +
                              std::to_wstring(key.x) + L"/" + std::to_wstring(key.y) + L".png";
    HINTERNET session = WinHttpOpen(L"OpenEFB/0.8 (+https://github.com/Gbaby4live/OpenEFB)",
                                    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        return {};
    }
    WinHttpSetTimeouts(session, 3000, 3000, 5000, 5000);
    HINTERNET connection = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(
        connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    std::vector<std::uint8_t> data;
    if (request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                      WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD status{};
        DWORD status_size = sizeof(status);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                                WINHTTP_NO_HEADER_INDEX) && status == 200) {
            for (;;) {
                DWORD available{};
                if (!WinHttpQueryDataAvailable(request, &available) || available == 0 ||
                    data.size() + available > 4 * 1024 * 1024) {
                    break;
                }
                const std::size_t offset = data.size();
                data.resize(offset + available);
                DWORD read{};
                if (!WinHttpReadData(request, data.data() + offset, available, &read)) {
                    data.clear();
                    break;
                }
                data.resize(offset + read);
            }
        }
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return data;
}

std::vector<std::uint8_t> decode_png(const std::vector<std::uint8_t>& png) {
    if (png.empty()) {
        return {};
    }
    IWICImagingFactory* factory{};
    IWICStream* stream{};
    IWICBitmapDecoder* decoder{};
    IWICBitmapFrameDecode* frame{};
    IWICFormatConverter* converter{};
    std::vector<std::uint8_t> pixels;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromMemory(const_cast<BYTE*>(png.data()),
                                               static_cast<DWORD>(png.size()))) &&
        SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad,
                                                   &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) &&
        SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
                                        WICBitmapDitherTypeNone, nullptr, 0.0,
                                        WICBitmapPaletteTypeCustom))) {
        UINT width{};
        UINT height{};
        if (SUCCEEDED(converter->GetSize(&width, &height)) && width == tile_size && height == tile_size) {
            pixels.resize(tile_size * tile_size * 4);
            if (FAILED(converter->CopyPixels(nullptr, tile_size * 4,
                                             static_cast<UINT>(pixels.size()), pixels.data()))) {
                pixels.clear();
            }
        }
    }
    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (stream) stream->Release();
    if (factory) factory->Release();
    return pixels;
}
#else
std::vector<std::uint8_t> download_tile(const TileKey&) { return {}; }
std::vector<std::uint8_t> decode_png(const std::vector<std::uint8_t>&) { return {}; }
#endif

} // namespace

class XPlaneMapTiles::Implementation final {
public:
    explicit Implementation(std::filesystem::path cache_directory)
        : cache_directory_(std::move(cache_directory)), worker_([this] { work(); }) {}

    ~Implementation() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        condition_.notify_one();
        if (worker_.joinable()) {
            worker_.join();
        }
        for (const auto& [key, texture] : textures_) {
            static_cast<void>(key);
            GLuint id = texture;
            glDeleteTextures(1, &id);
        }
    }

    void draw(MapStyle style, const MapTileViewport& viewport) {
        upload_ready();
        const int zoom = tile_zoom(viewport);
        const int count = 1 << zoom;
        const double center_world_x = world_x(viewport.longitude_degrees, zoom);
        const double center_world_y = world_y(viewport.latitude_degrees, zoom);
        const double screen_center_x = (viewport.left + viewport.right) * 0.5;
        const double screen_center_y = (viewport.top + viewport.bottom) * 0.5;
        const int first_x = static_cast<int>(std::floor((center_world_x - (screen_center_x - viewport.left)) / tile_size));
        const int last_x = static_cast<int>(std::floor((center_world_x + (viewport.right - screen_center_x)) / tile_size));
        const int first_y = std::max(0, static_cast<int>(std::floor((center_world_y - (viewport.top - screen_center_y)) / tile_size)));
        const int last_y = std::min(count - 1, static_cast<int>(std::floor((center_world_y + (screen_center_y - viewport.bottom)) / tile_size)));
        std::set<TileKey> visible_keys;

        XPLMSetGraphicsState(0, 1, 0, 0, 1, 0, 0);
        glColor4f(1.0F, 1.0F, 1.0F, 1.0F);
        for (int raw_x = first_x; raw_x <= last_x; ++raw_x) {
            const int x = (raw_x % count + count) % count;
            for (int y = first_y; y <= last_y; ++y) {
                const TileKey key{style, zoom, x, y};
                visible_keys.insert(key);
                const double tile_left = screen_center_x + raw_x * tile_size - center_world_x;
                const double tile_top = screen_center_y - (y * tile_size - center_world_y);
                const auto found = textures_.find(key);
                if (found == textures_.end()) {
                    request(key);
                    continue;
                }
                const double draw_left = std::max(tile_left, static_cast<double>(viewport.left));
                const double draw_right = std::min(tile_left + tile_size, static_cast<double>(viewport.right));
                const double draw_top = std::min(tile_top, static_cast<double>(viewport.top));
                const double draw_bottom = std::max(tile_top - tile_size, static_cast<double>(viewport.bottom));
                if (draw_left >= draw_right || draw_bottom >= draw_top) {
                    continue;
                }
                const double u_left = (draw_left - tile_left) / tile_size;
                const double u_right = (draw_right - tile_left) / tile_size;
                const double v_top = (tile_top - draw_top) / tile_size;
                const double v_bottom = (tile_top - draw_bottom) / tile_size;
                XPLMBindTexture2d(static_cast<int>(found->second), 0);
                glBegin(GL_QUADS);
                glTexCoord2d(u_left, v_bottom); glVertex2d(draw_left, draw_bottom);
                glTexCoord2d(u_right, v_bottom); glVertex2d(draw_right, draw_bottom);
                glTexCoord2d(u_right, v_top); glVertex2d(draw_right, draw_top);
                glTexCoord2d(u_left, v_top); glVertex2d(draw_left, draw_top);
                glEnd();
            }
        }
        XPLMSetGraphicsState(0, 0, 0, 0, 1, 0, 0);
        for (auto iterator = textures_.begin(); textures_.size() > 128 && iterator != textures_.end();) {
            if (visible_keys.contains(iterator->first)) {
                ++iterator;
                continue;
            }
            GLuint texture = iterator->second;
            glDeleteTextures(1, &texture);
            iterator = textures_.erase(iterator);
        }
    }

private:
    void request(const TileKey& key) {
        std::lock_guard lock(mutex_);
        if (const auto failed = failed_until_.find(key); failed != failed_until_.end()) {
            if (std::chrono::steady_clock::now() < failed->second) {
                return;
            }
            failed_until_.erase(failed);
        }
        if (pending_.insert(key).second) {
            requests_.push_back(key);
            condition_.notify_one();
        }
    }

    void upload_ready() {
        std::deque<DecodedTile> ready;
        {
            std::lock_guard lock(mutex_);
            ready.swap(ready_);
        }
        for (auto& tile : ready) {
            GLuint texture{};
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tile_size, tile_size, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, tile.pixels.data());
            textures_.emplace(tile.key, texture);
        }
    }

    void work() {
#if IBM
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif
        for (;;) {
            TileKey key;
            {
                std::unique_lock lock(mutex_);
                condition_.wait(lock, [this] { return stopping_ || !requests_.empty(); });
                if (stopping_) {
                    break;
                }
                key = requests_.front();
                requests_.pop_front();
            }
            const auto path = cache_path(cache_directory_, key);
            auto encoded = fresh_cache_file(path) ? read_file(path) : std::vector<std::uint8_t>{};
            if (encoded.empty()) {
                encoded = download_tile(key);
                if (!encoded.empty()) {
                    save_file(path, encoded);
                } else {
                    // A previously cached tile is preferable to an empty map when a
                    // provider is temporarily unavailable.
                    encoded = read_file(path);
                }
            }
            auto pixels = decode_png(encoded);
            {
                std::lock_guard lock(mutex_);
                pending_.erase(key);
                if (!pixels.empty()) {
                    ready_.push_back({key, std::move(pixels)});
                } else {
                    failed_until_[key] = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                }
            }
        }
#if IBM
        CoUninitialize();
#endif
    }

    std::filesystem::path cache_directory_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<TileKey> requests_;
    std::deque<DecodedTile> ready_;
    std::set<TileKey> pending_;
    std::map<TileKey, std::chrono::steady_clock::time_point> failed_until_;
    std::map<TileKey, GLuint> textures_;
    bool stopping_{false};
    std::thread worker_;
};

XPlaneMapTiles::XPlaneMapTiles(std::filesystem::path cache_directory)
    : implementation_(std::make_unique<Implementation>(std::move(cache_directory))) {}

XPlaneMapTiles::~XPlaneMapTiles() = default;

void XPlaneMapTiles::draw(MapStyle style, const MapTileViewport& viewport) {
    implementation_->draw(style, viewport);
}

} // namespace openefb::xplane
