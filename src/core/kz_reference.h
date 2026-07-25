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

// CIK-FIA KZ class minimum mass, kart plus driver, in kilograms.
inline constexpr double MASS_WITH_DRIVER_KG = 175.0;

// Top speed is gearing dependent; this is the range a circuit-geared kart reaches.
inline constexpr double TOP_SPEED_MIN_KMH = 135.0;
inline constexpr double TOP_SPEED_MAX_KMH = 145.0;

inline constexpr double ZERO_TO_100_KMH_MIN_S = 3.0;
inline constexpr double ZERO_TO_100_KMH_MAX_S = 3.5;

// Steady-state skidpad, slicks, dry asphalt.
inline constexpr double LATERAL_G_MIN = 2.0;
inline constexpr double LATERAL_G_MAX = 2.5;

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
