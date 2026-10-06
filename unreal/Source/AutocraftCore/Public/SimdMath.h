// Vectors and the simd functions GameCore uses, computed the way Apple's
// <simd/geometry.h> computes them for doubles (the "precise" variants that
// Swift gets without fast math), so the numbers match bit for bit when built
// with -ffp-contract=off.
#pragma once

#include <cmath>
#include <cstdint>

namespace ac {

/// `SIMD2<Double>`: a ground point (x, z) or any pair.
struct Vec2 {
    double x = 0, y = 0;

    constexpr Vec2() = default;
    constexpr Vec2(double x_, double y_) : x(x_), y(y_) {}
    /// `Vec2(repeating:)`.
    static constexpr Vec2 repeating(double v) { return Vec2(v, v); }
    static const Vec2 zero;

    constexpr double& operator[](int i) { return i == 0 ? x : y; }
    constexpr double operator[](int i) const { return i == 0 ? x : y; }

    constexpr Vec2 operator-() const { return Vec2(-x, -y); }
    constexpr Vec2& operator+=(Vec2 b) { x += b.x; y += b.y; return *this; }
    constexpr Vec2& operator-=(Vec2 b) { x -= b.x; y -= b.y; return *this; }
    constexpr Vec2& operator*=(Vec2 b) { x *= b.x; y *= b.y; return *this; }
    constexpr Vec2& operator/=(Vec2 b) { x /= b.x; y /= b.y; return *this; }
    constexpr Vec2& operator*=(double s) { x *= s; y *= s; return *this; }
    constexpr Vec2& operator/=(double s) { x /= s; y /= s; return *this; }
    constexpr bool operator==(const Vec2&) const = default;
};
inline constexpr Vec2 Vec2::zero{0, 0};

constexpr Vec2 operator+(Vec2 a, Vec2 b) { return Vec2(a.x + b.x, a.y + b.y); }
constexpr Vec2 operator-(Vec2 a, Vec2 b) { return Vec2(a.x - b.x, a.y - b.y); }
constexpr Vec2 operator*(Vec2 a, Vec2 b) { return Vec2(a.x * b.x, a.y * b.y); }
constexpr Vec2 operator/(Vec2 a, Vec2 b) { return Vec2(a.x / b.x, a.y / b.y); }
constexpr Vec2 operator*(Vec2 a, double s) { return Vec2(a.x * s, a.y * s); }
constexpr Vec2 operator*(double s, Vec2 a) { return Vec2(s * a.x, s * a.y); }
constexpr Vec2 operator/(Vec2 a, double s) { return Vec2(a.x / s, a.y / s); }
constexpr Vec2 operator+(Vec2 a, double s) { return Vec2(a.x + s, a.y + s); }
constexpr Vec2 operator-(Vec2 a, double s) { return Vec2(a.x - s, a.y - s); }

/// `SIMD3<Double>`: a world position (x, y up, z).
struct Vec3 {
    double x = 0, y = 0, z = 0;

    constexpr Vec3() = default;
    constexpr Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
    static constexpr Vec3 repeating(double v) { return Vec3(v, v, v); }
    static const Vec3 zero;

    constexpr double& operator[](int i) { return i == 0 ? x : i == 1 ? y : z; }
    constexpr double operator[](int i) const { return i == 0 ? x : i == 1 ? y : z; }

    constexpr Vec3 operator-() const { return Vec3(-x, -y, -z); }
    constexpr Vec3& operator+=(Vec3 b) { x += b.x; y += b.y; z += b.z; return *this; }
    constexpr Vec3& operator-=(Vec3 b) { x -= b.x; y -= b.y; z -= b.z; return *this; }
    constexpr Vec3& operator*=(double s) { x *= s; y *= s; z *= s; return *this; }
    constexpr Vec3& operator/=(double s) { x /= s; y /= s; z /= s; return *this; }
    constexpr bool operator==(const Vec3&) const = default;
};
inline constexpr Vec3 Vec3::zero{0, 0, 0};

constexpr Vec3 operator+(Vec3 a, Vec3 b) { return Vec3(a.x + b.x, a.y + b.y, a.z + b.z); }
constexpr Vec3 operator-(Vec3 a, Vec3 b) { return Vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
constexpr Vec3 operator*(Vec3 a, Vec3 b) { return Vec3(a.x * b.x, a.y * b.y, a.z * b.z); }
constexpr Vec3 operator/(Vec3 a, Vec3 b) { return Vec3(a.x / b.x, a.y / b.y, a.z / b.z); }
constexpr Vec3 operator*(Vec3 a, double s) { return Vec3(a.x * s, a.y * s, a.z * s); }
constexpr Vec3 operator*(double s, Vec3 a) { return Vec3(s * a.x, s * a.y, s * a.z); }
constexpr Vec3 operator/(Vec3 a, double s) { return Vec3(a.x / s, a.y / s, a.z / s); }

// simd: `simd_dot(x, y)` is `simd_reduce_add(x * y)`, the products first,
// then summed left to right.
inline double dot(Vec2 a, Vec2 b) {
    const double px = a.x * b.x, py = a.y * b.y;
    return px + py;
}
inline double dot(Vec3 a, Vec3 b) {
    const double px = a.x * b.x, py = a.y * b.y, pz = a.z * b.z;
    return px + py + pz;
}
inline double length_squared(Vec2 a) { return dot(a, a); }
inline double length_squared(Vec3 a) { return dot(a, a); }
/// `simd_precise_length`: `sqrt(length_squared)`.
inline double length(Vec2 a) { return std::sqrt(length_squared(a)); }
inline double length(Vec3 a) { return std::sqrt(length_squared(a)); }
inline double distance_squared(Vec2 a, Vec2 b) { return length_squared(a - b); }
inline double distance_squared(Vec3 a, Vec3 b) { return length_squared(a - b); }
inline double distance(Vec2 a, Vec2 b) { return length(a - b); }
inline double distance(Vec3 a, Vec3 b) { return length(a - b); }
/// `simd_precise_normalize`: `x * (1 / sqrt(length_squared))`.
inline Vec2 normalize(Vec2 a) { return a * (1 / std::sqrt(length_squared(a))); }
inline Vec3 normalize(Vec3 a) { return a * (1 / std::sqrt(length_squared(a))); }
/// Swift's `min` and `max` for numbers, exactly as the standard library
/// has them (`std::max` differs on equal values, so on -0 and +0):
/// `min(x, y)` is `y < x ? y : x`, `max(x, y)` is `y >= x ? y : x`.
template <class T> constexpr T min(T x, T y) { return y < x ? y : x; }
template <class T> constexpr T max(T x, T y) { return y >= x ? y : x; }
/// Swift's three-argument `min(x, y, z)` and `max(x, y, z)`.
template <class T> constexpr T min(T x, T y, T z) { return min(min(x, y), z); }
template <class T> constexpr T max(T x, T y, T z) { return max(max(x, y), z); }

/// Elementwise `simd_min`/`simd_max` (`fmin`/`fmax`).
inline Vec2 min(Vec2 a, Vec2 b) { return Vec2(std::fmin(a.x, b.x), std::fmin(a.y, b.y)); }
inline Vec2 max(Vec2 a, Vec2 b) { return Vec2(std::fmax(a.x, b.x), std::fmax(a.y, b.y)); }
inline Vec3 min(Vec3 a, Vec3 b) { return Vec3(std::fmin(a.x, b.x), std::fmin(a.y, b.y), std::fmin(a.z, b.z)); }
inline Vec3 max(Vec3 a, Vec3 b) { return Vec3(std::fmax(a.x, b.x), std::fmax(a.y, b.y), std::fmax(a.z, b.z)); }
/// `simd_clamp`: `simd_min(simd_max(x, lo), hi)`.
inline Vec2 clamp(Vec2 v, Vec2 lo, Vec2 hi) { return min(max(v, lo), hi); }
inline Vec3 clamp(Vec3 v, Vec3 lo, Vec3 hi) { return min(max(v, lo), hi); }
/// `simd_mix`: `x + t * (y - x)`.
inline Vec2 mix(Vec2 a, Vec2 b, Vec2 t) { return a + t * (b - a); }
inline Vec2 mix(Vec2 a, Vec2 b, double t) { return a + Vec2::repeating(t) * (b - a); }
inline Vec3 mix(Vec3 a, Vec3 b, Vec3 t) { return a + t * (b - a); }
inline Vec3 mix(Vec3 a, Vec3 b, double t) { return a + Vec3::repeating(t) * (b - a); }

/// The constant Swift calls `.pi` (named so as not to clash with Unreal's `PI`).
inline constexpr double pi = 3.141592653589793238462643383279502884;

/// Swift's `Double.rounded()`: to nearest, halves away from zero.
inline double rounded(double v) { return std::round(v); }

} // namespace ac
