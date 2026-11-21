// Headless GPU map renderer: framebuffer, GL programs, geometry buffers, the
// particle SSBO, the readback/encode loop and the prototype summary. Split out
// of map_gpu_render.cpp; the EGL/GL helpers live in map_gpu_gl.cpp.

#include "map_gpu_renderer.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "map_gpu_hud.hpp"
#include "map_gpu_math.hpp"
#include "map_gpu_shaders.hpp"

namespace chrononplugin::maps::gpu_detail {

GpuRenderer::~GpuRenderer() { shutdown(); }

void GpuRenderer::shutdown() {
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

bool GpuRenderer::init(const std::string& shape_path, const std::string& feature,
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

bool GpuRenderer::create_framebuffer(std::string& error) {
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

bool GpuRenderer::create_programs(std::string& error) {
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

bool GpuRenderer::create_cube(std::string& error) {
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

bool GpuRenderer::create_floor(std::string& error) {
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

bool GpuRenderer::create_outline(const Geometry& geometry, std::string& error) {
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

bool GpuRenderer::create_instances(const Geometry& geometry, std::string& error) {
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

bool GpuRenderer::create_particles(const MapShape& shape, std::string& error) {
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

void GpuRenderer::draw_floor(const Mat4& view_proj) {
    glUseProgram(floor_program);
    glUniformMatrix4fv(floor_view_proj, 1, GL_FALSE, view_proj.data());
    glBindVertexArray(floor_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void GpuRenderer::draw_outline(const Mat4& view_proj) {
    glUseProgram(line_program);
    glUniformMatrix4fv(line_view_proj, 1, GL_FALSE, view_proj.data());
    glUniform3f(line_color, 0.55f, 0.92f, 1.0f);
    glBindVertexArray(outline_vao);
    glLineWidth(1.0f);
    glDrawArrays(GL_LINES, 0, outline_count);
    glBindVertexArray(0);
}

void GpuRenderer::draw_scan_plane(const Mat4& view_proj, float scan_y_value) {
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

void GpuRenderer::draw_map(const Mat4& view_proj, float scan_height, bool with_towers) {
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

void GpuRenderer::run_particles(float dt, float time) {
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

void GpuRenderer::draw_particles(const Mat4& view_proj) {
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

void GpuRenderer::read_bgr(std::vector<std::uint8_t>& bgr) {
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

bool GpuRenderer::render(const std::string& scene, const std::string& output,
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

std::string GpuRenderer::ffmpeg_command(const std::string& output, int fps) const {
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

std::string GpuRenderer::shell_quote(const std::string& value) {
    std::string out = "'";
    for (const char c : value) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}
}  // namespace chrononplugin::maps::gpu_detail
