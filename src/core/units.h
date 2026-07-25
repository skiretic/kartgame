#ifndef KART_CORE_UNITS_H
#define KART_CORE_UNITS_H

// Engine-free unit conversions.
//
// Nothing in src/core/ may include godot-cpp. See docs/DECISIONS.md ADR-0017.
// The simulation works in SI throughout — meters, seconds, kilograms, radians.
// Conversions live here so that "is this value km/h or m/s?" is answered in one
// place instead of at every call site.

namespace kart::core {

inline constexpr double KMH_PER_MS = 3.6;
inline constexpr double PI = 3.14159265358979323846;

// Standard gravity, m/s². Lateral and braking loads are reported in multiples of
// this because the KZ reference figures (ARCHITECTURE.md §6.4) are quoted in g.
inline constexpr double G = 9.80665;

constexpr double kmh_to_ms(double kmh) {
	return kmh / KMH_PER_MS;
}

constexpr double ms_to_kmh(double ms) {
	return ms * KMH_PER_MS;
}

// Engine speed. The drivetrain solver works in rad/s; humans and dashboards
// work in rpm.
constexpr double rpm_to_rads(double rpm) {
	return rpm * (2.0 * PI) / 60.0;
}

constexpr double rads_to_rpm(double rads) {
	return rads * 60.0 / (2.0 * PI);
}

constexpr double ms2_to_g(double accel_ms2) {
	return accel_ms2 / G;
}

constexpr double g_to_ms2(double accel_g) {
	return accel_g * G;
}

} // namespace kart::core

#endif // KART_CORE_UNITS_H
