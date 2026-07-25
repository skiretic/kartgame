#ifndef KART_CORE_VEC3_H
#define KART_CORE_VEC3_H

#include <cmath>

// The three-vector the vehicle solver is written in.
//
// Nothing in src/core/ may include godot-cpp. See docs/DECISIONS.md ADR-0017 —
// which is the whole reason this file exists rather than the solver simply using
// `godot::Vector3`. A vehicle solver that reaches for the engine's vector type
// cannot be unit tested without the engine, and every curve, every geometric
// derivation and every mode decomposition in M3b is worth more as a test than as
// a thing that looks right while driving.
//
// ## Why double and not float
//
// `godot::Vector3` is single precision in a standard Godot build, and the
// boundary code converts. That is deliberate. ADR-0032 measured 9.999998 where
// 10.0 was analytically correct after 120 single-precision additions, which is
// fine for the body state Godot owns and not fine inside a solver that
// substeps at 240 Hz and differences positions to recover slip. The core works
// in double, converts once at the boundary, and `state_hash.h` quantizes before
// hashing so the conversion cannot make two runs disagree.
//
// ## Coordinate convention
//
// **Godot's, not Blender's**, because this is the frame the solver is handed
// and a second mapping is a second place to get it wrong:
//
//     +X right, +Y up, -Z forward
//
// So the kart's nose points along `-Z`, its roll axis is `Z`, its pitch axis is
// `X`, and its yaw axis is `Y`. CLAUDE.md documents the Blender side of the
// mapping — `export_yup` sends Blender's `(x, y, z)` to `(x, z, -y)` — and that
// conversion happens in the exporter, long before anything here sees a number.
//
// ## What is deliberately not here
//
// No quaternion, no 4x4, no transform. The solver receives the chassis basis
// from the engine as three axis vectors and works in whichever frame each
// quantity is naturally expressed in, converting explicitly. A transform type
// invites code that has stopped saying which frame it is in, and "which frame is
// this in" is the question that produced two of M2's orientation bugs.

namespace kart::core {

struct Vec3 {
	double x = 0.0;
	double y = 0.0;
	double z = 0.0;

	constexpr Vec3() = default;
	constexpr Vec3(double p_x, double p_y, double p_z) :
			x(p_x), y(p_y), z(p_z) {}

	constexpr Vec3 operator+(const Vec3 &other) const {
		return Vec3(x + other.x, y + other.y, z + other.z);
	}
	constexpr Vec3 operator-(const Vec3 &other) const {
		return Vec3(x - other.x, y - other.y, z - other.z);
	}
	constexpr Vec3 operator-() const { return Vec3(-x, -y, -z); }
	constexpr Vec3 operator*(double scalar) const {
		return Vec3(x * scalar, y * scalar, z * scalar);
	}
	constexpr Vec3 operator/(double scalar) const {
		return Vec3(x / scalar, y / scalar, z / scalar);
	}

	constexpr Vec3 &operator+=(const Vec3 &other) {
		x += other.x;
		y += other.y;
		z += other.z;
		return *this;
	}
	constexpr Vec3 &operator-=(const Vec3 &other) {
		x -= other.x;
		y -= other.y;
		z -= other.z;
		return *this;
	}
	constexpr Vec3 &operator*=(double scalar) {
		x *= scalar;
		y *= scalar;
		z *= scalar;
		return *this;
	}

	constexpr double dot(const Vec3 &other) const {
		return x * other.x + y * other.y + z * other.z;
	}

	constexpr Vec3 cross(const Vec3 &other) const {
		return Vec3(
				y * other.z - z * other.y,
				z * other.x - x * other.z,
				x * other.y - y * other.x);
	}

	constexpr double length_squared() const { return x * x + y * y + z * z; }

	double length() const { return std::sqrt(length_squared()); }

	// Unit vector, or zero for a zero-length input.
	//
	// Returning zero rather than a NaN is a decision, not an oversight. A tire
	// with no slip velocity, a suspension ray with no travel and a kart at rest
	// all produce zero-length vectors every tick at standstill, and a NaN
	// entering the solver there would propagate into the chassis state and never
	// leave. Zero is the physically correct answer in each of those cases: no
	// direction, therefore no force along it.
	Vec3 normalized() const {
		const double squared = length_squared();
		if (squared <= 0.0) {
			return Vec3();
		}
		return *this / std::sqrt(squared);
	}

	// The component of this vector along a **unit** axis, and the remainder.
	//
	// Used constantly by the tire code, which decomposes a contact velocity into
	// the wheel's rolling direction and its axle direction. Kept as a pair
	// because doing it as two separate calls computes the same dot product twice
	// and, more importantly, invites the two halves to drift onto different
	// axes.
	constexpr Vec3 project_onto_unit(const Vec3 &unit_axis) const {
		return unit_axis * dot(unit_axis);
	}
	constexpr Vec3 reject_from_unit(const Vec3 &unit_axis) const {
		return *this - unit_axis * dot(unit_axis);
	}

	// Rotation about an arbitrary **unit** axis, by Rodrigues' formula.
	//
	// This is the operation the caster-jacking derivation in ARCHITECTURE.md §6
	// is built on. A kart's kingpin is neither vertical nor in the plane of the
	// chassis — it is inclined by the caster angle in side view and by the
	// kingpin inclination in front view — and steering rotates the wheel about
	// *that* axis, not about a vertical one. The vertical displacement of the
	// contact patch that falls out of the rotation is the jacking, and deriving
	// it this way means the number comes from the geometry rather than from a
	// remembered closed form. See src/core/steering.h.
	Vec3 rotated(const Vec3 &unit_axis, double angle) const {
		const double c = std::cos(angle);
		const double s = std::sin(angle);
		return *this * c + unit_axis.cross(*this) * s +
				unit_axis * (unit_axis.dot(*this) * (1.0 - c));
	}

	bool is_finite() const {
		return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
	}
};

constexpr Vec3 operator*(double scalar, const Vec3 &vector) {
	return vector * scalar;
}

} // namespace kart::core

#endif // KART_CORE_VEC3_H
