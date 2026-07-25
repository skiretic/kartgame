#ifndef KART_CORE_CHASSIS_H
#define KART_CORE_CHASSIS_H

#include "core/kz_reference.h"
#include "core/vec3.h"

#include <cmath>

// Chassis mass properties. ROADMAP M3b, issue #30: "`RigidBody3D`, 175 kg with
// driver, inertia from a box approximation, CoM slightly rearward" — and, from
// the issue itself, "right-biased by the engine".
//
// Nothing in src/core/ may include godot-cpp. See docs/DECISIONS.md ADR-0017.
//
// ## Why a table of lumps rather than one box
//
// Issue #30 asks for three things: a mass, an inertia tensor from a box
// approximation, and a center of mass that is rearward and right-biased. Done
// literally — one box for the whole kart — the first two come out of the third,
// and the third has to be *asserted*. That is how `kart_debug_vehicle.gd` ended
// up carrying the sentence "an 80 kg kart carries its frame, engine and tires
// low, around 0.12 m, and a 95 kg driver reclined in the seat sits at about
// 0.32 m, so the pair land at 0.23 m". That reasoning is sound and it is prose:
// nothing checks it, and the number it produces cannot be interrogated.
//
// A table of lumps inverts it. Each part is a mass and a place, both of which
// can be argued with individually; the center of mass, the inertia tensor, the
// static axle split and the rollover threshold are then all *derived* from the
// same table and cannot disagree with each other. The right bias stops being a
// thing to remember to add and becomes a consequence of the engine being bolted
// where the engine is bolted.
//
// It also costs nothing at run time. The table is walked once at startup.
//
// ## Where the numbers come from, and which ones are guesses
//
// `ARCHITECTURE.md` §5 item 10 and CLAUDE.md forbid modeling a real part from
// memory, so the provenance of every mass is stated rather than implied. See
// docs/REFERENCES.md for the sources.
//
//   * **175 kg total** — CIK-FIA, and see the note in kz_reference.h: this is
//     the **KZ2** minimum. KZ proper is 170 kg.
//   * **20 kg engine** — a published figure for the TM KZ10 ES. A KZ engine is
//     a heavy thing for its size: 125 cc, but with a six-speed gearbox, a
//     clutch and a water jacket attached to it.
//   * **~90 kg complete kart without driver** — secondary sources agree on
//     roughly this for a KZ, and the table's non-driver lumps sum to 89.7 kg,
//     which is the check rather than the input.
//   * **Everything else is an estimate.** The individual lump masses — frame,
//     bodywork, axle assembly, wheels, fuel — are apportioned to reach that
//     90 kg total in proportions that are plausible for the parts. They are
//     labeled as estimates below and they are the first thing to correct if
//     better figures turn up. What they are *not* is arbitrary: the total and
//     the largest single item are both sourced, and the inertia tensor is far
//     less sensitive to how the remaining mass is divided than to where the
//     driver sits.
//
// ## The driver's position is calibrated, not measured, and here is why
//
// The driver is 78 kg — nearly half the kart — so the driver's seating position
// dominates the center of mass and therefore the static axle split. The obvious
// source for it is `tools/blender/kartlib/params.py`, which places the seat.
// That source is **known to be wrong**: issue #107 is open and says in its title
// that the seat, pedals and steering wheel do not fit an adult, and it is open
// precisely because the seat is the datum other geometry hangs off. Deriving the
// mass distribution from geometry the project has already flagged as incorrect
// would launder a known bug into a physics constant.
//
// So the driver's fore-aft position is instead solved for: the segment masses
// use standard anthropometric fractions (head 8%, trunk 50%, arms 10%,
// legs 32%), placed relative to the hips, and the whole assembly is shifted
// along the kart's axis until the static split matches the **published 42/58
// front-to-rear** figure that `kz_reference.h` already records. The solved shift
// is +58 mm rearward of the naive placement, which is small enough to be
// credible as "the geometry is roughly right and the seat is 58 mm out" and
// large enough to matter.
//
// When #107 is fixed, this calibration should be replaced by the real seat
// position and the resulting split reported — if it does not land near 42/58,
// one of the two is wrong and that is worth knowing.
//
// ## Ballast is modeled, because a real kart makes weight with lead
//
// The lumps sum to 167.7 kg. A KZ2 must weigh 175 kg with its driver aboard, so
// 7.3 kg of lead is bolted on, which is exactly what a team does at the scales.
// Modeling it explicitly rather than fattening the other numbers to fit has two
// payoffs: the kart and driver masses stay honest quantities that can be checked
// against a source, and ballast **position** becomes the tuning parameter it
// really is. It is placed on the left of the seat here, which is where a team
// puts it, for the reason in the next paragraph.
//
// ## The right bias is larger than it looks and it is not symmetric
//
// The engine, exhaust and radiator are all mounted outboard on the right and
// together weigh 27 kg — 15% of the kart, at about 320 mm off the centerline.
// That puts the center of mass 41 mm to the right of the middle even after the
// ballast is placed to the left to counter it, and the consequence is not
// cosmetic: the kart's rollover threshold differs by roughly 0.4 g between a
// left-hand corner and a right-hand one. A validation scenario that only ever
// turns one way measures one of those two karts. See the note in
// tools/verify/drive_probe.gd.

namespace kart::core {

// A single mass contribution: what it is, how heavy, where, and how big.
//
// `extent` is the full box dimensions used for the lump's own inertia about its
// own center. A zero extent is a point mass, which is a legitimate choice for
// something small and dense and a bad one for the frame.
struct MassLump {
	const char *name = "";
	double mass = 0.0; // kg
	Vec3 position; // chassis frame, meters
	Vec3 extent; // full box dimensions, meters; zero for a point mass
};

// A symmetric inertia tensor about the center of mass, kg m^2.
//
// Stored as six components rather than a 3x3 because it is symmetric by
// construction and a full matrix invites someone to write an asymmetric one.
struct InertiaTensor {
	double xx = 0.0;
	double yy = 0.0;
	double zz = 0.0;
	double xy = 0.0;
	double xz = 0.0;
	double yz = 0.0;

	// The largest off-diagonal term as a fraction of the largest diagonal one.
	//
	// This exists because Godot's `RigidBody3D.inertia` is a `Vector3`: the
	// engine accepts only the **diagonal**, and assumes the body's local axes are
	// its principal axes. They are not — the kart's mass is not symmetric about
	// its centerline — so handing Godot the diagonal is an approximation, and
	// this is its size. Measured rather than waved at: for the table below it is
	// 3.6%, which is small enough to accept and large enough that it should be
	// stated in the ADR rather than discovered later.
	double off_diagonal_fraction() const {
		double largest_off = std::fabs(xy);
		if (std::fabs(xz) > largest_off) {
			largest_off = std::fabs(xz);
		}
		if (std::fabs(yz) > largest_off) {
			largest_off = std::fabs(yz);
		}
		double largest_diagonal = xx;
		if (yy > largest_diagonal) {
			largest_diagonal = yy;
		}
		if (zz > largest_diagonal) {
			largest_diagonal = zz;
		}
		if (largest_diagonal <= 0.0) {
			return 0.0;
		}
		return largest_off / largest_diagonal;
	}
};

// What the solver and the Godot boundary both consume.
struct MassProperties {
	double mass = 0.0;
	Vec3 center_of_mass; // chassis frame
	InertiaTensor inertia; // about the center of mass, chassis axes
};

// Accumulate a table of lumps into mass properties.
//
// Two passes: the first finds the center of mass, the second sums each lump's
// own inertia plus its parallel-axis term about that center. It has to be two
// passes — the parallel-axis theorem is stated about the final center, and
// accumulating inertia about a moving origin gives a number that is wrong in a
// way that looks plausible.
//
// `count` rather than a container, and a raw array, because ARCHITECTURE.md §8
// rule 4 wants a stable iteration order and the simplest way to have one is to
// iterate an array in the order it was written.
inline MassProperties accumulate(const MassLump *lumps, int count) {
	MassProperties result;
	if (lumps == nullptr || count <= 0) {
		return result;
	}

	Vec3 moment;
	for (int index = 0; index < count; ++index) {
		result.mass += lumps[index].mass;
		moment += lumps[index].position * lumps[index].mass;
	}
	if (result.mass <= 0.0) {
		return result;
	}
	result.center_of_mass = moment / result.mass;

	for (int index = 0; index < count; ++index) {
		const MassLump &lump = lumps[index];
		const Vec3 offset = lump.position - result.center_of_mass;
		const double m = lump.mass;

		// The lump's own inertia, as a solid box about its own center.
		const Vec3 &e = lump.extent;
		result.inertia.xx += m * (e.y * e.y + e.z * e.z) / 12.0;
		result.inertia.yy += m * (e.x * e.x + e.z * e.z) / 12.0;
		result.inertia.zz += m * (e.x * e.x + e.y * e.y) / 12.0;

		// Parallel axis: I += m * (|d|^2 * identity - d (x) d).
		const double squared = offset.length_squared();
		result.inertia.xx += m * (squared - offset.x * offset.x);
		result.inertia.yy += m * (squared - offset.y * offset.y);
		result.inertia.zz += m * (squared - offset.z * offset.z);
		result.inertia.xy += m * (-offset.x * offset.y);
		result.inertia.xz += m * (-offset.x * offset.z);
		result.inertia.yz += m * (-offset.y * offset.z);
	}
	return result;
}

// The KZ2 kart this project simulates.
//
// **Frame and origin.** Godot's convention throughout — +X right, +Y up, -Z
// forward, so the nose is at negative Z and the rear axle at positive Z. The
// origin is on the ground, laterally centered, midway between the axles, which
// is where `tools/blender/kartlib/params.py` puts the exported mesh's origin;
// its comment says it chose that "because the chassis body's origin should sit
// near its center of mass", and this is the file that cashes that in.
//
// Lateral and vertical positions come from `params.py` wherever the part exists
// in the generated geometry, converted by Blender's `(x, y, z) -> (x, z, -y)`.
// Longitudinal positions for the driver are the calibrated ones described in the
// header comment.
namespace kz {

// Axle positions in the chassis frame, from the wheelbase. Negative Z is
// forward, so the front axle is the negative one.
inline constexpr double FRONT_AXLE_Z = -0.525;
inline constexpr double REAR_AXLE_Z = 0.525;

// Centerline half-tracks. **Not** half of `track_front`/`track_rear` in
// params.py — those are outside width and the centerline is one tire width
// narrower, which is open issue #119. 1.105 m and 1.185 m centerline.
inline constexpr double FRONT_HALF_TRACK = 0.5525;
inline constexpr double REAR_HALF_TRACK = 0.5925;

inline constexpr double FRONT_WHEEL_RADIUS = 0.140;
inline constexpr double REAR_WHEEL_RADIUS = 0.1475;

// Static front axle share, published for a KZ and recorded in kz_reference.h.
// The driver's seating position is solved to reproduce it — see the header.
inline constexpr double STATIC_FRONT_SHARE = 0.42;

// The table. Order is fixed and load bearing (ARCHITECTURE.md §8 rule 4).
//
// The `estimated` marker in each comment is not decoration: it separates the
// two masses that came from a source from the fourteen that were apportioned.
inline const MassLump KART_LUMPS[] = {
	// --- rolling chassis --------------------------------------------------
	// estimated. The tubes are 30 mm and 22 mm chrome-moly over about 1.8 m.
	{ "frame rails and tubes", 20.0, Vec3(0.000, 0.090, 0.000), Vec3(0.60, 0.16, 1.60) },
	// estimated. 4 mm aluminum, 560 x 760 mm, from params.py's tray block.
	{ "floor tray", 2.5, Vec3(0.000, 0.054, -0.020), Vec3(0.56, 0.01, 0.76) },
	// estimated. Nose, sidepods and rear plastics — 22.6% of the kart's surface
	// area per ROADMAP M2, and almost none of its mass.
	{ "bodywork nose and sidepods", 5.0, Vec3(0.000, 0.150, -0.100), Vec3(1.20, 0.20, 1.50) },
	// estimated. A fiberglass kart seat is genuinely this light.
	{ "seat", 2.0, Vec3(0.000, 0.175, 0.060), Vec3(0.33, 0.29, 0.30) },
	{ "steering column and wheel", 2.0, Vec3(0.000, 0.330, -0.400), Vec3(0.32, 0.40, 0.30) },
	{ "pedals and linkage", 1.8, Vec3(0.000, 0.100, -0.520), Vec3(0.22, 0.10, 0.16) },
	// estimated, and a moving target in the real thing: a full 8 L of premix is
	// 5.5 kg that is gone by the end of a long run. Modeled full, because a
	// validation scenario should measure the heavy case.
	{ "fuel, tank full", 5.5, Vec3(0.000, 0.150, -0.180), Vec3(0.22, 0.26, 0.20) },

	// --- powertrain, all of it outboard on the right ----------------------
	// **sourced**: 20 kg, TM KZ10 ES. Position from params.py's engine block.
	{ "engine", 20.0, Vec3(0.319, 0.150, 0.190), Vec3(0.23, 0.30, 0.26) },
	// estimated. An expansion chamber is thin steel and mostly air.
	{ "exhaust", 4.0, Vec3(0.330, 0.250, 0.050), Vec3(0.13, 0.13, 0.62) },
	// estimated, coolant included.
	{ "radiator and coolant", 3.0, Vec3(0.308, 0.320, 0.000), Vec3(0.04, 0.43, 0.27) },

	// --- unsprung, though on a kart nothing is truly sprung ----------------
	// estimated. A 50 mm solid axle 1.08 m long is most of this, plus the
	// sprocket, the brake disc and two hubs.
	{ "rear axle, sprocket, brake", 9.0, Vec3(0.000, REAR_WHEEL_RADIUS, REAR_AXLE_Z), Vec3(0.05, 0.05, 1.08) },
	{ "rear wheel left", 3.6, Vec3(-REAR_HALF_TRACK, REAR_WHEEL_RADIUS, REAR_AXLE_Z), Vec3(0.215, 0.295, 0.295) },
	{ "rear wheel right", 3.6, Vec3(REAR_HALF_TRACK, REAR_WHEEL_RADIUS, REAR_AXLE_Z), Vec3(0.215, 0.295, 0.295) },
	{ "front wheel left", 2.6, Vec3(-FRONT_HALF_TRACK, FRONT_WHEEL_RADIUS, FRONT_AXLE_Z), Vec3(0.135, 0.280, 0.280) },
	{ "front wheel right", 2.6, Vec3(FRONT_HALF_TRACK, FRONT_WHEEL_RADIUS, FRONT_AXLE_Z), Vec3(0.135, 0.280, 0.280) },
	{ "front spindles and hubs", 2.5, Vec3(0.000, FRONT_WHEEL_RADIUS, FRONT_AXLE_Z), Vec3(1.00, 0.08, 0.10) },

	// --- driver, 78 kg, segmented by anthropometric fractions --------------
	// Longitudinal positions include the +58 mm calibration described in the
	// header comment. Do not "correct" these against params.py's seat until
	// issue #107 is closed — that is the geometry they deliberately do not use.
	{ "driver head and helmet", 6.2, Vec3(0.000, 0.560, 0.128), Vec3(0.26, 0.26, 0.26) },
	{ "driver trunk", 39.0, Vec3(0.000, 0.330, 0.248), Vec3(0.40, 0.50, 0.30) },
	{ "driver arms", 7.8, Vec3(0.000, 0.420, -0.032), Vec3(0.45, 0.15, 0.40) },
	{ "driver legs", 25.0, Vec3(0.000, 0.170, -0.122), Vec3(0.34, 0.20, 0.60) },

	// --- and the lead that makes the difference up ------------------------
	// Placed left of center, which is what a team does to counter 27 kg of
	// powertrain hanging off the right. It does not counter much of it.
	{ "ballast, lead on the seat", 7.3, Vec3(-0.200, 0.200, 0.300), Vec3(0.12, 0.10, 0.10) },
};

inline constexpr int KART_LUMP_COUNT =
		static_cast<int>(sizeof(KART_LUMPS) / sizeof(KART_LUMPS[0]));

inline MassProperties kart_mass_properties() {
	return accumulate(KART_LUMPS, KART_LUMP_COUNT);
}

// Static vertical load on one tire, newtons, from the derived split.
//
// Supersedes `kz_reference.h`'s `STATIC_LOAD_PER_TIRE_N`, which divides the
// weight evenly by four and says in its own comment that it is "not a load any
// particular tire actually carries". That constant stays where it is because
// the tire curve is anchored to it; this is the real one.
inline double static_load_front_tire(const MassProperties &properties) {
	return properties.mass * G * STATIC_FRONT_SHARE * 0.5;
}

inline double static_load_rear_tire(const MassProperties &properties) {
	return properties.mass * G * (1.0 - STATIC_FRONT_SHARE) * 0.5;
}

// Lateral acceleration, in g, at which the kart tips rather than slides.
//
// The tipping axis is the line joining the **outside front** and **outside
// rear** contact patches, so the arm is the perpendicular distance from the
// center of mass to that line — not half the rear track, which is what
// ADR-0031 used. The front track is the narrower of the two, so interpolating
// between the half-tracks at the center of mass's longitudinal position gives a
// shorter arm than the rear half-track does.
//
// `turning_left` selects which pair of tires is loaded. The two answers differ,
// because the center of mass is 41 mm right of the centerline: a kart turning
// left rolls onto its right-hand tires with the mass already leaning that way.
inline double rollover_threshold_g(const MassProperties &properties, bool turning_left) {
	const double rear_share = 1.0 - STATIC_FRONT_SHARE;
	// Longitudinal interpolation: at the front axle the arm is the front
	// half-track, at the rear axle the rear half-track.
	const double arm_at_centerline =
			FRONT_HALF_TRACK + rear_share * (REAR_HALF_TRACK - FRONT_HALF_TRACK);
	// Turning left loads the right-hand tires, and a center of mass displaced to
	// the right is that much closer to them.
	const double lateral_offset =
			turning_left ? -properties.center_of_mass.x : properties.center_of_mass.x;
	const double arm = arm_at_centerline + lateral_offset;
	if (properties.center_of_mass.y <= 0.0) {
		return 0.0;
	}
	return arm / properties.center_of_mass.y;
}

} // namespace kz

} // namespace kart::core

#endif // KART_CORE_CHASSIS_H
