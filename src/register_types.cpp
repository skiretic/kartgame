#include "register_types.h"

#include "kart_core.h"
#include "kart_random.h"
#include "kart_state_hash.h"

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
