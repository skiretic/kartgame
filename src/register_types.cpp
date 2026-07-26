#include "register_types.h"

#include "audio/audio_probe.h"
#include "audio/engine_voice.h"
#include "kart_core.h"
#include "kart_random.h"
#include "kart_state_hash.h"
#include "tuning/tuning_registry.h"
#include "vehicle/kart_body.h"

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

	// The tuning boundary. ROADMAP M3b, issue #159, ADR-0037.
	//
	// A Node and not a singleton, on purpose: nothing in this project is an
	// autoload, and a globally reachable tuning registry would exist during
	// `drive.sh` and be one edit away from a §6.4 figure measured under a preset
	// that nothing recorded. A scene that wants tuning adds one.
	GDREGISTER_CLASS(kartgame::KartTuning);
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
