// GPU map-render prototype — headless EGL + OpenGL ES 3.2.
//
// This is deliberately independent from the OpenCV CPU map compositor:
//   * render target: RGBA8 FBO + GL_DEPTH_COMPONENT24 depth renderbuffer;
//   * 2.5D: instanced extruded blocks, perspective camera, hardware depth test;
//   * particles: 98,304 agents in an SSBO updated by a compute shader, rendered
//     as additive GPU line trails and glow points;
//   * output: glReadPixels -> FFmpeg/NVENC pipe.
//
// No OpenCV, no CPU raster fallback, no synthetic depth. The only CPU image
// work is the small HUD bitmap font drawn after readback.

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr int kParticleCount = 32768;  // 128 workgroups of 256

// ── tiny math layer (column-major, GLSL-compatible) ────────────────────────

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(const Vec3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vec3 normalize(const Vec3& v) {
    const float length = std::sqrt(std::max(1e-12f, dot(v, v)));
    return {v.x / length, v.y / length, v.z / length};
}

struct Mat4 {
    std::array<float, 16> m{};
    const float* data() const { return m.data(); }
};

Mat4 identity() {
    Mat4 result;
    result.m = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    return result;
}

Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 result;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float value = 0.0f;
            for (int k = 0; k < 4; ++k) value += a.m[k * 4 + row] * b.m[col * 4 + k];
            result.m[col * 4 + row] = value;
        }
    }
    return result;
}

Mat4 perspective(float fov_y_radians, float aspect, float near_plane, float far_plane) {
    Mat4 result;
    const float f = 1.0f / std::tan(fov_y_radians * 0.5f);
    result.m[0] = f / aspect;
    result.m[5] = f;
    result.m[10] = (far_plane + near_plane) / (near_plane - far_plane);
    result.m[11] = -1.0f;
    result.m[14] = (2.0f * far_plane * near_plane) / (near_plane - far_plane);
    return result;
}

Mat4 look_at(const Vec3& eye, const Vec3& center, const Vec3& up) {
    const Vec3 f = normalize(center - eye);
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);
    Mat4 result = identity();
    result.m[0] = s.x;  result.m[4] = s.y;  result.m[8] = s.z;
    result.m[1] = u.x;  result.m[5] = u.y;  result.m[9] = u.z;
    result.m[2] = -f.x; result.m[6] = -f.y; result.m[10] = -f.z;
    result.m[12] = -dot(s, eye);
    result.m[13] = -dot(u, eye);
    result.m[14] = dot(f, eye);
    return result;
}

// ── headless EGL ───────────────────────────────────────────────────────────

struct EglContext {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    std::string renderer;
    std::string version;

    ~EglContext() { shutdown(); }

    void shutdown() {
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

    bool init(std::string& error) {
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
};

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

// ── map shape / geometry ───────────────────────────────────────────────────

struct Point2 {
    float x = 0.0f;
    float z = 0.0f;
};

struct MapShape {
    std::vector<std::vector<Point2>> rings;
    float min_x = 0.0f;
    float max_x = 0.0f;
    float min_z = 0.0f;
    float max_z = 0.0f;
};

bool point_in_ring(const std::vector<Point2>& ring, float x, float z) {
    bool inside = false;
    if (ring.size() < 3) return false;
    for (std::size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
        const Point2& a = ring[i];
        const Point2& b = ring[j];
        const bool crosses = ((a.z > z) != (b.z > z)) &&
            (x < (b.x - a.x) * (z - a.z) / ((b.z - a.z) + 1e-12f) + a.x);
        if (crosses) inside = !inside;
    }
    return inside;
}

bool shape_contains(const MapShape& shape, float x, float z) {
    for (const auto& ring : shape.rings) {
        if (point_in_ring(ring, x, z)) return true;
    }
    return false;
}

MapShape load_shape(const std::string& path, const std::string& feature_name,
                    std::string& error) {
    std::ifstream input(path);
    if (!input) {
        error = "cannot open GeoJSON: " + path;
        return {};
    }
    nlohmann::json document;
    try {
        input >> document;
    } catch (const std::exception& exc) {
        error = std::string("cannot parse GeoJSON: ") + exc.what();
        return {};
    }
    const auto features = document.find("features");
    if (features == document.end() || !features->is_array()) {
        error = "GeoJSON has no FeatureCollection features";
        return {};
    }

    std::vector<std::vector<std::pair<double, double>>> raw_rings;
    double min_lon = 1e30, max_lon = -1e30, min_lat = 1e30, max_lat = -1e30;
    for (const auto& feature : *features) {
        if (!feature.is_object()) continue;
        const auto properties = feature.find("properties");
        if (properties == feature.end() ||
            properties->value("shapeName", "") != feature_name) {
            continue;
        }
        const auto geometry = feature.find("geometry");
        if (geometry == feature.end() || !geometry->is_object()) continue;
        const std::string type = geometry->value("type", "");
        const auto coordinates = geometry->find("coordinates");
        if (coordinates == geometry->end()) continue;

        auto add_polygon = [&](const nlohmann::json& polygon) {
            if (!polygon.is_array() || polygon.empty()) return;
            const auto& outer = polygon.at(0);
            if (!outer.is_array() || outer.size() < 3) return;
            std::vector<std::pair<double, double>> ring;
            ring.reserve(outer.size());
            for (const auto& point : outer) {
                if (!point.is_array() || point.size() < 2) continue;
                const double lon = point.at(0).get<double>();
                const double lat = point.at(1).get<double>();
                ring.emplace_back(lon, lat);
                min_lon = std::min(min_lon, lon);
                max_lon = std::max(max_lon, lon);
                min_lat = std::min(min_lat, lat);
                max_lat = std::max(max_lat, lat);
            }
            if (ring.size() >= 3) raw_rings.push_back(std::move(ring));
        };

        if (type == "Polygon") {
            add_polygon(*coordinates);
        } else if (type == "MultiPolygon") {
            for (const auto& polygon : *coordinates) add_polygon(polygon);
        }
    }
    if (raw_rings.empty()) {
        error = "territory feature not found in GeoJSON: " + feature_name;
        return {};
    }

    const double center_lon = 0.5 * (min_lon + max_lon);
    const double center_lat = 0.5 * (min_lat + max_lat);
    const double cos_lat = std::cos(center_lat * kPi / 180.0);
    const double width_m = (max_lon - min_lon) * 111320.0 * cos_lat;
    const double height_m = (max_lat - min_lat) * 110540.0;
    const double scale = 1.9 / std::max(1.0, std::max(width_m, height_m));

    MapShape shape;
    shape.min_x = 1e30f;
    shape.max_x = -1e30f;
    shape.min_z = 1e30f;
    shape.max_z = -1e30f;
    for (const auto& raw : raw_rings) {
        std::vector<Point2> ring;
        ring.reserve(raw.size());
        for (const auto& point : raw) {
            const float x = static_cast<float>(
                (point.first - center_lon) * 111320.0 * cos_lat * scale);
            const float z = static_cast<float>(
                -(point.second - center_lat) * 110540.0 * scale);
            ring.push_back({x, z});
            shape.min_x = std::min(shape.min_x, x);
            shape.max_x = std::max(shape.max_x, x);
            shape.min_z = std::min(shape.min_z, z);
            shape.max_z = std::max(shape.max_z, z);
        }
        shape.rings.push_back(std::move(ring));
    }
    error.clear();
    return shape;
}

struct Instance {
    float x = 0.0f;       // world x
    float z = 0.0f;       // world z
    float half_x = 0.0f;  // half width
    float half_z = 0.0f;  // half depth
    float height = 0.0f;  // world y extent
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

struct Geometry {
    std::vector<Instance> plates;
    std::vector<Instance> towers;
    std::vector<Vec3> outline;
    float max_height = 0.0f;
};

float hash01(std::uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return static_cast<float>(value & 0x00ffffffU) /
           static_cast<float>(0x00ffffffU);
}

Geometry build_geometry(const MapShape& shape) {
    Geometry geometry;
    const int grid_x = 96;
    const int grid_z = 64;
    const float cell_x = (shape.max_x - shape.min_x) / grid_x;
    const float cell_z = (shape.max_z - shape.min_z) / grid_z;
    geometry.outline = {};
    for (const auto& ring : shape.rings) {
        for (std::size_t i = 0; i < ring.size(); ++i) {
            const Point2& a = ring[i];
            const Point2& b = ring[(i + 1) % ring.size()];
            geometry.outline.push_back({a.x, 0.016f, a.z});
            geometry.outline.push_back({b.x, 0.016f, b.z});
        }
    }

    for (int iz = 0; iz < grid_z; ++iz) {
        for (int ix = 0; ix < grid_x; ++ix) {
            const float x = shape.min_x + (ix + 0.5f) * cell_x;
            const float z = shape.min_z + (iz + 0.5f) * cell_z;
            if (!shape_contains(shape, x, z)) continue;
            const float noise = hash01(static_cast<std::uint32_t>(ix) * 0x9e3779b9U ^
                                       static_cast<std::uint32_t>(iz) * 0x85ebca6bU);
            const float cluster = 0.5f + 0.5f * std::sin(
                x * 9.0f + z * 5.0f + noise * 2.0f);
            const float height = 0.018f + 0.20f *
                std::pow(noise, 1.7f) * (0.58f + 0.42f * cluster);

            geometry.plates.push_back(
                {x, z, cell_x * 0.47f, cell_z * 0.47f, 0.006f,
                 0.02f, 0.20f, 0.24f});
            if (noise > 0.06f) {
                const float t = std::clamp((height - 0.02f) / 0.22f, 0.0f, 1.0f);
                geometry.towers.push_back(
                    {x, z, cell_x * 0.33f, cell_z * 0.33f, height,
                     0.07f + 0.83f * t, 0.52f + 0.30f * t, 0.92f - 0.64f * t});
                geometry.max_height = std::max(geometry.max_height, height);
            }
        }
    }
    geometry.max_height = std::max(0.08f, geometry.max_height);
    return geometry;
}

// ── shaders ────────────────────────────────────────────────────────────────

const char* kSceneVS = R"GLSL(#version 310 es
precision highp float;
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_inst0;
layout(location = 3) in vec4 a_inst1;
uniform mat4 u_view_proj;
out vec3 v_world;
out vec3 v_normal;
out vec3 v_color;
void main() {
    vec3 world = vec3(
        a_pos.x * a_inst0.z * 2.0 + a_inst0.x,
        a_pos.y * a_inst1.x,
        a_pos.z * a_inst0.w * 2.0 + a_inst0.y);
    v_world = world;
    v_normal = a_normal;
    v_color = a_inst1.yzw;
    gl_Position = u_view_proj * vec4(world, 1.0);
}
)GLSL";

const char* kSceneFS = R"GLSL(#version 310 es
precision highp float;
in vec3 v_world;
in vec3 v_normal;
in vec3 v_color;
uniform float u_scan_height;
uniform float u_max_height;
out vec4 out_color;
void main() {
    if (v_world.y > u_scan_height) discard;
    vec3 normal = normalize(v_normal);
    vec3 light = normalize(vec3(0.42, 0.88, 0.32));
    float ndl = max(dot(normal, light), 0.0);
    vec3 color = v_color * (0.36 + 0.76 * ndl);
    float h = clamp(v_world.y / max(u_max_height, 0.0001), 0.0, 1.0);
    color += v_color * 0.18 * h;
    float edge = smoothstep(u_scan_height - 0.024, u_scan_height, v_world.y);
    color += vec3(0.03, 0.55, 0.90) * edge * 0.85;
    float distance_fade = exp(-0.09 * length(v_world.xz));
    out_color = vec4(color * distance_fade, 1.0);
}
)GLSL";

const char* kFloorVS = R"GLSL(#version 310 es
precision highp float;
layout(location = 0) in vec3 a_pos;
uniform mat4 u_view_proj;
out vec3 v_world;
void main() {
    v_world = a_pos;
    gl_Position = u_view_proj * vec4(a_pos, 1.0);
}
)GLSL";

const char* kFloorFS = R"GLSL(#version 310 es
precision highp float;
in vec3 v_world;
out vec4 out_color;
void main() {
    vec2 grid = abs(fract(v_world.xz * 12.0) - 0.5);
    float line = smoothstep(0.465, 0.500, max(grid.x, grid.y));
    vec3 base = vec3(0.004, 0.010, 0.018);
    vec3 color = base + vec3(0.018, 0.085, 0.125) * line;
    float radial = exp(-0.08 * length(v_world.xz));
    out_color = vec4(color * radial, 1.0);
}
)GLSL";

const char* kLineVS = R"GLSL(#version 310 es
precision highp float;
layout(location = 0) in vec3 a_pos;
uniform mat4 u_view_proj;
void main() {
    gl_Position = u_view_proj * vec4(a_pos, 1.0);
}
)GLSL";

const char* kLineFS = R"GLSL(#version 310 es
precision highp float;
uniform vec3 u_color;
out vec4 out_color;
void main() { out_color = vec4(u_color, 1.0); }
)GLSL";

const char* kScanVS = R"GLSL(#version 310 es
precision highp float;
layout(location = 0) in vec3 a_pos;
uniform mat4 u_view_proj;
uniform float u_scan_y;
out vec3 v_world;
void main() {
    v_world = vec3(a_pos.x, u_scan_y, a_pos.z);
    gl_Position = u_view_proj * vec4(v_world, 1.0);
}
)GLSL";

const char* kScanFS = R"GLSL(#version 310 es
precision highp float;
uniform vec4 u_color;
out vec4 out_color;
void main() { out_color = u_color; }
)GLSL";

const char* kParticleCompute = R"GLSL(#version 310 es
precision highp float;
layout(local_size_x = 256) in;
layout(std430, binding = 0) buffer ParticleData { vec4 data[]; };
uniform int u_count;
uniform float u_dt;
uniform float u_time;
uniform vec4 u_bounds;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(u_count)) return;
    uint base = i * 8u;
    vec4 pos = data[base];
    vec4 vel = data[base + 1u];
    for (int k = 5; k > 0; --k) {
        data[base + 2u + uint(k)] = data[base + 1u + uint(k)];
    }
    pos.x += vel.x * u_dt;
    pos.z += vel.z * u_dt;
    if (pos.x > u_bounds.y) pos.x = u_bounds.x;
    if (pos.x < u_bounds.x) pos.x = u_bounds.y;
    if (pos.z > u_bounds.w) pos.z = u_bounds.z;
    if (pos.z < u_bounds.z) pos.z = u_bounds.w;
    pos.y = 0.045 + 0.018 * sin(u_time * 2.3 + vel.w * 6.2831853);
    data[base] = pos;
    data[base + 2u] = vec4(pos.xyz, 1.0);
}
)GLSL";

const char* kParticleLineVS = R"GLSL(#version 310 es
precision highp float;
layout(std430, binding = 0) buffer ParticleData { vec4 data[]; };
uniform mat4 u_view_proj;
out vec3 v_color;
out vec3 v_world;
out float v_alpha;
void main() {
    int particle = gl_VertexID / 10;
    int segment = (gl_VertexID % 10) / 2;
    int endpoint = gl_VertexID % 2;
    int index = particle * 8 + 2 + segment + endpoint;
    vec4 p = data[index];
    gl_Position = u_view_proj * vec4(p.xyz, 1.0);
    v_world = p.xyz;
    vec3 color = (particle % 3 == 0)
        ? vec3(1.0, 0.46, 0.05) : vec3(0.04, 0.58, 1.0);
    float fade = 1.0 - float(segment + endpoint) / 6.0;
    v_color = color;
    v_alpha = fade * 0.17;
}
)GLSL";

const char* kParticleLineFS = R"GLSL(#version 310 es
precision highp float;
in vec3 v_color;
in vec3 v_world;
in float v_alpha;
uniform vec4 u_bounds;
uniform highp sampler2D u_mask;
out vec4 out_color;
void main() {
    vec2 uv = (v_world.xz - u_bounds.xy) /
              max(u_bounds.zw - u_bounds.xy, vec2(0.0001));
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) discard;
    if (texture(u_mask, uv).r < 0.5) discard;
    out_color = vec4(v_color * v_alpha, v_alpha);
}
)GLSL";

const char* kParticlePointVS = R"GLSL(#version 310 es
precision highp float;
layout(std430, binding = 0) buffer ParticleData { vec4 data[]; };
uniform mat4 u_view_proj;
out vec3 v_color;
out vec3 v_world;
void main() {
    int particle = gl_VertexID;
    vec4 p = data[particle * 8 + 2];
    gl_Position = u_view_proj * vec4(p.xyz, 1.0);
    v_world = p.xyz;
    gl_PointSize = 2.0;
    v_color = (particle % 3 == 0)
        ? vec3(1.0, 0.64, 0.16) : vec3(0.14, 0.78, 1.0);
}
)GLSL";

const char* kParticlePointFS = R"GLSL(#version 310 es
precision highp float;
in vec3 v_color;
in vec3 v_world;
uniform vec4 u_bounds;
uniform highp sampler2D u_mask;
out vec4 out_color;
void main() {
    vec2 uv = (v_world.xz - u_bounds.xy) /
              max(u_bounds.zw - u_bounds.xy, vec2(0.0001));
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) discard;
    if (texture(u_mask, uv).r < 0.5) discard;
    float d = length(gl_PointCoord - vec2(0.5));
    float alpha = smoothstep(0.5, 0.03, d) * 0.55;
    out_color = vec4(v_color * alpha, alpha);
}
)GLSL";

// ── bitmap HUD (5x7, BGR output; no OpenCV) ────────────────────────────────

struct Glyph {
    char character;
    std::array<std::uint8_t, 7> rows;
};

const std::array<Glyph, 40>& glyphs() {
    static const std::array<Glyph, 40> kGlyphs{{
        {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
        {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
        {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
        {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
        {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
        {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
        {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}},
        {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
        {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
        {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
        {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
        {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
        {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
        {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
        {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
        {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
        {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
        {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
        {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
        {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
        {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
        {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
        {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},
        {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
        {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
        {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
        {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
        {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
        {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
        {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
        {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
        {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
        {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
        {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
        {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
        {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
        {'/', {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10}},
        {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
        {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}},
        {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    }};
    return kGlyphs;
}

const Glyph* find_glyph(char character) {
    if (character == ' ') return nullptr;
    const char upper = static_cast<char>(
        std::toupper(static_cast<unsigned char>(character)));
    for (const Glyph& glyph : glyphs()) {
        if (glyph.character == upper) return &glyph;
    }
    return nullptr;
}

void put_pixel(std::vector<std::uint8_t>& bgr, int width, int height,
               int x, int y, std::uint8_t b, std::uint8_t g, std::uint8_t r) {
    if (x < 0 || y < 0 || x >= width || y >= height) return;
    const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 3;
    bgr[offset] = b;
    bgr[offset + 1] = g;
    bgr[offset + 2] = r;
}

void fill_rect(std::vector<std::uint8_t>& bgr, int width, int height,
               int x, int y, int w, int h,
               std::uint8_t b, std::uint8_t g, std::uint8_t r) {
    for (int yy = 0; yy < h; ++yy) {
        for (int xx = 0; xx < w; ++xx) {
            put_pixel(bgr, width, height, x + xx, y + yy, b, g, r);
        }
    }
}

int text_width(const std::string& text, int scale) {
    return static_cast<int>(text.size()) * 6 * scale;
}

void draw_text(std::vector<std::uint8_t>& bgr, int width, int height,
               int x, int y, const std::string& text, int scale,
               std::uint8_t b, std::uint8_t g, std::uint8_t r,
               bool shadow = true) {
    int cursor = x;
    for (const char character : text) {
        const Glyph* glyph = find_glyph(character);
        if (character == ' ') {
            cursor += 4 * scale;
            continue;
        }
        if (glyph == nullptr) {
            cursor += 6 * scale;
            continue;
        }
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if ((glyph->rows[row] & (1u << (4 - col))) == 0) continue;
                if (shadow) {
                    fill_rect(bgr, width, height,
                              cursor + col * scale + 2, y + row * scale + 2,
                              scale, scale, 0, 0, 0);
                }
                fill_rect(bgr, width, height,
                          cursor + col * scale, y + row * scale,
                          scale, scale, b, g, r);
            }
        }
        cursor += 6 * scale;
    }
}

void draw_corner(std::vector<std::uint8_t>& bgr, int width, int height,
                 int x, int y, int sx, int sy, int length,
                 std::uint8_t b, std::uint8_t g, std::uint8_t r) {
    fill_rect(bgr, width, height, sx > 0 ? x : x - length, y,
              length, 2, b, g, r);
    fill_rect(bgr, width, height, x, sy > 0 ? y : y - length,
              2, length, b, g, r);
}

void draw_hud(std::vector<std::uint8_t>& bgr, int width, int height,
              const std::string& scene, int frame, int frames, float t,
              const std::string& renderer) {
    const int margin = 42;
    const std::uint8_t cyan_b = 255, cyan_g = 214, cyan_r = 62;
    const std::uint8_t white_b = 248, white_g = 250, white_r = 252;
    draw_corner(bgr, width, height, margin, margin, 1, 1, 58,
                cyan_b, cyan_g, cyan_r);
    draw_corner(bgr, width, height, width - margin, margin, -1, 1, 58,
                cyan_b, cyan_g, cyan_r);

    const std::string title = scene == "particles"
        ? "GPU PARTICLE FLOW / COMPUTE SHADER"
        : "GPU VOLUMETRIC / DEPTH BUFFER";
    draw_text(bgr, width, height, margin, margin + 24, title, 3,
              cyan_b, cyan_g, cyan_r);
    const std::string subtitle = scene == "particles"
        ? "32768 AGENTS / SSBO TRAILS"
        : "24-BIT DEPTH / INSTANCED 2.5D";
    draw_text(bgr, width, height, margin, margin + 58, subtitle, 2,
              210, 214, 222);

    char progress[64];
    std::snprintf(progress, sizeof(progress), "%s %03d / %03d",
                  scene == "particles" ? "FLOW" : "SCAN",
                  frame + 1, frames);
    draw_text(bgr, width, height,
              width - margin - text_width(progress, 2), margin + 24,
              progress, 2, 235, 235, 240);

    draw_text(bgr, width, height, margin, height - 108, "JERUSALEM", 5,
              white_b, white_g, white_r);
    const std::string footer = renderer;
    draw_text(bgr, width, height,
              margin, height - 46, footer, 2, 160, 174, 188);

    const int line_y = margin + 92;
    fill_rect(bgr, width, height, margin, line_y, 122, 2,
              cyan_b, cyan_g, cyan_r);
    fill_rect(bgr, width, height, width - margin - 122, line_y, 122, 2,
              cyan_b, cyan_g, cyan_r);

    // A small GPU/scan telemetry strip; it is deliberately outside the 3D
    // render so the HUD cannot hide whether the depth pass is working.
    const int bar_x = width - margin - 260;
    const int bar_y = height - 58;
    fill_rect(bgr, width, height, bar_x, bar_y, 260, 2, 50, 70, 84);
    fill_rect(bgr, width, height, bar_x, bar_y,
              static_cast<int>(260.0f * std::clamp(t, 0.0f, 1.0f)), 2,
              cyan_b, cyan_g, cyan_r);
}

// ── GPU renderer ───────────────────────────────────────────────────────────

struct GpuRenderer {
    EglContext egl;
    int width = 0;
    int height = 0;
    GLuint fbo = 0;
    GLuint color_texture = 0;
    GLuint depth_renderbuffer = 0;

    GLuint scene_program = 0;
    GLuint floor_program = 0;
    GLuint line_program = 0;
    GLuint scan_program = 0;
    GLuint particle_compute_program = 0;
    GLuint particle_line_program = 0;
    GLuint particle_point_program = 0;

    GLuint cube_vao = 0;
    GLuint cube_vbo = 0;
    GLuint cube_normal_vbo = 0;
    GLuint cube_ibo = 0;
    GLuint instance_vbo = 0;
    GLuint floor_vao = 0;
    GLuint floor_vbo = 0;
    GLuint outline_vao = 0;
    GLuint outline_vbo = 0;
    GLuint particle_vao = 0;
    GLuint particle_ssbo = 0;
    GLuint particle_mask_texture = 0;
    float bounds_min_x = -1.0f;
    float bounds_max_x = 1.0f;
    float bounds_min_z = -1.0f;
    float bounds_max_z = 1.0f;

    int plate_count = 0;
    int total_instance_count = 0;
    int outline_count = 0;
    float max_height = 0.0f;
    int particle_count = 0;
    std::string gl_renderer;

    GLint scene_view_proj = -1;
    GLint scene_scan_height = -1;
    GLint scene_max_height = -1;
    GLint floor_view_proj = -1;
    GLint line_view_proj = -1;
    GLint line_color = -1;
    GLint scan_view_proj = -1;
    GLint scan_y = -1;
    GLint scan_color = -1;
    GLint particle_view_proj_line = -1;
    GLint particle_view_proj_point = -1;
    GLint particle_bounds_line = -1;
    GLint particle_bounds_point = -1;

    ~GpuRenderer() { shutdown(); }

    void shutdown() {
        if (egl.display == EGL_NO_DISPLAY) return;
        const GLuint buffers[] = {instance_vbo, floor_vbo, outline_vbo,
                                  particle_ssbo, cube_vbo, cube_normal_vbo,
                                  cube_ibo};
        glDeleteBuffers(7, buffers);
        const GLuint vaos[] = {cube_vao, floor_vao, outline_vao, particle_vao};
        glDeleteVertexArrays(4, vaos);
        const GLuint programs[] = {scene_program, floor_program, line_program,
                                   scan_program, particle_compute_program,
                                   particle_line_program,
                                   particle_point_program};
        for (const GLuint program : programs) {
            if (program != 0) glDeleteProgram(program);
        }
        if (particle_mask_texture != 0)
            glDeleteTextures(1, &particle_mask_texture);
        if (color_texture != 0) glDeleteTextures(1, &color_texture);
        if (depth_renderbuffer != 0) glDeleteRenderbuffers(1, &depth_renderbuffer);
        if (fbo != 0) glDeleteFramebuffers(1, &fbo);
        egl.shutdown();
    }

    bool init(const std::string& shape_path, const std::string& feature,
              std::string& error) {
        if (!egl.init(error)) return false;
        gl_renderer = egl.renderer;
        std::string shape_error;
        const MapShape shape = load_shape(shape_path, feature, shape_error);
        if (!shape_error.empty()) {
            error = shape_error;
            return false;
        }
        const Geometry geometry = build_geometry(shape);
        max_height = geometry.max_height;
        if (!create_framebuffer(error)) return false;
        if (!create_programs(error)) return false;
        if (!create_cube(error)) return false;
        if (!create_floor(error)) return false;
        if (!create_outline(geometry, error)) return false;
        if (!create_instances(geometry, error)) return false;
        if (!create_particles(shape, error)) return false;
        error.clear();
        return true;
    }

    bool create_framebuffer(std::string& error) {
        glGenTextures(1, &color_texture);
        glBindTexture(GL_TEXTURE_2D, color_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glGenRenderbuffers(1, &depth_renderbuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, depth_renderbuffer);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);

        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, color_texture, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, depth_renderbuffer);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            error = "RGBA8 + DEPTH24 framebuffer is incomplete";
            return false;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return true;
    }

    bool create_programs(std::string& error) {
        scene_program = link_program(
            compile_shader(GL_VERTEX_SHADER, kSceneVS, "scene.vs"),
            compile_shader(GL_FRAGMENT_SHADER, kSceneFS, "scene.fs"), "scene");
        floor_program = link_program(
            compile_shader(GL_VERTEX_SHADER, kFloorVS, "floor.vs"),
            compile_shader(GL_FRAGMENT_SHADER, kFloorFS, "floor.fs"), "floor");
        line_program = link_program(
            compile_shader(GL_VERTEX_SHADER, kLineVS, "line.vs"),
            compile_shader(GL_FRAGMENT_SHADER, kLineFS, "line.fs"), "line");
        scan_program = link_program(
            compile_shader(GL_VERTEX_SHADER, kScanVS, "scan.vs"),
            compile_shader(GL_FRAGMENT_SHADER, kScanFS, "scan.fs"), "scan");
        particle_compute_program = link_compute_program(
            compile_shader(GL_COMPUTE_SHADER, kParticleCompute,
                           "particle.comp"), "particle.comp");
        particle_line_program = link_program(
            compile_shader(GL_VERTEX_SHADER, kParticleLineVS, "particle-line.vs"),
            compile_shader(GL_FRAGMENT_SHADER, kParticleLineFS, "particle-line.fs"),
            "particle-line");
        particle_point_program = link_program(
            compile_shader(GL_VERTEX_SHADER, kParticlePointVS, "particle-point.vs"),
            compile_shader(GL_FRAGMENT_SHADER, kParticlePointFS, "particle-point.fs"),
            "particle-point");

        scene_view_proj = glGetUniformLocation(scene_program, "u_view_proj");
        scene_scan_height = glGetUniformLocation(scene_program, "u_scan_height");
        scene_max_height = glGetUniformLocation(scene_program, "u_max_height");
        floor_view_proj = glGetUniformLocation(floor_program, "u_view_proj");
        line_view_proj = glGetUniformLocation(line_program, "u_view_proj");
        line_color = glGetUniformLocation(line_program, "u_color");
        scan_view_proj = glGetUniformLocation(scan_program, "u_view_proj");
        scan_y = glGetUniformLocation(scan_program, "u_scan_y");
        scan_color = glGetUniformLocation(scan_program, "u_color");
        particle_view_proj_line = glGetUniformLocation(
            particle_line_program, "u_view_proj");
        particle_view_proj_point = glGetUniformLocation(
            particle_point_program, "u_view_proj");
        particle_bounds_line = glGetUniformLocation(
            particle_line_program, "u_bounds");
        particle_bounds_point = glGetUniformLocation(
            particle_point_program, "u_bounds");
        glUseProgram(particle_line_program);
        glUniform1i(glGetUniformLocation(particle_line_program, "u_mask"), 0);
        glUseProgram(particle_point_program);
        glUniform1i(glGetUniformLocation(particle_point_program, "u_mask"), 0);
        if (scene_view_proj < 0 || scene_scan_height < 0 ||
            scene_max_height < 0 || floor_view_proj < 0 ||
            line_view_proj < 0 || line_color < 0 || scan_view_proj < 0 ||
            scan_y < 0 || scan_color < 0 || particle_view_proj_line < 0 ||
            particle_view_proj_point < 0 || particle_bounds_line < 0 ||
            particle_bounds_point < 0) {
            error = "one or more shader uniforms were optimized out";
            return false;
        }
        error.clear();
        return true;
    }

    bool create_cube(std::string& error) {
        const float positions[] = {
            0.5f, 0.0f, 0.5f,  0.5f, 0.0f, -0.5f,  0.5f, 1.0f, -0.5f,  0.5f, 1.0f, 0.5f,
            -0.5f, 0.0f, -0.5f, -0.5f, 0.0f, 0.5f, -0.5f, 1.0f, 0.5f, -0.5f, 1.0f, -0.5f,
            -0.5f, 0.0f, 0.5f,  0.5f, 0.0f, 0.5f,  0.5f, 1.0f, 0.5f, -0.5f, 1.0f, 0.5f,
            0.5f, 0.0f, -0.5f, -0.5f, 0.0f, -0.5f, -0.5f, 1.0f, -0.5f, 0.5f, 1.0f, -0.5f,
            -0.5f, 1.0f, 0.5f,  0.5f, 1.0f, 0.5f,  0.5f, 1.0f, -0.5f, -0.5f, 1.0f, -0.5f,
            -0.5f, 0.0f, -0.5f, 0.5f, 0.0f, -0.5f, 0.5f, 0.0f, 0.5f, -0.5f, 0.0f, 0.5f,
        };
        const float normals[] = {
            1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0,
            -1, 0, 0, -1, 0, 0, -1, 0, 0, -1, 0, 0,
            0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1,
            0, 0, -1, 0, 0, -1, 0, 0, -1, 0, 0, -1,
            0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0,
            0, -1, 0, 0, -1, 0, 0, -1, 0, 0, -1, 0,
        };
        const std::uint16_t indices[] = {
            0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7,
            8, 9, 10, 8, 10, 11, 12, 13, 14, 12, 14, 15,
            16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23,
        };
        glGenVertexArrays(1, &cube_vao);
        glBindVertexArray(cube_vao);
        glGenBuffers(1, &cube_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, cube_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(positions), positions, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glGenBuffers(1, &cube_normal_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, cube_normal_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(normals), normals, GL_STATIC_DRAW);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glGenBuffers(1, &cube_ibo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cube_ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
                     GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

        glGenBuffers(1, &instance_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, instance_vbo);
        glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_STATIC_DRAW);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Instance), nullptr);
        glVertexAttribDivisor(2, 1);
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Instance),
                              reinterpret_cast<const void*>(4 * sizeof(float)));
        glVertexAttribDivisor(3, 1);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
        error.clear();
        return true;
    }

    bool create_floor(std::string& error) {
        const float vertices[] = {
            -3.0f, 0.0f, -3.0f, 3.0f, 0.0f, -3.0f, 3.0f, 0.0f, 3.0f,
            -3.0f, 0.0f, -3.0f, 3.0f, 0.0f, 3.0f, -3.0f, 0.0f, 3.0f,
        };
        glGenVertexArrays(1, &floor_vao);
        glBindVertexArray(floor_vao);
        glGenBuffers(1, &floor_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, floor_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glBindVertexArray(0);
        error.clear();
        return true;
    }

    bool create_outline(const Geometry& geometry, std::string& error) {
        outline_count = static_cast<int>(geometry.outline.size());
        glGenVertexArrays(1, &outline_vao);
        glBindVertexArray(outline_vao);
        glGenBuffers(1, &outline_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, outline_vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(geometry.outline.size() * sizeof(Vec3)),
                     geometry.outline.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vec3), nullptr);
        glBindVertexArray(0);
        error.clear();
        return true;
    }

    bool create_instances(const Geometry& geometry, std::string& error) {
        std::vector<Instance> instances = geometry.plates;
        plate_count = static_cast<int>(instances.size());
        instances.insert(instances.end(), geometry.towers.begin(),
                         geometry.towers.end());
        total_instance_count = static_cast<int>(instances.size());
        glBindBuffer(GL_ARRAY_BUFFER, instance_vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(instances.size() * sizeof(Instance)),
                     instances.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        error.clear();
        return true;
    }

    bool create_particles(const MapShape& shape, std::string& error) {
        particle_count = kParticleCount;
        std::vector<float> data(static_cast<std::size_t>(particle_count) * 8 * 4);
        std::mt19937 rng(0xC0FFEEu);
        std::uniform_real_distribution<float> distribution(0.0f, 1.0f);
        auto& values = data;
        for (int i = 0; i < particle_count; ++i) {
            float x = 0.0f;
            float z = 0.0f;
            bool accepted = false;
            for (int attempt = 0; attempt < 128; ++attempt) {
                x = shape.min_x + (shape.max_x - shape.min_x) * distribution(rng);
                z = shape.min_z + (shape.max_z - shape.min_z) * distribution(rng);
                if (shape_contains(shape, x, z)) {
                    accepted = true;
                    break;
                }
            }
            if (!accepted) {
                x = 0.5f * (shape.min_x + shape.max_x);
                z = 0.5f * (shape.min_z + shape.max_z);
            }
            const float direction = (i % 2 == 0) ? 1.0f : -1.0f;
            const float speed = 0.16f + 0.30f * distribution(rng);
            const float vx = direction * speed;
            const float vz = (distribution(rng) - 0.5f) * 0.055f;
            const float seed = distribution(rng);
            const std::size_t base = static_cast<std::size_t>(i) * 8 * 4;
            values[base + 0] = x;
            values[base + 1] = 0.045f;
            values[base + 2] = z;
            values[base + 3] = 1.0f;
            values[base + 4] = vx;
            values[base + 5] = 0.0f;
            values[base + 6] = vz;
            values[base + 7] = seed;
            for (int trail = 0; trail < 6; ++trail) {
                const std::size_t offset = base + (2 + trail) * 4;
                values[offset + 0] = x - vx * (1.0f / 30.0f) * 1.5f * trail;
                values[offset + 1] = 0.045f + 0.002f * std::sin(seed * 6.0f + trail);
                values[offset + 2] = z - vz * (1.0f / 30.0f) * 1.5f * trail;
                values[offset + 3] = 1.0f;
            }
        }
        glGenBuffers(1, &particle_ssbo);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, particle_ssbo);
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     static_cast<GLsizeiptr>(data.size() * sizeof(float)),
                     data.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, particle_ssbo);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        glGenVertexArrays(1, &particle_vao);
        glBindVertexArray(particle_vao);
        glBindVertexArray(0);

        bounds_min_x = shape.min_x;
        bounds_max_x = shape.max_x;
        bounds_min_z = shape.min_z;
        bounds_max_z = shape.max_z;
        constexpr int kMaskSize = 128;
        std::vector<std::uint8_t> mask(
            static_cast<std::size_t>(kMaskSize) * kMaskSize, 0);
        for (int y = 0; y < kMaskSize; ++y) {
            const float z = shape.min_z +
                (y + 0.5f) / kMaskSize * (shape.max_z - shape.min_z);
            for (int x = 0; x < kMaskSize; ++x) {
                const float world_x = shape.min_x +
                    (x + 0.5f) / kMaskSize * (shape.max_x - shape.min_x);
                if (shape_contains(shape, world_x, z))
                    mask[static_cast<std::size_t>(y) * kMaskSize + x] = 255;
            }
        }
        glGenTextures(1, &particle_mask_texture);
        glBindTexture(GL_TEXTURE_2D, particle_mask_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kMaskSize, kMaskSize, 0,
                     GL_RED, GL_UNSIGNED_BYTE, mask.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        error.clear();
        return true;
    }

    void draw_floor(const Mat4& view_proj) {
        glUseProgram(floor_program);
        glUniformMatrix4fv(floor_view_proj, 1, GL_FALSE, view_proj.data());
        glBindVertexArray(floor_vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }

    void draw_outline(const Mat4& view_proj) {
        glUseProgram(line_program);
        glUniformMatrix4fv(line_view_proj, 1, GL_FALSE, view_proj.data());
        glUniform3f(line_color, 0.55f, 0.92f, 1.0f);
        glBindVertexArray(outline_vao);
        glLineWidth(1.0f);
        glDrawArrays(GL_LINES, 0, outline_count);
        glBindVertexArray(0);
    }

    void draw_scan_plane(const Mat4& view_proj, float scan_y_value) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);
        glUseProgram(scan_program);
        glUniformMatrix4fv(scan_view_proj, 1, GL_FALSE, view_proj.data());
        glUniform1f(scan_y, scan_y_value);
        glUniform4f(scan_color, 0.06f, 0.62f, 0.94f, 0.14f);
        glBindVertexArray(floor_vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    void draw_map(const Mat4& view_proj, float scan_height, bool with_towers) {
        glUseProgram(scene_program);
        glUniformMatrix4fv(scene_view_proj, 1, GL_FALSE, view_proj.data());
        glUniform1f(scene_scan_height, scan_height);
        glUniform1f(scene_max_height, max_height);
        glBindVertexArray(cube_vao);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cube_ibo);
        glDrawElementsInstanced(GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, nullptr,
                                with_towers ? total_instance_count : plate_count);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    void run_particles(float dt, float time) {
        glUseProgram(particle_compute_program);
        glUniform1i(glGetUniformLocation(particle_compute_program, "u_count"),
                    particle_count);
        glUniform1f(glGetUniformLocation(particle_compute_program, "u_dt"), dt);
        glUniform1f(glGetUniformLocation(particle_compute_program, "u_time"), time);
        glUniform4f(glGetUniformLocation(particle_compute_program, "u_bounds"),
                    -1.05f, 1.05f, -0.90f, 0.90f);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, particle_ssbo);
        glDispatchCompute(static_cast<GLuint>(particle_count / 256), 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT |
                        GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
    }

    void draw_particles(const Mat4& view_proj) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glDepthMask(GL_FALSE);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, particle_mask_texture);
        glBindVertexArray(particle_vao);
        glUseProgram(particle_line_program);
        glUniformMatrix4fv(particle_view_proj_line, 1, GL_FALSE, view_proj.data());
        glUniform4f(particle_bounds_line, bounds_min_x, bounds_min_z,
                    bounds_max_x, bounds_max_z);
        glDrawArrays(GL_LINES, 0, particle_count * 10);
        glUseProgram(particle_point_program);
        glUniformMatrix4fv(particle_view_proj_point, 1, GL_FALSE, view_proj.data());
        glUniform4f(particle_bounds_point, bounds_min_x, bounds_min_z,
                    bounds_max_x, bounds_max_z);
        glDrawArrays(GL_POINTS, 0, particle_count);
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    void read_bgr(std::vector<std::uint8_t>& bgr) {
        bgr.resize(static_cast<std::size_t>(width) * height * 3);
        std::vector<std::uint8_t> rgba(
            static_cast<std::size_t>(width) * height * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        for (int y = 0; y < height; ++y) {
            const int source_y = height - 1 - y;
            for (int x = 0; x < width; ++x) {
                const std::size_t source =
                    (static_cast<std::size_t>(source_y) * width + x) * 4;
                const std::size_t target =
                    (static_cast<std::size_t>(y) * width + x) * 3;
                bgr[target + 0] = rgba[source + 2];
                bgr[target + 1] = rgba[source + 1];
                bgr[target + 2] = rgba[source + 0];
            }
        }
    }

    bool render(const std::string& scene, const std::string& output,
                const std::string& summary_output, int frames, int fps,
                std::string& error) {
        std::vector<std::uint8_t> bgr;
        const std::string command = ffmpeg_command(output, fps);
        std::FILE* encoder = ::popen(command.c_str(), "w");
        if (encoder == nullptr) {
            error = "cannot launch ffmpeg/NVENC";
            return false;
        }
        std::vector<std::uint8_t> rgba;
        const auto start = std::chrono::steady_clock::now();
        for (int frame = 0; frame < frames; ++frame) {
            const float t = frames > 1
                ? static_cast<float>(frame) / static_cast<float>(frames - 1)
                : 0.0f;
            const float eased = t * t * (3.0f - 2.0f * t);
            const float yaw = -0.20f + 0.34f * eased;
            const bool particle_scene = scene == "particles";
            const float radius = particle_scene ? 2.25f : 2.85f;
            const float eye_height = particle_scene ? 2.00f : 1.62f;
            const Vec3 eye{std::sin(yaw) * radius, eye_height,
                           std::cos(yaw) * radius};
            const Vec3 center{0.0f, particle_scene ? 0.055f : 0.10f, 0.0f};
            const Mat4 view = look_at(eye, center, {0.0f, 1.0f, 0.0f});
            const Mat4 projection = perspective(
                34.0f * kPi / 180.0f,
                static_cast<float>(width) / static_cast<float>(height),
                0.08f, 40.0f);
            const Mat4 view_proj = multiply(projection, view);

            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glViewport(0, 0, width, height);
            glClearColor(0.003f, 0.006f, 0.013f, 1.0f);
            glClearDepthf(1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            glDisable(GL_CULL_FACE);

            draw_floor(view_proj);
            if (particle_scene) {
                draw_map(view_proj, 1.0f, false);
                run_particles(1.0f / static_cast<float>(fps),
                              static_cast<float>(frame) / static_cast<float>(fps));
                draw_particles(view_proj);
            } else {
                const float scan = 0.018f + eased * (max_height + 0.045f);
                draw_map(view_proj, scan, true);
                draw_scan_plane(view_proj, scan);
            }
            draw_outline(view_proj);
            glFinish();
            read_bgr(bgr);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            draw_hud(bgr, width, height, scene, frame, frames, t, gl_renderer);
            if (std::fwrite(bgr.data(), 1, bgr.size(), encoder) != bgr.size()) {
                ::pclose(encoder);
                error = "FFmpeg pipe closed while writing frames";
                return false;
            }
        }
        const int status = ::pclose(encoder);
        if (status != 0) {
            std::remove(output.c_str());
            error = "ffmpeg/NVENC exited with a non-zero status";
            return false;
        }
        const double wall = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start).count();
        nlohmann::json summary = {
            {"schema", "chronon.maps-gpu-prototype.v1"},
            {"scene", scene},
            {"renderer", gl_renderer},
            {"gles_version", egl.version},
            {"width", width},
            {"height", height},
            {"fps", fps},
            {"frames", frames},
            {"depth_attachment", "GL_DEPTH_COMPONENT24"},
            {"depth_test", "GL_LESS"},
            {"instanced_geometry", true},
            {"compute_shader", scene == "particles"},
            {"particle_count", scene == "particles" ? particle_count : 0},
            {"compute_workgroups", scene == "particles" ? particle_count / 256 : 0},
            {"scanline", scene != "particles"},
            {"encoder", "h264_nvenc/p6/cq15"},
            {"wall_s", wall},
        };
        std::ofstream summary_file(summary_output);
        if (!summary_file) {
            error = "cannot write GPU summary: " + summary_output;
            return false;
        }
        summary_file << summary.dump(2) << '\n';
        std::printf("GPU_RENDER_PASS output=%s scene=%s frames=%d renderer=%s\n",
                    output.c_str(), scene.c_str(), frames, gl_renderer.c_str());
        error.clear();
        return true;
    }

    std::string ffmpeg_command(const std::string& output, int fps) const {
        std::ostringstream command;
        command << "/usr/bin/ffmpeg -hide_banner -loglevel error -y"
                << " -f rawvideo -pix_fmt bgr24 -video_size " << width << "x"
                << height << " -framerate " << fps << " -i -"
                << " -c:v h264_nvenc -gpu 0 -preset p6 -tune hq"
                << " -rc vbr -cq 15 -b:v 0 -profile:v high -level:v 4.0"
                << " -refs 1 -g 48 -keyint_min 48 -sc_threshold 0 -bf 0"
                << " -flags +cgop -color_primaries bt709 -color_trc bt709"
                << " -colorspace bt709 -color_range tv -pix_fmt yuv420p"
                << " -movflags +faststart " << shell_quote(output);
        return command.str();
    }

    static std::string shell_quote(const std::string& value) {
        std::string out = "'";
        for (const char c : value) {
            if (c == '\'') out += "'\\''";
            else out += c;
        }
        out += "'";
        return out;
    }
};

// ── CLI ────────────────────────────────────────────────────────────────────

struct Options {
    std::string input = "plugins/maps/showcases/assets/jerusalem.geojson";
    std::string feature = "Jerusalem";
    std::string scene = "volumetric";
    std::string output;
    std::string summary;
    int width = 1920;
    int height = 1080;
    int fps = 30;
    int frames = 120;
    bool self_test = false;
};

void usage(const char* argv0) {
    std::fprintf(stderr,
        "usage: %s --scene volumetric|particles --output out.mp4\n"
        "       [--input geojson] [--feature name] [--width 1920]\n"
        "       [--height 1080] [--fps 30] [--frames 120]\n"
        "       [--summary-output out.json]\n"
        "       %s --self-test\n",
        argv0, argv0);
}

bool parse_options(int argc, char** argv, Options& options, std::string& error) {
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                error = std::string("missing value for ") + name;
                return {};
            }
            return argv[++i];
        };
        if (argument == "--self-test") {
            options.self_test = true;
        } else if (argument == "--input") {
            options.input = next("--input");
        } else if (argument == "--feature") {
            options.feature = next("--feature");
        } else if (argument == "--scene") {
            options.scene = next("--scene");
        } else if (argument == "--output") {
            options.output = next("--output");
        } else if (argument == "--summary-output") {
            options.summary = next("--summary-output");
        } else if (argument == "--width") {
            options.width = std::atoi(next("--width").c_str());
        } else if (argument == "--height") {
            options.height = std::atoi(next("--height").c_str());
        } else if (argument == "--fps") {
            options.fps = std::atoi(next("--fps").c_str());
        } else if (argument == "--frames") {
            options.frames = std::atoi(next("--frames").c_str());
        } else if (argument == "--help" || argument == "-h") {
            usage(argv[0]);
            std::exit(0);
        } else {
            error = "unknown argument: " + argument;
            return false;
        }
        if (!error.empty()) return false;
    }
    if (options.self_test) return true;
    if (options.scene != "volumetric" && options.scene != "particles") {
        error = "--scene must be volumetric or particles";
        return false;
    }
    if (options.output.empty()) {
        error = "--output is required for a render";
        return false;
    }
    if (options.width <= 0 || options.height <= 0 || options.fps <= 0 ||
        options.frames < 2) {
        error = "width, height and fps must be positive and frames must be >= 2";
        return false;
    }
    if (options.summary.empty()) options.summary = options.output + ".gpu.json";
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    std::string error;
    if (!parse_options(argc, argv, options, error)) {
        std::fprintf(stderr, "map-gpu: %s\n", error.c_str());
        usage(argv[0]);
        return 2;
    }

    if (options.self_test) {
        EglContext probe;
        if (!probe.init(error)) {
            std::fprintf(stderr, "GPU_SELF_TEST_SKIP %s\n", error.c_str());
            return 77;
        }
        GLint invocations = 0;
        glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &invocations);
        std::printf("GPU_SELF_TEST_PASS renderer=%s version=%s compute_invocations=%d\n",
                    probe.renderer.c_str(), probe.version.c_str(), invocations);
        return 0;
    }

    try {
        std::filesystem::path output_path(options.output);
        if (output_path.has_parent_path())
            std::filesystem::create_directories(output_path.parent_path());
        std::filesystem::path summary_path(options.summary);
        if (summary_path.has_parent_path())
            std::filesystem::create_directories(summary_path.parent_path());

        GpuRenderer renderer;
        renderer.width = options.width;
        renderer.height = options.height;
        if (!renderer.init(options.input, options.feature, error)) {
            std::fprintf(stderr, "map-gpu: %s\n", error.c_str());
            return 1;
        }
        if (!renderer.render(options.scene, options.output, options.summary,
                             options.frames, options.fps, error)) {
            std::fprintf(stderr, "map-gpu: %s\n", error.c_str());
            return 1;
        }
    } catch (const std::exception& exc) {
        std::fprintf(stderr, "map-gpu: %s\n", exc.what());
        return 1;
    }
    return 0;
}
