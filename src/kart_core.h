#ifndef KARTGAME_KART_CORE_H
#define KARTGAME_KART_CORE_H

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace kartgame {

// The GDScript-facing edge of the C++ extension.
//
// Abstract and static-only on purpose: this is a namespace that GDScript can
// reach, not an object with state. Anything with per-frame state becomes a real
// Node in a later milestone.
//
// It exists in M0 to prove three things at once — that the extension loads, that
// GDScript can call into it, and that src/core/ (which knows nothing about Godot)
// can be reached through it. See docs/DECISIONS.md ADR-0017.
class KartCore : public godot::Object {
	GDCLASS(KartCore, godot::Object)

protected:
	static void _bind_methods();

public:
	// What this binary is and what it was built against. Printed at startup and
	// attached to bug reports — a GDExtension built against the wrong API fails
	// in ways that are much easier to read with this in hand.
	static godot::Dictionary build_info();

	// The KZ performance envelope from src/core/kz_reference.h, surfaced so the
	// tuning UI and the validation scenarios read the same constants the solver
	// does rather than keeping their own copies.
	static godot::Dictionary kz_reference();

	// Unit conversions, exposed so GDScript-side HUD and telemetry code shares one
	// definition with the solver instead of scattering 3.6 through the codebase.
	static double kmh_to_ms(double p_kmh);
	static double ms_to_kmh(double p_ms);
	static double rpm_to_rads(double p_rpm);
	static double rads_to_rpm(double p_rads);
};

} // namespace kartgame

#endif // KARTGAME_KART_CORE_H
