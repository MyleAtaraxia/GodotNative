#pragma once

#include <gn_runtime.h>

#include <godot_cpp/classes/resource_format_loader.hpp>
#include <godot_cpp/classes/script_extension.hpp>
#include <godot_cpp/classes/script_language_extension.hpp>
#include <godot_cpp/templates/hash_map.hpp>

#include <vector>

namespace godot {

class NativeLanguage : public ScriptLanguageExtension {
	GDCLASS(NativeLanguage, ScriptLanguageExtension)

protected:
	static void _bind_methods() {}

public:
	static NativeLanguage *singleton;

	String _get_name() const override { return "GodotNative"; }
	void _init() override {}
	String _get_type() const override { return "NativeScript"; }
	String _get_extension() const override { return "gdn"; }
	void _finish() override {}
	PackedStringArray _get_reserved_words() const override { return {}; }
	bool _is_control_flow_keyword(const String &) const override { return false; }
	PackedStringArray _get_comment_delimiters() const override { return {}; }
	PackedStringArray _get_string_delimiters() const override { return {}; }
	bool _is_using_templates() override { return false; }
	bool _has_named_classes() const override { return false; }
	bool _supports_builtin_mode() const override { return false; }
	bool _supports_documentation() const override { return false; }
	bool _can_inherit_from_file() const override { return false; }
	bool _can_make_function() const override { return false; }
	bool _overrides_external_editor() override { return false; }
	void _add_global_constant(const StringName &, const Variant &) override {}
	void _add_named_global_constant(const StringName &, const Variant &) override {}
	void _remove_named_global_constant(const StringName &) override {}
	void _thread_enter() override {}
	void _thread_exit() override {}
	String _debug_get_error() const override { return ""; }
	int32_t _debug_get_stack_level_count() const override { return 0; }
	int32_t _debug_get_stack_level_line(int32_t) const override { return 0; }
	String _debug_get_stack_level_function(int32_t) const override { return ""; }
	String _debug_get_stack_level_source(int32_t) const override { return ""; }
	TypedArray<Dictionary> _debug_get_current_stack_info() override { return {}; }
	void _reload_all_scripts() override {}
	void _reload_scripts(const Array &, bool) override {}
	void _reload_tool_script(const Ref<Script> &, bool) override {}
	PackedStringArray _get_recognized_extensions() const override { return { "gdn" }; }
	TypedArray<Dictionary> _get_public_functions() const override { return {}; }
	Dictionary _get_public_constants() const override { return {}; }
	TypedArray<Dictionary> _get_public_annotations() const override { return {}; }
	void _profiling_start() override {}
	void _profiling_stop() override {}
	void _profiling_set_save_native_calls(bool) override {}
	int32_t _profiling_get_accumulated_data(ScriptLanguageExtensionProfilingInfo *, int32_t) override { return 0; }
	int32_t _profiling_get_frame_data(ScriptLanguageExtensionProfilingInfo *, int32_t) override { return 0; }
	void _frame() override {}
	bool _handles_global_class_type(const String &) const override { return false; }
	Dictionary _get_global_class_name(const String &) const override { return {}; }
};

class NativeCoroutine : public RefCounted {
	GDCLASS(NativeCoroutine, RefCounted)

protected:
	static void _bind_methods();
};

class NativeScript : public ScriptExtension {
	GDCLASS(NativeScript, ScriptExtension)

protected:
	static void _bind_methods();

public:
	const gn::Class *info = nullptr;
	std::vector<const char *> chain;
	mutable Ref<Script> base_script;
	HashMap<StringName, const gn::Method *> methods;
	HashMap<StringName, const gn::Prop *> props;
	HashMap<StringName, const gn::Prop *> statics;
	mutable Dictionary constants;
	mutable bool constants_ready = false;

	static Ref<NativeScript> of(const gn::Class *c);
	static void release_all();
	void setup(const gn::Class *p_info);
	bool read_static(const StringName &name, Variant &ret) const;
	bool write_static(const StringName &name, const Variant &value) const;
	const Dictionary &constant_map() const;
	Variant new_(const Variant **args, GDExtensionInt argc, GDExtensionCallError &error);

	bool _can_instantiate() const override { return !info->is_abstract; }
	Ref<Script> _get_base_script() const override;
	StringName _get_global_name() const override { return info->global_name; }
	bool _inherits_script(const Ref<Script> &s) const override;
	StringName _get_instance_base_type() const override { return info->base; }
	void *_instance_create(Object *owner) const override;
	void *_placeholder_instance_create(Object *) const override { return nullptr; }
	bool _instance_has(Object *o) const override { return o->get_script().operator Object *() == this; }
	bool _has_source_code() const override { return false; }
	String _get_source_code() const override { return ""; }
	void _set_source_code(const String &) override {}
	Error _reload(bool) override { return OK; }
	bool _has_method(const StringName &m) const override { return methods.has(m); }
	bool _has_static_method(const StringName &m) const override;
	Dictionary _get_method_info(const StringName &) const override { return {}; }
	bool _is_tool() const override { return false; }
	bool _is_valid() const override { return true; }
	bool _is_abstract() const override { return info->is_abstract; }
	ScriptLanguage *_get_language() const override { return NativeLanguage::singleton; }
	bool _has_script_signal(const StringName &name) const override;
	TypedArray<Dictionary> _get_script_signal_list() const override;
	bool _has_property_default_value(const StringName &) const override { return false; }
	Variant _get_property_default_value(const StringName &) const override { return {}; }
	void _update_exports() override {}
	TypedArray<Dictionary> _get_script_method_list() const override;
	TypedArray<Dictionary> _get_script_property_list() const override;
	int32_t _get_member_line(const StringName &) const override { return -1; }
	Dictionary _get_constants() const override { return constant_map(); }
	TypedArray<StringName> _get_members() const override;
	bool _is_placeholder_fallback_enabled() const override { return false; }
	Variant _get_rpc_config() const override { return {}; }
};

class NativeLoader : public ResourceFormatLoader {
	GDCLASS(NativeLoader, ResourceFormatLoader)

protected:
	static void _bind_methods() {}

public:
	PackedStringArray _get_recognized_extensions() const override { return { "gd", "gdc" }; }
	bool _recognize_path(const String &path, const StringName &) const override;
	bool _handles_type(const StringName &type) const override { return type == StringName("Script") || type == StringName("GDScript"); }
	String _get_resource_type(const String &path) const override;
	bool _exists(const String &path) const override;
	Variant _load(const String &path, const String &original, bool threads, int32_t cache_mode) const override;
};

} // namespace godot
