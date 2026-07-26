#ifndef KART_CORE_KZ_REFERENCE_H
#define KART_CORE_KZ_REFERENCE_H

#include "core/units.h"

// Published KZ shifter kart performance figures.
//
// ADR-0014 traded a real circuit for a fictional one, which cost the project its
// lap-time ground truth. These numbers are the replacement: the physics is wrong
// if an instrumented run lands outside these ranges, and the M3b validation
// scenarios assert against exactly these constants.
//
// One definition, three consumers: the validation scenarios, the tuning UI, and
// the docs. Duplicating them into a test file is how they drift.
//
// Sources are secondary (manufacturer figures, CIK-FIA technical regulations,
// published test data) and the ranges are deliberately wide — they are a
// plausibility gate, not a fit target.

namespace kart::core::kz {

// CIK-FIA minimum mass, kart plus driver, in kilograms.
//
// **This is KZ2's figure, not KZ's.** The two classes differ here: KZ runs to
// 170 kg and KZ2 to 175 kg, driver included. Everything else in this file, and
// everything in ARCHITECTURE.md §6.3, describes a machine common to both — the
// same 125 cc six-speed engine, the same tires — and the classes are separated
// mainly by chassis and brake homologation, which this project does not model.
//
// 175 kg is kept, and the label corrected, because KZ2 is the class almost
// anyone racing a shifter kart is actually in: KZ proper is the international
// top tier. The 5 kg is not a rounding difference either — it is 2.9% of the
// vehicle, worth about 0.1 s over a 0-100 km/h run.
inline constexpr double MASS_WITH_DRIVER_KG = 175.0;

// Top speed is gearing dependent; this is the range a circuit-geared kart reaches.
inline constexpr double TOP_SPEED_MIN_KMH = 135.0;
inline constexpr double TOP_SPEED_MAX_KMH = 145.0;

inline constexpr double ZERO_TO_100_KMH_MIN_S = 3.0;
inline constexpr double ZERO_TO_100_KMH_MAX_S = 3.5;

// Lateral acceleration, in two bands, because they are two different quantities
// and reading one as the other cost this project two milestones. ADR-0034.
//
// This file used to carry a single pair labeled "steady-state skidpad" while
// `ARCHITECTURE.md` §6.4 labeled the same two numbers "peak lateral
// acceleration". Every measurement ever taken against them was sustained, and
// read as sustained the pair is not merely wrong but impossible: the kart tips
// about the line joining its outside contact patches at **2.4336 g turning left**
// (`chassis.h`'s `rollover_threshold_g`), so a sustained 2.5 g describes a kart
// already on two wheels. Nothing sustains more lateral acceleration than it tips
// at.
//
// A transient may exceed the tipping threshold freely, because tipping takes
// time — 2.5 g held for 0.2 s rolls this kart 0.4 degrees, and held for 2.6 s
// puts it over. That is the whole distinction, and it is why the peak band is
// allowed to sit above a number the sustained band must stay below.
//
// **The two bands are not equally well sourced, and the difference is stated
// rather than smoothed over.** The sustained ceiling is anchored on this kart's
// own geometry: 2.0 g sits 18% below its own tipping point, so every value in the
// band is reachable with margin. The sustained floor is weaker — it comes from a
// lab-measured, telemetry-validated kart that solves to 1.456 g steady state, a
// figure this project has reached only through a third-party re-encoding of a
// paywalled paper nobody here has read. It is a plausibility floor, not a
// measurement, which is exactly what the header comment above says these ranges
// are for.

// Steady-state skidpad, slicks, dry asphalt. What the constant-radius scenario
// measures, and the only band a sustained figure may be judged against.
inline constexpr double LATERAL_SUSTAINED_G_MIN = 1.5;
inline constexpr double LATERAL_SUSTAINED_G_MAX = 2.0;

// Transient peak — corner entry, a single apex, a datalogger's peak channel.
// Only a transient probe may be judged against this.
//
// Published kart figures cluster here partly for a measurement reason worth
// knowing: a steering-wheel-mounted logger sits roughly 0.6 m ahead of the center
// of mass, so its lateral channel reads `a_y + yaw_accel * x`, and 10 rad/s^2 of
// turn-in contributes 0.6 g the chassis never felt.
inline constexpr double LATERAL_PEAK_G_MIN = 2.0;
inline constexpr double LATERAL_PEAK_G_MAX = 2.5;

inline constexpr double BRAKING_G_MIN = 1.5;
inline constexpr double BRAKING_G_MAX = 2.0;

// 125cc two-stroke. The powerband is narrow enough that gear selection dominates
// how the kart is driven — see ARCHITECTURE.md §6.3.
inline constexpr double POWERBAND_MIN_RPM = 9000.0;
inline constexpr double POWERBAND_MAX_RPM = 14000.0;
inline constexpr double PEAK_POWER_RPM = 13000.0;
inline constexpr double PEAK_POWER_HP = 45.0;

inline constexpr int GEAR_COUNT = 6;

// Derived, so the conversion is exercised rather than hand-copied.
inline constexpr double TOP_SPEED_MAX_MS = kmh_to_ms(TOP_SPEED_MAX_KMH);

// Static vertical load on one tire, newtons, with the mass split evenly. The
// real split is nearer 42/58 front to rear on a KZ — the engine and the driver's
// hips are both behind the middle — so this is the load a tire curve is anchored
// to, not a load any particular tire actually carries. It exists so the tire
// model's nominal load and the class minimum mass cannot drift apart.
inline constexpr double STATIC_LOAD_PER_TIRE_N = MASS_WITH_DRIVER_KG * G / 4.0;

} // namespace kart::core::kz

#endif // KART_CORE_KZ_REFERENCE_H
