#include "register_types.h"

#include "audio/audio_probe.h"
#include "audio/engine_voice.h"
#include "audio/noise_voice.h"
#include "kart_core.h"
#include "kart_random.h"
#include "kart_state_hash.h"
#include "session/kart_ghost.h"
#include "session/kart_profile.h"
#include "session/kart_session.h"
#include "track/kart_track.h"
#include "tuning/tuning_registry.h"
#include "vehicle/kart_body.h"
#include "vehicle/player_driver.h"

#include <gdextension_interface.h>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

void initialize_kartgame_module(ModuleInitializationLevel p_level) {
	// Godot calls this once per initialization level. SCENE is where custom Node
	// and Object types belong; registering at CORE or SERVERS would run before the
	// classes we inherit from exist.
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	// Abstract: KartCore is static-only, so GDScript must not be able to .new() it.
	GDREGISTER_ABSTRACT_CLASS(kartgame::KartCore);

	// These two are instantiable, unlike KartCore: each one owns state that a
	// caller is expected to hold. RefCounted, so a scenario script can make one
	// per body without managing lifetimes. ROADMAP M3a.
	GDREGISTER_CLASS(kartgame::KartRandom);
	GDREGISTER_CLASS(kartgame::KartStateHash);

	// The M3b boundary. A Node, so SCENE is the level it must appear at — and it
	// is instantiable because `scripts/game/proving_ground.gd` builds one in code
	// and hands it the mesh and collision shape this class deliberately does not
	// own. ROADMAP M3b, issues #30 and #31.
	GDREGISTER_CLASS(kartgame::KartBody);

	// The human at the controls. ADR-0040: `KartBody` no longer reads the `Input`
	// singleton, so a scene with no driver node is a kart that coasts. That is why
	// this is registered beside the body rather than left to a later milestone —
	// the two are one wiring change and shipping half of it is a game that cannot
	// be driven.
	GDREGISTER_CLASS(kartgame::PlayerDriver);

	// The M8 audio boundary probe. Issue #81, ADR-0035.
	//
	// All three are registered even though only `AudioProbe` is ever constructed
	// from GDScript: `AudioProbeStream::_instantiate_playback` returns an
	// `AudioProbePlayback`, and an unregistered class has no virtual bindings, so
	// Godot would call the base `_mix` and the probe would measure nothing while
	// looking like it worked.
	GDREGISTER_CLASS(kartgame::AudioProbePlayback);
	GDREGISTER_CLASS(kartgame::AudioProbeStream);
	GDREGISTER_CLASS(kartgame::AudioProbe);

	// The production engine note. Issues #81 and #82, ADR-0035.
	//
	// The playback is registered for the same reason the probe's is, and the
	// consequence of forgetting is quieter than a crash: Godot would call
	// `AudioStreamPlayback::_mix` from the base class, the player would report itself
	// as playing, and the kart would be silent with no error anywhere.
	//
	// Registration order matters here in a way it does not for a Node. The playback
	// is instantiated from `EngineVoiceStream::_instantiate_playback`, so it must be
	// known to ClassDB before any stream can hand one back.
	GDREGISTER_CLASS(kartgame::EngineVoicePlayback);
	GDREGISTER_CLASS(kartgame::EngineVoiceStream);

	// Tire scrub and wind. Issue #84, §12.
	//
	// One class for both layers, with a `layer` property — the difference between
	// them is entirely in how a scene mounts them, scrub on an `AudioStreamPlayer3D`
	// at the kart and wind on a plain `AudioStreamPlayer` at the driver's head.
	// Same registration-order rule as above and the same silent failure if it is
	// forgotten.
	GDREGISTER_CLASS(kartgame::NoiseVoicePlayback);
	GDREGISTER_CLASS(kartgame::NoiseVoiceStream);

	// The tuning boundary. ROADMAP M3b, issue #159, ADR-0037.
	//
	// A Node and not a singleton, on purpose: nothing in this project is an
	// autoload, and a globally reachable tuning registry would exist during
	// `drive.sh` and be one edit away from a §6.4 figure measured under a preset
	// that nothing recorded. A scene that wants tuning adds one.
	GDREGISTER_CLASS(kartgame::KartTuning);

	// The shell's seam. ROADMAP M3c, `GAMEDESIGN.md` §9: the front end picks a
	// configuration, hands it to the session runner and reads a result back, and
	// that is what keeps M6's determinism work from having to reason about menus.
	//
	// RefCounted rather than Node, both of them: a session configuration is an
	// argument and a lap timer is one per kart, and neither belongs in a tree.
	GDREGISTER_CLASS(kartgame::KartSession);
	GDREGISTER_CLASS(kartgame::KartClassification);
	GDREGISTER_CLASS(kartgame::KartLapTimer);

	// The save. ROADMAP M3c, ADR-0042, `GAMEDESIGN.md` §8.
	//
	// Two classes rather than one, and the split is ADR-0042's second decision made
	// structural: if `profile.save` will not parse, a player still has to reach the
	// menu, read the text and use the pad. A boot script constructs a `KartSettings`,
	// loads it and has a usable menu before it has looked at a `KartProfile` at all.
	GDREGISTER_CLASS(kartgame::KartProfile);
	GDREGISTER_CLASS(kartgame::KartSettings);

	// The ghost you race, which is a transform stream and not a second kart.
	// RefCounted for the same reason the rest are — one per best-lap slot, held by
	// whatever is drawing it, and `scripts/game/ghost_kart.gd` is the Node3D that
	// does the drawing.
	//
	// ADR-0041: a ghost is **not** re-simulated, so there is no `GhostDriver` here
	// and there must not be one. ADR-0040's producer table says otherwise in one
	// line and ADR-0041 is the later decision.
	GDREGISTER_CLASS(kartgame::KartGhost);

	// The circuit. ROADMAP M5, ADR-0046, issue #63.
	//
	// RefCounted rather than Node, and deliberately: a track is a *definition*
	// that several things read - the scene builds colliders from it, the session
	// runner takes checkpoints and sector marks from it, and `gentrack.py` reads
	// the same file to build the mesh. Making it a Node would put the definition in
	// one place in the tree and invite every other reader to go looking for it
	// there, which is how `settings.cfg` ended up saveable, loadable and never once
	// loaded.
	GDREGISTER_CLASS(kartgame::KartTrack);
}

void uninitialize_kartgame_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
}

extern "C" {
// The symbol named by entry_symbol in kartgame.gdextension. Godot resolves this
// by name after dlopen, so the two must stay in sync — a mismatch shows up as
// "No entry point found", not as a link error.
GDExtensionBool GDE_EXPORT kartgame_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

	init_obj.register_initializer(initialize_kartgame_module);
	init_obj.register_terminator(uninitialize_kartgame_module);
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

	return init_obj.init();
}
}
