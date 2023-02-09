// z-pilot - shared math, RNG and noise primitives.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace zp {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTau = 6.28318530717958647692f;

inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float saturatef(float v) { return clampf(v, 0.0f, 1.0f); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float signf(float v) { return v < 0.0f ? -1.0f : 1.0f; }
inline float smoothstepf(float e0, float e1, float x) {
    float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
inline float wrapAngle(float a) {
    while (a > kPi) a -= kTau;
    while (a < -kPi) a += kTau;
    return a;
}
// Frame-rate independent approach factor for exponential smoothing.
inline float damp(float rate, float dt) { return 1.0f - expf(-rate * dt); }

struct Vec2 {
    float x = 0, y = 0;
    Vec2() = default;
    Vec2(float x_, float y_) : x(x_), y(y_) {}
    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2 operator/(float s) const { return {x / s, y / s}; }
    Vec2 operator-() const { return {-x, -y}; }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
};
inline float dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
inline float cross(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }
inline float length(const Vec2& a) { return sqrtf(a.x * a.x + a.y * a.y); }
inline float lengthSq(const Vec2& a) { return a.x * a.x + a.y * a.y; }
inline Vec2 normalize(const Vec2& a) {
    float l = length(a);
    return l > 1e-8f ? a / l : Vec2(0, 0);
}
inline Vec2 perp(const Vec2& a) { return {-a.y, a.x}; }
inline Vec2 rotate(const Vec2& v, float ang) {
    float c = cosf(ang), s = sinf(ang);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}
inline Vec2 fromAngle(float a) { return {cosf(a), sinf(a)}; }
inline Vec2 lerp(const Vec2& a, const Vec2& b, float t) { return a + (b - a) * t; }

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    explicit Vec3(float s) : x(s), y(s), z(s) {}
    Vec3(const Vec2& v, float z_) : x(v.x), y(v.y), z(z_) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator*(const Vec3& o) const { return {x * o.x, y * o.y, z * o.z}; }
    Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec2 xy() const { return {x, y}; }
};
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& a) { return sqrtf(dot(a, a)); }
inline Vec3 normalize(const Vec3& a) {
    float l = length(a);
    return l > 1e-8f ? a / l : Vec3(0, 0, 0);
}
inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }

struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    Vec4() = default;
    Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    Vec4(const Vec3& v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
    Vec3 xyz() const { return {x, y, z}; }
    Vec4 operator*(float s) const { return {x * s, y * s, z * s, w * s}; }
    Vec4 operator+(const Vec4& o) const { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
    Vec4 operator-(const Vec4& o) const { return {x - o.x, y - o.y, z - o.z, w - o.w}; }
};
inline Vec4 lerp(const Vec4& a, const Vec4& b, float t) { return a + (b - a) * t; }

// Row-major, column-vector convention: v' = M * v.
// Uploaded verbatim to HLSL cbuffers declared `row_major`.
struct Mat4 {
    float m[4][4] = {};

    static Mat4 identity() {
        Mat4 r;
        r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.0f;
        return r;
    }
    static Mat4 translation(const Vec3& t) {
        Mat4 r = identity();
        r.m[0][3] = t.x; r.m[1][3] = t.y; r.m[2][3] = t.z;
        return r;
    }
    static Mat4 scaling(const Vec3& s) {
        Mat4 r = identity();
        r.m[0][0] = s.x; r.m[1][1] = s.y; r.m[2][2] = s.z;
        return r;
    }
    static Mat4 rotationZ(float a) {
        Mat4 r = identity();
        float c = cosf(a), s = sinf(a);
        r.m[0][0] = c; r.m[0][1] = -s;
        r.m[1][0] = s; r.m[1][1] = c;
        return r;
    }
    static Mat4 rotationY(float a) {
        Mat4 r = identity();
        float c = cosf(a), s = sinf(a);
        r.m[0][0] = c; r.m[0][2] = s;
        r.m[2][0] = -s; r.m[2][2] = c;
        return r;
    }
    static Mat4 rotationX(float a) {
        Mat4 r = identity();
        float c = cosf(a), s = sinf(a);
        r.m[1][1] = c; r.m[1][2] = -s;
        r.m[2][1] = s; r.m[2][2] = c;
        return r;
    }
    static Mat4 perspectiveLH(float fovY, float aspect, float zn, float zf) {
        Mat4 r;
        float f = 1.0f / tanf(fovY * 0.5f);
        r.m[0][0] = f / aspect;
        r.m[1][1] = f;
        r.m[2][2] = zf / (zf - zn);
        r.m[2][3] = -zn * zf / (zf - zn);
        r.m[3][2] = 1.0f;
        return r;
    }
    static Mat4 lookAtLH(const Vec3& eye, const Vec3& at, const Vec3& up) {
        Vec3 z = normalize(at - eye);
        Vec3 x = normalize(cross(up, z));
        Vec3 y = cross(z, x);
        Mat4 r = identity();
        r.m[0][0] = x.x; r.m[0][1] = x.y; r.m[0][2] = x.z; r.m[0][3] = -dot(x, eye);
        r.m[1][0] = y.x; r.m[1][1] = y.y; r.m[1][2] = y.z; r.m[1][3] = -dot(y, eye);
        r.m[2][0] = z.x; r.m[2][1] = z.y; r.m[2][2] = z.z; r.m[2][3] = -dot(z, eye);
        return r;
    }
    // Maps pixel coordinates (0..w, 0..h, y down) to clip space.
    static Mat4 screenOrtho(float w, float h) {
        Mat4 r = identity();
        r.m[0][0] = 2.0f / w;  r.m[0][3] = -1.0f;
        r.m[1][1] = -2.0f / h; r.m[1][3] = 1.0f;
        r.m[2][2] = 1.0f;
        return r;
    }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a.m[i][k] * b.m[k][j];
            r.m[i][j] = s;
        }
    return r;
}
inline Vec3 transformPoint(const Mat4& m, const Vec3& v) {
    return {m.m[0][0] * v.x + m.m[0][1] * v.y + m.m[0][2] * v.z + m.m[0][3],
            m.m[1][0] * v.x + m.m[1][1] * v.y + m.m[1][2] * v.z + m.m[1][3],
            m.m[2][0] * v.x + m.m[2][1] * v.y + m.m[2][2] * v.z + m.m[2][3]};
}

// Quaternion helpers (xyzw) for spinning debris shards.
inline Vec4 quatFromAxisAngle(const Vec3& axis, float ang) {
    Vec3 a = normalize(axis);
    float s = sinf(ang * 0.5f);
    return {a.x * s, a.y * s, a.z * s, cosf(ang * 0.5f)};
}
inline Vec4 quatMul(const Vec4& a, const Vec4& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

struct Rng {
    uint32_t s = 0x9e3779b9u;
    explicit Rng(uint32_t seed = 12345u) { s = seed ? seed : 1u; }
    uint32_t next() {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return s;
    }
    float f01() { return (next() >> 8) * (1.0f / 16777216.0f); }
    float range(float a, float b) { return a + (b - a) * f01(); }
    int rangeI(int a, int b) { return a + (int)(next() % (uint32_t)(b - a + 1)); }
    float signed1() { return f01() * 2.0f - 1.0f; }
    Vec2 unitVec() { float a = f01() * kTau; return {cosf(a), sinf(a)}; }
    Vec2 inDisc() { return unitVec() * sqrtf(f01()); }
    // Cheap approximate normal distribution.
    float gauss() { return (f01() + f01() + f01() + f01() - 2.0f) * 1.2f; }
};

inline float hash2(int x, int y, uint32_t seed) {
    uint32_t h = (uint32_t)(x * 374761393) + (uint32_t)(y * 668265263) + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h >> 8) * (1.0f / 16777216.0f);
}

inline float valueNoise(float x, float y, uint32_t seed) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - xi, fy = y - yi;
    float ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
    float a = hash2(xi, yi, seed), b = hash2(xi + 1, yi, seed);
    float c = hash2(xi, yi + 1, seed), d = hash2(xi + 1, yi + 1, seed);
    return lerpf(lerpf(a, b, ux), lerpf(c, d, ux), uy) * 2.0f - 1.0f;
}

inline float fbm(float x, float y, int octaves, uint32_t seed) {
    float sum = 0, amp = 1, freq = 1, norm = 0;
    for (int i = 0; i < octaves; ++i) {
        sum += valueNoise(x * freq, y * freq, seed + i * 7919u) * amp;
        norm += amp;
        amp *= 0.5f;
        freq *= 2.03f;
    }
    return sum / norm;
}

inline Vec3 hsv(float h, float s, float v) {
    h = h - floorf(h);
    float r = fabsf(h * 6.0f - 3.0f) - 1.0f;
    float g = 2.0f - fabsf(h * 6.0f - 2.0f);
    float b = 2.0f - fabsf(h * 6.0f - 4.0f);
    return {v * lerpf(1.0f, saturatef(r), s), v * lerpf(1.0f, saturatef(g), s),
            v * lerpf(1.0f, saturatef(b), s)};
}

}  // namespace zp
