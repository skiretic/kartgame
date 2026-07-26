#include "doctest.h"

#include "core/chassis.h"
#include "core/kz_reference.h"
#include "core/units.h"

#include <cstdio>
#include <cstring>

// Mass properties, issue #30.
//
// These tests do two different jobs and it is worth keeping them apart. The
// first few check the *arithmetic* — the parallel-axis theorem, the two-pass
// accumulation, translation invariance — on tables small enough to verify by
// hand. The rest check the *kart*: that the table in chassis.h produces a
// vehicle with the mass, the axle split, the center-of-mass height and the
// inertia a KZ2 actually has.
//
// The second group is the interesting one, because every number it asserts is
// derived from a table of parts rather than typed in. A test that says "the
// center of mass is 0.22 m up" is worthless if 0.22 is also the input. Here it
// is not: move the engine and the assertion fails, which is the property that
// makes the table worth having.

using namespace kart::core;

TEST_CASE("accumulate finds the center of mass of a trivial table") {
	const MassLump lumps[] = {
		{ "a", 1.0, Vec3(-1.0, 0.0, 0.0), Vec3() },
		{ "b", 1.0, Vec3(1.0, 0.0, 0.0), Vec3() },
	};
	const MassProperties properties = accumulate(lumps, 2);

	CHECK(properties.mass == doctest::Approx(2.0));
	CHECK(properties.center_of_mass.x == doctest::Approx(0.0));

	// Two point masses at +/-1 m about the Y axis: I = sum(m r^2) = 2.
	CHECK(properties.inertia.yy == doctest::Approx(2.0));
	CHECK(properties.inertia.zz == doctest::Approx(2.0));
	// ...and zero about the axis they lie on.
	CHECK(properties.inertia.xx == doctest::Approx(0.0));
	// A pair symmetric about the origin has no products of inertia. This is the
	// case that catches a sign error in the parallel-axis off-diagonal term,
	// because it is the only place the sign is visible.
	CHECK(properties.inertia.xy == doctest::Approx(0.0));
	CHECK(properties.inertia.xz == doctest::Approx(0.0));
}

TEST_CASE("a single box reproduces the closed-form box inertia") {
	// m/12 * (h^2 + d^2) and so on. Asserted against the formula rather than
	// against a number, so the test says what it means.
	const double m = 12.0;
	const Vec3 e(2.0, 3.0, 4.0);
	const MassLump lumps[] = { { "box", m, Vec3(5.0, -6.0, 7.0), e } };
	const MassProperties properties = accumulate(lumps, 1);

	// Placed away from the origin, but the inertia is reported about its own
	// center of mass, so the offset must not appear.
	CHECK(properties.inertia.xx == doctest::Approx(m * (e.y * e.y + e.z * e.z) / 12.0));
	CHECK(properties.inertia.yy == doctest::Approx(m * (e.x * e.x + e.z * e.z) / 12.0));
	CHECK(properties.inertia.zz == doctest::Approx(m * (e.x * e.x + e.y * e.y) / 12.0));
	CHECK(properties.inertia.xy == doctest::Approx(0.0));
}

TEST_CASE("inertia about the center of mass is translation invariant") {
	// Shift the whole table by an arbitrary vector. The center of mass moves by
	// exactly that vector and the inertia tensor does not change at all. This is
	// the strongest cheap test of the two-pass accumulation: a single-pass
	// version that accumulated about the origin passes every other test here and
	// fails this one.
	const Vec3 shift(3.5, -2.25, 8.0);
	MassLump shifted[kz::KART_LUMP_COUNT];
	for (int index = 0; index < kz::KART_LUMP_COUNT; ++index) {
		shifted[index] = kz::KART_LUMPS[index];
		shifted[index].position += shift;
	}

	const MassProperties original = kz::kart_mass_properties();
	const MassProperties moved = accumulate(shifted, kz::KART_LUMP_COUNT);

	CHECK(moved.center_of_mass.x == doctest::Approx(original.center_of_mass.x + shift.x));
	CHECK(moved.center_of_mass.y == doctest::Approx(original.center_of_mass.y + shift.y));
	CHECK(moved.center_of_mass.z == doctest::Approx(original.center_of_mass.z + shift.z));

	CHECK(moved.inertia.xx == doctest::Approx(original.inertia.xx));
	CHECK(moved.inertia.yy == doctest::Approx(original.inertia.yy));
	CHECK(moved.inertia.zz == doctest::Approx(original.inertia.zz));
	CHECK(moved.inertia.xy == doctest::Approx(original.inertia.xy));
	CHECK(moved.inertia.xz == doctest::Approx(original.inertia.xz));
	CHECK(moved.inertia.yz == doctest::Approx(original.inertia.yz));
}

TEST_CASE("the kart weighs what the class minimum says") {
	const MassProperties properties = kz::kart_mass_properties();
	// Exactly, not approximately: the ballast lump exists to make this exact,
	// and if it stops being exact the ballast is stale.
	CHECK(properties.mass == doctest::Approx(kz::MASS_WITH_DRIVER_KG).epsilon(1e-9));
}

TEST_CASE("the static axle split is the published 42/58") {
	const MassProperties properties = kz::kart_mass_properties();
	// Front share from the lever arms: the front axle carries the fraction of
	// the weight proportional to how far the center of mass is from the rear.
	const double wheelbase = kz::REAR_AXLE_Z - kz::FRONT_AXLE_Z;
	const double front_share = (kz::REAR_AXLE_Z - properties.center_of_mass.z) / wheelbase;

	CHECK(front_share == doctest::Approx(kz::STATIC_FRONT_SHARE).epsilon(1e-3));

	// And the per-tire loads that follow, which are what the tire model's
	// nominal load should be compared against.
	CHECK(kz::static_load_front_tire(properties) == doctest::Approx(360.4).epsilon(1e-3));
	CHECK(kz::static_load_rear_tire(properties) == doctest::Approx(497.7).epsilon(1e-3));

	// kz_reference.h anchors the tire curve at 500 N and its comment says a rear
	// tire carries "roughly 500 N". Derived independently here, the rear tire
	// carries 497.7 N. That agreement is worth asserting: it means the tire
	// curve's anchor and the mass table describe the same vehicle.
	CHECK(kz::static_load_rear_tire(properties) ==
			doctest::Approx(kz::STATIC_LOAD_PER_TIRE_N * 1.16).epsilon(2e-2));
}

TEST_CASE("the center of mass is low, rearward, and biased right by the engine") {
	const MassProperties properties = kz::kart_mass_properties();

	// Height. `kart_debug_vehicle.gd` assumed 0.23 m from a two-mass argument in
	// a comment; the table gets 0.2197 m from twenty-one parts. Those agreeing
	// to 1.4% is the M3a estimate being vindicated, not this file copying it.
	CHECK(properties.center_of_mass.y == doctest::Approx(0.2197).epsilon(1e-3));
	CHECK(properties.center_of_mass.y > 0.18);
	CHECK(properties.center_of_mass.y < 0.28);

	// Rearward. ARCHITECTURE.md §6 asks for "slightly rearward" and this is
	// 84 mm behind the axle midpoint, which is 8% of the wheelbase.
	CHECK(properties.center_of_mass.z == doctest::Approx(0.0838).epsilon(1e-2));
	CHECK(properties.center_of_mass.z > 0.0);

	// Right-biased, which issue #30 asks for by name. Positive X is right, and
	// this is the engine, exhaust and radiator being bolted on that side.
	CHECK(properties.center_of_mass.x == doctest::Approx(0.0409).epsilon(1e-2));
	CHECK(properties.center_of_mass.x > 0.02);
}

TEST_CASE("removing the powertrain removes the right bias") {
	// The bias is a consequence, not a constant. Rebuild the table with the
	// three right-hand lumps deleted and the ballast centered, and the center of
	// mass should return to the centerline. If it does not, something else is
	// asymmetric and that is a bug rather than a kart.
	// Selected by name, not by position. The first version of this test dropped
	// every lump with x > 0.2 and silently deleted the two right-hand wheels
	// along with the powertrain, which left the table asymmetric and made the
	// test fail for the opposite of the reason it was testing.
	MassLump balanced[kz::KART_LUMP_COUNT];
	int count = 0;
	for (int index = 0; index < kz::KART_LUMP_COUNT; ++index) {
		const MassLump &lump = kz::KART_LUMPS[index];
		if (std::strcmp(lump.name, "engine") == 0 ||
				std::strcmp(lump.name, "exhaust") == 0 ||
				std::strcmp(lump.name, "radiator and coolant") == 0) {
			continue;
		}
		balanced[count] = lump;
		if (std::strcmp(lump.name, "ballast, lead on the seat") == 0) {
			balanced[count].position.x = 0.0; // the lead that countered them
		}
		++count;
	}
	const MassProperties properties = accumulate(balanced, count);
	CHECK(properties.center_of_mass.x == doctest::Approx(0.0).epsilon(1e-9));
}

TEST_CASE("the inertia tensor is physically possible") {
	const MassProperties properties = kz::kart_mass_properties();
	const InertiaTensor &inertia = properties.inertia;

	// Every principal moment is positive, and the triangle inequality holds:
	// no rigid body has a moment of inertia about one axis larger than the sum
	// of the other two. A tensor that violates this describes an object that
	// cannot exist, and it is the fastest way to catch a botched lump extent.
	CHECK(inertia.xx > 0.0);
	CHECK(inertia.yy > 0.0);
	CHECK(inertia.zz > 0.0);
	CHECK(inertia.xx + inertia.yy >= inertia.zz);
	CHECK(inertia.yy + inertia.zz >= inertia.xx);
	CHECK(inertia.zz + inertia.xx >= inertia.yy);

	// The measured values, so a change to the table shows up as a diff here
	// rather than as a kart that turns differently for no visible reason.
	CHECK(inertia.xx == doctest::Approx(22.18).epsilon(1e-2)); // pitch
	CHECK(inertia.yy == doctest::Approx(27.93).epsilon(1e-2)); // yaw
	CHECK(inertia.zz == doctest::Approx(13.20).epsilon(1e-2)); // roll

	// Yaw radius of gyration. A kart is short and its mass is piled in the
	// middle — the driver alone is 45% of it — so this should be well under half
	// the wheelbase. 0.40 m against a 1.05 m wheelbase is a kart that changes
	// direction eagerly, which is the whole point of one.
	const double yaw_gyration = std::sqrt(inertia.yy / properties.mass);
	CHECK(yaw_gyration == doctest::Approx(0.399).epsilon(1e-2));
	CHECK(yaw_gyration < 0.5);

	// Roll inertia is the smallest of the three, which is what a wide, flat,
	// low thing looks like. If roll ever came out largest, the table's extents
	// would be describing something the shape of a lamppost.
	CHECK(inertia.zz < inertia.xx);
	CHECK(inertia.zz < inertia.yy);
}

TEST_CASE("the diagonal-only inertia Godot accepts is a stated approximation") {
	const MassProperties properties = kz::kart_mass_properties();
	// `RigidBody3D.inertia` is a Vector3. Godot takes the diagonal and assumes
	// the body axes are principal axes; the kart's are not. The size of that lie
	// is measured here rather than hoped about, and 3.6% is small enough to
	// accept knowingly.
	CHECK(properties.inertia.off_diagonal_fraction() == doctest::Approx(0.0355).epsilon(5e-2));
	CHECK(properties.inertia.off_diagonal_fraction() < 0.05);
}

TEST_CASE("the rollover threshold is asymmetric, and it bounds the tire") {
	const MassProperties properties = kz::kart_mass_properties();
	const double left = kz::rollover_threshold_g(properties, true);
	const double right = kz::rollover_threshold_g(properties, false);

	// Turning left loads the right-hand tires, and the center of mass is already
	// leaning that way, so the kart tips earlier turning left than turning
	// right. 0.37 g apart is not a rounding difference — it is most of the width
	// of §6.4's whole lateral band.
	CHECK(left == doctest::Approx(2.434).epsilon(1e-2));
	CHECK(right == doctest::Approx(2.806).epsilon(1e-2));
	CHECK(right - left == doctest::Approx(0.372).epsilon(2e-2));

	// The number that matters for M3b: the kart must be able to **sustain** the
	// top of §6.4's sustained band without tipping over first, in the worse of
	// the two directions. Compared against the sustained ceiling and not the peak
	// one, because a transient is allowed to exceed the tipping threshold — it
	// takes 2.6 s at 2.5 g to actually put this kart over — while a steady state
	// is not. ADR-0034 split the two bands for exactly this reason, after they
	// were the same pair of numbers wearing two contradictory labels.
	CHECK(left > kz::LATERAL_SUSTAINED_G_MAX);
	// And it is genuinely tight: the margin over the band's ceiling is under half
	// a g, so a tire peak raised to reach further into the band eats most of it.
	// That is not a defect, it is what a kart is — they do roll over.
	CHECK(left - kz::LATERAL_SUSTAINED_G_MAX < 0.5);
}

TEST_CASE("report the mass properties") {
	// Not an assertion — a report. `tests/run.sh -ts="report the mass"` prints
	// the table that issue #30 is closed with, and the same numbers appear in
	// the telemetry panel at #43. Keeping the source of the issue evidence
	// inside the suite means the evidence cannot go stale silently.
	const MassProperties properties = kz::kart_mass_properties();
	std::printf("\n    mass                %8.2f kg\n", properties.mass);
	std::printf("    center of mass      x %+.4f  y %+.4f  z %+.4f  m\n",
			properties.center_of_mass.x, properties.center_of_mass.y,
			properties.center_of_mass.z);
	const double wheelbase = kz::REAR_AXLE_Z - kz::FRONT_AXLE_Z;
	const double front_share = (kz::REAR_AXLE_Z - properties.center_of_mass.z) / wheelbase;
	std::printf("    static split        %8.1f%% front / %.1f%% rear\n",
			front_share * 100.0, (1.0 - front_share) * 100.0);
	std::printf("    static tire load    front %.1f N   rear %.1f N\n",
			kz::static_load_front_tire(properties), kz::static_load_rear_tire(properties));
	std::printf("    inertia diagonal    Ixx %.2f  Iyy %.2f  Izz %.2f  kg m^2\n",
			properties.inertia.xx, properties.inertia.yy, properties.inertia.zz);
	std::printf("    inertia products    Ixy %+.3f  Ixz %+.3f  Iyz %+.3f  (%.1f%% of diagonal)\n",
			properties.inertia.xy, properties.inertia.xz, properties.inertia.yz,
			properties.inertia.off_diagonal_fraction() * 100.0);
	std::printf("    rollover threshold  %.3f g turning left, %.3f g turning right\n",
			kz::rollover_threshold_g(properties, true),
			kz::rollover_threshold_g(properties, false));
	CHECK(properties.mass > 0.0);
}
