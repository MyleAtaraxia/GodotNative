#include <runtime.h>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

#ifdef TOOLS_ENABLED
void gn_register_editor();
void gn_editor_plugin();
#endif
void gn_register_hand_bench();

using namespace godot;

static Ref<NativeLoader> loader;

static void initialize(ModuleInitializationLevel level) {
	if (level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		gn_register_hand_bench();
	}
#ifdef TOOLS_ENABLED
	if (level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		gn_register_editor();
		gn_editor_plugin();
	}
#else
	if (level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_CLASS(NativeCoroutine);
	GDREGISTER_CLASS(NativeLanguage);
	GDREGISTER_CLASS(NativeScript);
	GDREGISTER_CLASS(NativeLoader);
	NativeLanguage::singleton = memnew(NativeLanguage);
	Engine::get_singleton()->register_script_language(NativeLanguage::singleton);
	loader.instantiate();
	ResourceLoader::get_singleton()->add_resource_format_loader(loader, true);
#endif
}

static void uninitialize(ModuleInitializationLevel level) {
#ifndef TOOLS_ENABLED
	if (level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	NativeScript::release_all();
	ResourceLoader::get_singleton()->remove_resource_format_loader(loader);
	loader.unref();
	Engine::get_singleton()->unregister_script_language(NativeLanguage::singleton);
	memdelete(NativeLanguage::singleton);
#endif
}

extern "C" GDExtensionBool GDE_EXPORT godot_native_init(GDExtensionInterfaceGetProcAddress get_proc, GDExtensionClassLibraryPtr library, GDExtensionInitialization *init) {
	GDExtensionBinding::InitObject object(get_proc, library, init);
	object.register_initializer(initialize);
	object.register_terminator(uninitialize);
	object.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
	return object.init();
}
