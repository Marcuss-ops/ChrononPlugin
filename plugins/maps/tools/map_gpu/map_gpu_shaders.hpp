#pragma once

// Private to the map_gpu tool: the GLSL ES 3.2 sources compiled by the headless
// renderer. Kept out of the renderer implementation so shader edits do not
// touch the C++ pipeline.

namespace chrononplugin::maps::gpu_detail {

inline const char* const kSceneVS = R"GLSL(#version 310 es
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

inline const char* const kSceneFS = R"GLSL(#version 310 es
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

inline const char* const kFloorVS = R"GLSL(#version 310 es
precision highp float;
layout(location = 0) in vec3 a_pos;
uniform mat4 u_view_proj;
out vec3 v_world;
void main() {
    v_world = a_pos;
    gl_Position = u_view_proj * vec4(a_pos, 1.0);
}
)GLSL";

inline const char* const kFloorFS = R"GLSL(#version 310 es
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

inline const char* const kLineVS = R"GLSL(#version 310 es
precision highp float;
layout(location = 0) in vec3 a_pos;
uniform mat4 u_view_proj;
void main() {
    gl_Position = u_view_proj * vec4(a_pos, 1.0);
}
)GLSL";

inline const char* const kLineFS = R"GLSL(#version 310 es
precision highp float;
uniform vec3 u_color;
out vec4 out_color;
void main() { out_color = vec4(u_color, 1.0); }
)GLSL";

inline const char* const kScanVS = R"GLSL(#version 310 es
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

inline const char* const kScanFS = R"GLSL(#version 310 es
precision highp float;
uniform vec4 u_color;
out vec4 out_color;
void main() { out_color = u_color; }
)GLSL";

inline const char* const kParticleCompute = R"GLSL(#version 310 es
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

inline const char* const kParticleLineVS = R"GLSL(#version 310 es
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

inline const char* const kParticleLineFS = R"GLSL(#version 310 es
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

inline const char* const kParticlePointVS = R"GLSL(#version 310 es
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

inline const char* const kParticlePointFS = R"GLSL(#version 310 es
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

}  // namespace chrononplugin::maps::gpu_detail
