#pragma once

// Private to the map_gpu tool: the headless GPU renderer (framebuffer, GL
// programs, geometry buffers, particle SSBO and the readback/encode loop).

#include "map_gpu_gl.hpp"
#include "map_gpu_geometry.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace chrononplugin::maps::gpu_detail {

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


    ~GpuRenderer();

    void shutdown();

    bool init(const std::string& shape_path, const std::string& feature,
              std::string& error);

    bool create_framebuffer(std::string& error);

    bool create_programs(std::string& error);

    bool create_cube(std::string& error);

    bool create_floor(std::string& error);

    bool create_outline(const Geometry& geometry, std::string& error);

    bool create_instances(const Geometry& geometry, std::string& error);

    bool create_particles(const MapShape& shape, std::string& error);

    void draw_floor(const Mat4& view_proj);

    void draw_outline(const Mat4& view_proj);

    void draw_scan_plane(const Mat4& view_proj, float scan_y_value);

    void draw_map(const Mat4& view_proj, float scan_height, bool with_towers);

    void run_particles(float dt, float time);

    void draw_particles(const Mat4& view_proj);

    void read_bgr(std::vector<std::uint8_t>& bgr);

    bool render(const std::string& scene, const std::string& output,
                const std::string& summary_output, int frames, int fps,
                std::string& error);

    std::string ffmpeg_command(const std::string& output, int fps) const;

    static std::string shell_quote(const std::string& value);
};

}  // namespace chrononplugin::maps::gpu_detail
