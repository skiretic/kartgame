#include "kart_core.h"

#include "core/kz_reference.h"
#include "core/units.h"
#include "version.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace kartgame {

void KartCore::_bind_methods() {
	// Static binds, so GDScript calls KartCore.build_info() with no instance.
	// KartCore is registered abstract, so there is no instance to be had.
	ClassDB::bind_static_method("KartCore", D_METHOD("build_info"), &KartCore::build_info);
	ClassDB::bind_static_method("KartCore", D_METHOD("kz_reference"), &KartCore::kz_reference);

	ClassDB::bind_static_method("KartCore", D_METHOD("kmh_to_ms", "kmh"), &KartCore::kmh_to_ms);
	ClassDB::bind_static_method("KartCore", D_METHOD("ms_to_kmh", "ms"), &KartCore::ms_to_kmh);
	ClassDB::bind_static_method("KartCore", D_METHOD("rpm_to_rads", "rpm"), &KartCore::rpm_to_rads);
	ClassDB::bind_static_method("KartCore", D_METHOD("rads_to_rpm", "rads"), &KartCore::rads_to_rpm);
}

Dictionary KartCore::build_info() {
	Dictionary info;

	info["extension_version"] = KARTGAME_VERSION;

	// The API level godot-cpp generated its bindings from. If this disagrees with
	// the running engine by more than a patch release, expect trouble.
	info["godot_api_version"] = KARTGAME_STRINGIFY(GODOT_VERSION_MAJOR) "." KARTGAME_STRINGIFY(GODOT_VERSION_MINOR) "." KARTGAME_STRINGIFY(GODOT_VERSION_PATCH);

	// Set by SConstruct from godot-cpp's own build variables, so these always
	// describe the file the engine actually loaded.
	info["build_target"] = KARTGAME_STRINGIFY(KARTGAME_BUILD_TARGET);
	info["build_platform"] = KARTGAME_STRINGIFY(KARTGAME_BUILD_PLATFORM);
	info["build_arch"] = KARTGAME_STRINGIFY(KARTGAME_BUILD_ARCH);

	// Determinism (ADR-0005) is only meaningful for a known float width. Godot and
	// godot-cpp must agree here or every Variant marshalled across the boundary is
	// the wrong size.
#ifdef REAL_T_IS_DOUBLE
	info["float_precision"] = "double";
#else
	info["float_precision"] = "single";
#endif

	return info;
}

Dictionary KartCore::kz_reference() {
	namespace kz = kart::core::kz;

	Dictionary ref;
	ref["mass_with_driver_kg"] = kz::MASS_WITH_DRIVER_KG;
	ref["top_speed_min_kmh"] = kz::TOP_SPEED_MIN_KMH;
	ref["top_speed_max_kmh"] = kz::TOP_SPEED_MAX_KMH;
	ref["zero_to_100_kmh_min_s"] = kz::ZERO_TO_100_KMH_MIN_S;
	ref["zero_to_100_kmh_max_s"] = kz::ZERO_TO_100_KMH_MAX_S;
	ref["lateral_g_min"] = kz::LATERAL_G_MIN;
	ref["lateral_g_max"] = kz::LATERAL_G_MAX;
	ref["braking_g_min"] = kz::BRAKING_G_MIN;
	ref["braking_g_max"] = kz::BRAKING_G_MAX;
	ref["powerband_min_rpm"] = kz::POWERBAND_MIN_RPM;
	ref["powerband_max_rpm"] = kz::POWERBAND_MAX_RPM;
	ref["peak_power_rpm"] = kz::PEAK_POWER_RPM;
	ref["peak_power_hp"] = kz::PEAK_POWER_HP;
	ref["gear_count"] = kz::GEAR_COUNT;
	return ref;
}

double KartCore::kmh_to_ms(double p_kmh) {
	return kart::core::kmh_to_ms(p_kmh);
}

double KartCore::ms_to_kmh(double p_ms) {
	return kart::core::ms_to_kmh(p_ms);
}

double KartCore::rpm_to_rads(double p_rpm) {
	return kart::core::rpm_to_rads(p_rpm);
}

double KartCore::rads_to_rpm(double p_rads) {
	return kart::core::rads_to_rpm(p_rads);
}

} // namespace kartgame
