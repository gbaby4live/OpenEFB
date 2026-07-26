#include "xplane_gpu_image.hpp"

#include <XPLMGraphics.h>

#if IBM
#include <Windows.h>
#include <GL/gl.h>
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#elif APL
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <cstddef>
#include <sstream>
#include <utility>

namespace openefb::xplane {
namespace {

#if IBM
using CreateShader = GLuint(APIENTRY*)(GLenum);
using ShaderSource = void(APIENTRY*)(GLuint, GLsizei, const char* const*, const GLint*);
using CompileShader = void(APIENTRY*)(GLuint);
using GetShaderiv = void(APIENTRY*)(GLuint, GLenum, GLint*);
using GetShaderInfoLog = void(APIENTRY*)(GLuint, GLsizei, GLsizei*, char*);
using DeleteShader = void(APIENTRY*)(GLuint);
using CreateProgram = GLuint(APIENTRY*)();
using AttachShader = void(APIENTRY*)(GLuint, GLuint);
using LinkProgram = void(APIENTRY*)(GLuint);
using GetProgramiv = void(APIENTRY*)(GLuint, GLenum, GLint*);
using GetProgramInfoLog = void(APIENTRY*)(GLuint, GLsizei, GLsizei*, char*);
using UseProgram = void(APIENTRY*)(GLuint);
using DeleteProgram = void(APIENTRY*)(GLuint);
using GetUniformLocation = GLint(APIENTRY*)(GLuint, const char*);
using Uniform1i = void(APIENTRY*)(GLint, GLint);

void* load_gl(const char* name) {
    void* address = reinterpret_cast<void*>(wglGetProcAddress(name));
    if (address == nullptr || address == reinterpret_cast<void*>(1) ||
        address == reinterpret_cast<void*>(2) || address == reinterpret_cast<void*>(3) ||
        address == reinterpret_cast<void*>(-1)) return nullptr;
    return address;
}

struct ShaderApi {
    CreateShader create_shader{reinterpret_cast<CreateShader>(load_gl("glCreateShader"))};
    ShaderSource shader_source{reinterpret_cast<ShaderSource>(load_gl("glShaderSource"))};
    CompileShader compile_shader{reinterpret_cast<CompileShader>(load_gl("glCompileShader"))};
    GetShaderiv get_shader_iv{reinterpret_cast<GetShaderiv>(load_gl("glGetShaderiv"))};
    GetShaderInfoLog get_shader_log{reinterpret_cast<GetShaderInfoLog>(load_gl("glGetShaderInfoLog"))};
    DeleteShader delete_shader{reinterpret_cast<DeleteShader>(load_gl("glDeleteShader"))};
    CreateProgram create_program{reinterpret_cast<CreateProgram>(load_gl("glCreateProgram"))};
    AttachShader attach_shader{reinterpret_cast<AttachShader>(load_gl("glAttachShader"))};
    LinkProgram link_program{reinterpret_cast<LinkProgram>(load_gl("glLinkProgram"))};
    GetProgramiv get_program_iv{reinterpret_cast<GetProgramiv>(load_gl("glGetProgramiv"))};
    GetProgramInfoLog get_program_log{reinterpret_cast<GetProgramInfoLog>(load_gl("glGetProgramInfoLog"))};
    UseProgram use_program{reinterpret_cast<UseProgram>(load_gl("glUseProgram"))};
    DeleteProgram delete_program{reinterpret_cast<DeleteProgram>(load_gl("glDeleteProgram"))};
    GetUniformLocation get_uniform{reinterpret_cast<GetUniformLocation>(load_gl("glGetUniformLocation"))};
    Uniform1i uniform_1i{reinterpret_cast<Uniform1i>(load_gl("glUniform1i"))};

    [[nodiscard]] bool complete() const noexcept {
        return create_shader && shader_source && compile_shader && get_shader_iv && get_shader_log &&
               delete_shader && create_program && attach_shader && link_program && get_program_iv &&
               get_program_log && use_program && delete_program && get_uniform && uniform_1i;
    }
};

constexpr GLenum vertex_shader = 0x8B31;
constexpr GLenum fragment_shader = 0x8B30;
constexpr GLenum compile_status = 0x8B81;
constexpr GLenum link_status = 0x8B82;

GLuint compile(ShaderApi& api, GLenum type, const char* source, std::string& error) {
    const GLuint shader = api.create_shader(type);
    api.shader_source(shader, 1, &source, nullptr);
    api.compile_shader(shader);
    GLint okay{};
    api.get_shader_iv(shader, compile_status, &okay);
    if (okay) return shader;
    char log[512]{};
    api.get_shader_log(shader, static_cast<GLsizei>(sizeof(log) - 1), nullptr, log);
    error = log;
    api.delete_shader(shader);
    return 0;
}

struct SharedShader {
    ShaderApi api;
    GLuint program{};
    GLint sampler{-1};
    std::string status{"SHADER NOT INITIALIZED"};

    bool initialize() {
        if (program) return sampler >= 0;
        if (!api.complete()) {
            status = "OPENGL SHADER API UNAVAILABLE";
            return false;
        }
        constexpr const char* vertex =
            "#version 120\n"
            "varying vec2 uv;\n"
            "void main(){ gl_Position=ftransform(); uv=gl_MultiTexCoord0.xy; }\n";
        constexpr const char* fragment =
            "#version 120\n"
            "uniform sampler2D image;\n"
            "varying vec2 uv;\n"
            "void main(){ gl_FragColor=texture2D(image,uv); }\n";
        std::string error;
        const GLuint vs = compile(api, vertex_shader, vertex, error);
        if (!vs) { status = "VERTEX SHADER: " + error; return false; }
        const GLuint fs = compile(api, fragment_shader, fragment, error);
        if (!fs) { api.delete_shader(vs); status = "FRAGMENT SHADER: " + error; return false; }
        program = api.create_program();
        api.attach_shader(program, vs);
        api.attach_shader(program, fs);
        api.link_program(program);
        api.delete_shader(vs);
        api.delete_shader(fs);
        GLint okay{};
        api.get_program_iv(program, link_status, &okay);
        if (!okay) {
            char log[512]{};
            api.get_program_log(program, static_cast<GLsizei>(sizeof(log) - 1), nullptr, log);
            status = "SHADER LINK: " + std::string(log);
            api.delete_program(program);
            program = 0;
            return false;
        }
        sampler = api.get_uniform(program, "image");
        status = sampler >= 0 ? "GPU IMAGE SHADER READY" : "IMAGE SAMPLER NOT FOUND";
        return sampler >= 0;
    }
};

SharedShader& shared_shader() {
    static SharedShader shader;
    return shader;
}
#endif

} // namespace

class XPlaneGpuImage::Implementation final {
public:
    ~Implementation() {
        if (texture_) glDeleteTextures(1, &texture_);
    }

    bool upload(int width, int height, const std::vector<std::uint8_t>& pixels,
                GpuPixelFormat format) {
        if (width <= 0 || height <= 0 ||
            pixels.size() != static_cast<std::size_t>(width) * height * 4) {
            status_ = "INVALID IMAGE BUFFER";
            return false;
        }
        if (!texture_) {
            int texture{};
            XPLMGenerateTextureNumbers(&texture, 1);
            texture_ = static_cast<GLuint>(texture);
        }
        XPLMSetGraphicsState(0, 1, 0, 0, 0, 0, 0);
        XPLMBindTexture2d(static_cast<int>(texture_), 0);
        static_cast<void>(glGetError());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        const GLenum external = format == GpuPixelFormat::bgra ? GL_BGRA : GL_RGBA;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, external,
                     GL_UNSIGNED_BYTE, nullptr);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, external,
                        GL_UNSIGNED_BYTE, pixels.data());
        const GLenum error = glGetError();
        width_ = width;
        height_ = height;
#if IBM
        const bool shader_ready = shared_shader().initialize();
        status_ = error == GL_NO_ERROR
            ? (shader_ready ? "GPU IMAGE READY" : shared_shader().status)
            : "GL UPLOAD ERROR " + std::to_string(error);
        return error == GL_NO_ERROR && shader_ready;
#else
        status_ = error == GL_NO_ERROR ? "GPU IMAGE READY" : "GL UPLOAD ERROR";
        return error == GL_NO_ERROR;
#endif
    }

    bool draw(double left, double bottom, double right, double top,
              double u_left, double v_bottom, double u_right, double v_top) const {
        if (!texture_ || width_ <= 0 || height_ <= 0) return false;
        XPLMSetGraphicsState(0, 1, 0, 0, 0, 0, 0);
        XPLMBindTexture2d(static_cast<int>(texture_), 0);
#if IBM
        auto& shader = shared_shader();
        if (!shader.initialize()) return false;
        shader.api.use_program(shader.program);
        shader.api.uniform_1i(shader.sampler, 0);
#endif
        glColor4f(1.0F, 1.0F, 1.0F, 1.0F);
        glBegin(GL_QUADS);
        glTexCoord2d(u_left, v_bottom); glVertex2d(left, bottom);
        glTexCoord2d(u_right, v_bottom); glVertex2d(right, bottom);
        glTexCoord2d(u_right, v_top); glVertex2d(right, top);
        glTexCoord2d(u_left, v_top); glVertex2d(left, top);
        glEnd();
#if IBM
        shader.api.use_program(0);
#endif
        return true;
    }

    [[nodiscard]] std::string status() const { return status_; }

private:
    GLuint texture_{};
    int width_{};
    int height_{};
    std::string status_{"GPU IMAGE EMPTY"};
};

XPlaneGpuImage::XPlaneGpuImage() : implementation_(std::make_unique<Implementation>()) {}
XPlaneGpuImage::~XPlaneGpuImage() = default;
bool XPlaneGpuImage::upload(int width, int height, const std::vector<std::uint8_t>& pixels,
                            GpuPixelFormat format) {
    return implementation_->upload(width, height, pixels, format);
}
bool XPlaneGpuImage::draw(double left, double bottom, double right, double top,
                          double u_left, double v_bottom, double u_right, double v_top) const {
    return implementation_->draw(left, bottom, right, top,
                                 u_left, v_bottom, u_right, v_top);
}
std::string XPlaneGpuImage::status() const { return implementation_->status(); }

} // namespace openefb::xplane
