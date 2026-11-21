#pragma once

// Private to the map_gpu tool: the headless EGL/OpenGL ES 3.2 context and the
// shader-link helpers shared by the renderer.

#include <EGL/egl.h>
#include <GLES3/gl31.h>

#include <string>

namespace chrononplugin::maps::gpu_detail {

struct EglContext {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    std::string renderer;
    std::string version;

    ~EglContext();

    void shutdown();

    bool init(std::string& error);
};

[[noreturn]] void gl_fail(const std::string& message);

GLuint compile_shader(GLenum type, const char* source, const char* label);

GLuint link_program(GLuint vertex_shader, GLuint fragment_shader, const char* label);

GLuint link_compute_program(GLuint compute_shader, const char* label);

void check_framebuffer();

}  // namespace chrononplugin::maps::gpu_detail
