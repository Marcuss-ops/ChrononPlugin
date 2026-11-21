#pragma once

// Private to the map_gpu tool: the tiny GLSL-compatible math layer and the
// tool-wide constants shared by the renderer and the geometry builder.

#include <algorithm>

#include <array>
#include <cmath>

namespace chrononplugin::maps::gpu_detail {

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr int kParticleCount = 32768;  // 128 workgroups of 256

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

inline Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(const Vec3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline Vec3 normalize(const Vec3& v) {
    const float length = std::sqrt(std::max(1e-12f, dot(v, v)));
    return {v.x / length, v.y / length, v.z / length};
}

struct Mat4 {
    std::array<float, 16> m{};
    const float* data() const { return m.data(); }
};

inline Mat4 identity() {
    Mat4 result;
    result.m = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    return result;
}

inline Mat4 multiply(const Mat4& a, const Mat4& b) {
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

inline Mat4 perspective(float fov_y_radians, float aspect, float near_plane, float far_plane) {
    Mat4 result;
    const float f = 1.0f / std::tan(fov_y_radians * 0.5f);
    result.m[0] = f / aspect;
    result.m[5] = f;
    result.m[10] = (far_plane + near_plane) / (near_plane - far_plane);
    result.m[11] = -1.0f;
    result.m[14] = (2.0f * far_plane * near_plane) / (near_plane - far_plane);
    return result;
}

inline Mat4 look_at(const Vec3& eye, const Vec3& center, const Vec3& up) {
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

}  // namespace chrononplugin::maps::gpu_detail
