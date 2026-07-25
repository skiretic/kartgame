#include "doctest.h"

#include "core/units.h"
#include "core/vec3.h"

// `Vec3` is small enough that most of it is not worth testing, so these cases
// are aimed at the three things that are: the sign conventions, the zero-length
// guard, and `rotated`.
//
// `rotated` earns real coverage because the caster-jacking derivation in
// src/core/steering.h is built directly on it. If Rodrigues' formula is
// transcribed with the cross product the wrong way round, every rotation still
// has the right magnitude and every one of them goes the wrong way — which on a
// kart means the jacking lifts the wrong wheel, and the symptom is a kart that
// understeers in a way no amount of tire tuning fixes.

using namespace kart::core;

static void check_close(const Vec3 &measured, const Vec3 &expected, double epsilon = 1e-12) {
	CHECK(measured.x == doctest::Approx(expected.x).epsilon(epsilon));
	CHECK(measured.y == doctest::Approx(expected.y).epsilon(epsilon));
	CHECK(measured.z == doctest::Approx(expected.z).epsilon(epsilon));
}

TEST_CASE("the coordinate convention is Godot's") {
	// Stated as a test rather than only as a comment, because this is the fact
	// the whole solver is oriented around and CLAUDE.md records that getting it
	// wrong is invisible until the kart drives backwards.
	const Vec3 right(1.0, 0.0, 0.0);
	const Vec3 up(0.0, 1.0, 0.0);
	const Vec3 forward(0.0, 0.0, -1.0);

	// The basis (X, Y, Z) is right-handed, so X cross Y is Z. Godot's forward is
	// **-Z**, and that minus sign is the whole trap: the identities a reader
	// expects to hold between right, up and forward do not, and two of the three
	// come back negated.
	//
	//     right cross up      =  X cross Y  =  Z  = -forward
	//     up    cross forward =  Y cross -Z = -X  = -right
	//     forward cross right = -Z cross X  = -Y  = -up
	//
	// Written out because the first version of this test asserted the unnegated
	// forms, which is the same mistake as building a kart that drives backwards
	// and is caught here for a hundredth of the cost.
	check_close(right.cross(up), -forward);
	check_close(up.cross(forward), -right);
	check_close(forward.cross(right), -up);
}

TEST_CASE("dot and cross agree with the geometry") {
	const Vec3 a(1.0, 2.0, 3.0);
	const Vec3 b(-4.0, 5.0, 6.0);

	CHECK(a.dot(b) == doctest::Approx(-4.0 + 10.0 + 18.0));
	// A cross product is orthogonal to both of its inputs. Cheap, and it catches
	// a transposed term that a single hand-computed triple would not.
	CHECK(a.cross(b).dot(a) == doctest::Approx(0.0));
	CHECK(a.cross(b).dot(b) == doctest::Approx(0.0));
	// Anticommutative. This is the property a transposed cross product breaks
	// while still producing an orthogonal vector of the right magnitude.
	check_close(a.cross(b), -b.cross(a));
}

TEST_CASE("normalizing a zero vector gives zero, not a NaN") {
	// The case that matters at standstill: no slip velocity, no travel, no
	// direction. A NaN here enters the chassis state and never leaves.
	const Vec3 zero;
	CHECK(zero.normalized().length() == doctest::Approx(0.0));
	CHECK(zero.normalized().is_finite());

	const Vec3 unit = Vec3(3.0, 0.0, 4.0).normalized();
	CHECK(unit.length() == doctest::Approx(1.0));
	check_close(unit, Vec3(0.6, 0.0, 0.8));
}

TEST_CASE("projection and rejection split a vector without losing any of it") {
	const Vec3 axis = Vec3(1.0, 1.0, 0.0).normalized();
	const Vec3 v(2.0, -3.0, 5.0);

	check_close(v.project_onto_unit(axis) + v.reject_from_unit(axis), v);
	// The rejection is orthogonal to the axis by construction; assert it, because
	// the tire code relies on the rolling and lateral components being
	// independent and a sign slip here would couple them.
	CHECK(v.reject_from_unit(axis).dot(axis) == doctest::Approx(0.0));
}

TEST_CASE("rotation about an axis goes the right way") {
	// A quarter turn about +Y takes +Z to +X in a right-handed frame. This is the
	// case that pins the sign of the cross-product term in Rodrigues' formula,
	// and therefore the direction the jacking pushes.
	const Vec3 up(0.0, 1.0, 0.0);
	check_close(Vec3(0.0, 0.0, 1.0).rotated(up, PI * 0.5), Vec3(1.0, 0.0, 0.0));
	check_close(Vec3(1.0, 0.0, 0.0).rotated(up, PI * 0.5), Vec3(0.0, 0.0, -1.0));
}

TEST_CASE("rotation preserves length and leaves the axis alone") {
	const Vec3 axis = Vec3(0.2, 0.9, -0.35).normalized();
	const Vec3 v(1.5, -2.5, 0.75);

	for (int step = 0; step < 8; ++step) {
		const double angle = static_cast<double>(step) * 0.7;
		CHECK(v.rotated(axis, angle).length() == doctest::Approx(v.length()));
	}
	// A vector along the axis is unmoved by a rotation about it — the property
	// that makes a kingpin axis a kingpin axis.
	check_close((axis * 2.0).rotated(axis, 1.234), axis * 2.0);
}

TEST_CASE("a full turn is the identity and rotations compose") {
	const Vec3 axis = Vec3(-0.3, 0.4, 0.86).normalized();
	const Vec3 v(0.9, 1.7, -2.2);

	check_close(v.rotated(axis, 2.0 * PI), v, 1e-9);
	check_close(v.rotated(axis, 0.3).rotated(axis, 0.45), v.rotated(axis, 0.75), 1e-12);
	// And it is a rotation, not a rotation-and-a-reflection: reversing the angle
	// returns the original.
	check_close(v.rotated(axis, 1.1).rotated(axis, -1.1), v, 1e-12);
}
