// Headless EGL + OpenGL ES 3.2 context and shader-link helpers of the GPU map
// renderer. Split out of map_gpu_render.cpp.

#include "map_gpu_gl.hpp"

#include <EGL/eglext.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace chrononplugin::maps::gpu_detail {

EglContext::~EglContext() { shutdown(); }

void EglContext::shutdown() {
    if (display != EGL_NO_DISPLAY) {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
        if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
        eglTerminate(display);
    }
    display = EGL_NO_DISPLAY;
    context = EGL_NO_CONTEXT;
    surface = EGL_NO_SURFACE;
}

bool EglContext::init(std::string& error) {
    auto get_platform_display =
        reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));
    auto query_devices = reinterpret_cast<PFNEGLQUERYDEVICESEXTPROC>(
        eglGetProcAddress("eglQueryDevicesEXT"));

    std::vector<EGLDisplay> candidates;
    if (get_platform_display && query_devices) {
        EGLDeviceEXT devices[8]{};
        EGLint device_count = 0;
        if (query_devices(8, devices, &device_count)) {
            for (EGLint i = 0; i < device_count; ++i) {
                candidates.push_back(get_platform_display(
                    EGL_PLATFORM_DEVICE_EXT, devices[i], nullptr));
            }
        }
    }
    if (get_platform_display) {
        candidates.push_back(get_platform_display(
            EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr));
    }
    candidates.push_back(eglGetDisplay(EGL_DEFAULT_DISPLAY));

    const EGLint config_attributes[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_NONE};
    const EGLint pbuffer_attributes[] = {
        EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    const EGLint context_attributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};

    for (const EGLDisplay candidate : candidates) {
        if (candidate == EGL_NO_DISPLAY) continue;
        EGLint major = 0;
        EGLint minor = 0;
        if (!eglInitialize(candidate, &major, &minor)) continue;
        if (!eglBindAPI(EGL_OPENGL_ES_API)) {
            eglTerminate(candidate);
            continue;
        }
        EGLConfig config = nullptr;
        EGLint config_count = 0;
        if (!eglChooseConfig(candidate, config_attributes, &config, 1,
                             &config_count) ||
            config_count < 1) {
            eglTerminate(candidate);
            continue;
        }
        EGLSurface pbuffer = eglCreatePbufferSurface(
            candidate, config, pbuffer_attributes);
        if (pbuffer == EGL_NO_SURFACE) {
            eglTerminate(candidate);
            continue;
        }
        EGLContext context_handle = eglCreateContext(
            candidate, config, EGL_NO_CONTEXT, context_attributes);
        if (context_handle == EGL_NO_CONTEXT ||
            !eglMakeCurrent(candidate, pbuffer, pbuffer, context_handle)) {
            if (context_handle != EGL_NO_CONTEXT)
                eglDestroyContext(candidate, context_handle);
            eglDestroySurface(candidate, pbuffer);
            eglTerminate(candidate);
            continue;
        }
        display = candidate;
        context = context_handle;
        surface = pbuffer;
        const auto* gl_version = glGetString(GL_VERSION);
        const auto* gl_renderer = glGetString(GL_RENDERER);
        version = gl_version ? reinterpret_cast<const char*>(gl_version) : "";
        renderer = gl_renderer ? reinterpret_cast<const char*>(gl_renderer) : "";
        error.clear();
        return true;
    }
    error = "no headless EGL/OpenGL ES 3.2 display could be initialized";
    return false;
}

// ── GL helpers ─────────────────────────────────────────────────────────────

[[noreturn]] void gl_fail(const std::string& message) {
    std::fprintf(stderr, "map-gpu: %s\n", message.c_str());
    std::exit(1);
}

GLuint compile_shader(GLenum type, const char* source, const char* label) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        GLint log_length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
        std::vector<char> log(static_cast<std::size_t>(std::max(1, log_length)));
        glGetShaderInfoLog(shader, log_length, nullptr, log.data());
        gl_fail(std::string("shader compile failed (") + label + "):\n" + log.data());
    }
    return shader;
}

GLuint link_program(GLuint vertex_shader, GLuint fragment_shader, const char* label) {
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        GLint log_length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
        std::vector<char> log(static_cast<std::size_t>(std::max(1, log_length)));
        glGetProgramInfoLog(program, log_length, nullptr, log.data());
        gl_fail(std::string("program link failed (") + label + "):\n" + log.data());
    }
    return program;
}

GLuint link_compute_program(GLuint compute_shader, const char* label) {
    const GLuint program = glCreateProgram();
    glAttachShader(program, compute_shader);
    glLinkProgram(program);
    glDeleteShader(compute_shader);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        GLint log_length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
        std::vector<char> log(static_cast<std::size_t>(std::max(1, log_length)));
        glGetProgramInfoLog(program, log_length, nullptr, log.data());
        gl_fail(std::string("compute link failed (") + label + "):\n" + log.data());
    }
    return program;
}

void check_framebuffer() {
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::ostringstream message;
        message << "framebuffer incomplete: 0x" << std::hex << status;
        gl_fail(message.str());
    }
}

}  // namespace chrononplugin::maps::gpu_detail
