#include "transpiler.h"

#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/editor_export_plugin.hpp>
#include <godot_cpp/classes/editor_plugin.hpp>
#include <godot_cpp/classes/editor_plugin_registration.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace {

const char *GENERATED_DIR = "res://addons/godot_native/generated";
const char *LIBRARY = "res://addons/godot_native/bin/godot_native.windows.template_release.x86_64.dll";

std::string std_str(const String &s) {
	return s.utf8().get_data();
}

gn::Ty to_ty(int64_t t, bool is_return_variant) {
	switch ((Variant::Type)t) {
		case Variant::NIL: return is_return_variant ? gn::Ty::Var : gn::Ty::Void;
		case Variant::BOOL: return gn::Ty::Bool;
		case Variant::INT: return gn::Ty::Int;
		case Variant::FLOAT: return gn::Ty::Float;
		case Variant::STRING: return gn::Ty::Str;
		case Variant::VECTOR2: return gn::Ty::V2;
		case Variant::VECTOR3: return gn::Ty::V3;
		case Variant::PACKED_FLOAT64_ARRAY: return gn::Ty::PF64;
		case Variant::PACKED_INT64_ARRAY: return gn::Ty::PI64;
		case Variant::ARRAY: return gn::Ty::Arr;
		case Variant::DICTIONARY: return gn::Ty::Dict;
		default: return gn::Ty::Var;
	}
}

bool direct_ok(gn::Ty t) {
	return t != gn::Ty::Var && t != gn::Ty::Arr && t != gn::Ty::Dict && t != gn::Ty::PF64 && t != gn::Ty::PI64;
}

class LiveClasses : public gn::ProjectQuery {
public:
	bool class_exists(const std::string &name) const override {
		return ClassDBSingleton::get_singleton()->class_exists(name.c_str());
	}
	gn::NativeMember method(const std::string &cls, const std::string &name) const override {
		gn::NativeMember r;
		TypedArray<Dictionary> list = ClassDBSingleton::get_singleton()->class_get_method_list(cls.c_str());
		for (int i = 0; i < list.size(); i++) {
			Dictionary d = list[i];
			if (String(d["name"]) == String(name.c_str())) {
				Dictionary ret = d["return"];
				r.found = true;
				r.type = to_ty(ret["type"], int64_t(ret["usage"]) & PROPERTY_USAGE_NIL_IS_VARIANT);
				r.direct = direct_ok(r.type) && !(int64_t(d["flags"]) & (METHOD_FLAG_VARARG | METHOD_FLAG_VIRTUAL | METHOD_FLAG_STATIC));
				Array args = d["args"];
				for (int a = 0; a < args.size(); a++) {
					Dictionary arg = args[a];
					gn::Ty t = to_ty(arg["type"], true);
					r.args.push_back(t);
					r.direct = r.direct && t != gn::Ty::Void && direct_ok(t) && !(int64_t(arg["usage"]) & (PROPERTY_USAGE_CLASS_IS_ENUM | PROPERTY_USAGE_CLASS_IS_BITFIELD));
				}
				r.defaults = Array(d["default_args"]).size();
				return r;
			}
		}
		return r;
	}
	bool read(const std::string &path, std::string &source) const override {
		String p = path.c_str();
		if (!FileAccess::file_exists(p)) {
			return false;
		}
		source = std_str(FileAccess::get_file_as_string(p));
		return true;
	}
	std::string absolute(const std::string &from, const std::string &path) const override {
		String p = path.c_str();
		return std_str(p.is_absolute_path() ? p : String(from.c_str()).get_base_dir().path_join(p).simplify_path());
	}
	std::string global_class_path(const std::string &name) const override {
		TypedArray<Dictionary> list = ProjectSettings::get_singleton()->get_global_class_list();
		for (int i = 0; i < list.size(); i++) {
			Dictionary d = list[i];
			if (String(d["class"]) == String(name.c_str())) {
				return std_str(d["path"]);
			}
		}
		return "";
	}
	gn::NativeMember property(const std::string &cls, const std::string &name) const override {
		gn::NativeMember r;
		TypedArray<Dictionary> list = ClassDBSingleton::get_singleton()->class_get_property_list(cls.c_str());
		for (int i = 0; i < list.size(); i++) {
			Dictionary d = list[i];
			if (String(d["name"]) == String(name.c_str())) {
				r.found = true;
				r.type = to_ty(d["type"], true);
				return r;
			}
		}
		return r;
	}
};

void find_scripts(const String &dir, PackedStringArray &out) {
	Ref<DirAccess> d = DirAccess::open(dir);
	if (d.is_null()) {
		return;
	}
	for (const String &f : d->get_files()) {
		if (f.get_extension() == "gd") {
			out.push_back(dir.path_join(f));
		}
	}
	for (const String &sub : d->get_directories()) {
		if (!sub.begins_with(".") && sub != "addons") {
			find_scripts(dir.path_join(sub), out);
		}
	}
}

} // namespace

namespace godot {

class NativeExportPlugin : public EditorExportPlugin {
	GDCLASS(NativeExportPlugin, EditorExportPlugin)

protected:
	static void _bind_methods() {}

public:
	String _get_name() const override { return "GodotNative"; }

	void _export_begin(const PackedStringArray &, bool, const String &, uint32_t) override {
		// shortcut: failures only print an error and remove the library, because _export_begin cannot abort. Upgrade once a real export hook exists for this.
		DirAccess::make_dir_recursive_absolute(GENERATED_DIR);
		Ref<DirAccess> gen = DirAccess::open(GENERATED_DIR);
		for (const String &f : gen->get_files()) {
			gen->remove(f);
		}
		DirAccess::remove_absolute(LIBRARY);

		Ref<FileAccess> ignore = FileAccess::open(String(GENERATED_DIR).path_join(".gdignore"), FileAccess::WRITE);
		ignore.unref();

		PackedStringArray scripts;
		find_scripts("res://", scripts);
		LiveClasses classes;
		std::vector<std::string> toggled;
		for (const String &path : scripts) {
			if (gn::is_toggled(std_str(FileAccess::get_file_as_string(path)))) {
				toggled.push_back(std_str(path));
			}
		}
		gn::Output r = gn::transpile_project(toggled, classes);
		for (const std::string &w : r.warnings) {
			UtilityFunctions::print("[GodotNative] warning: ", w.c_str());
		}
		if (!r.ok) {
			UtilityFunctions::push_error("[GodotNative] ", r.error.c_str());
			return;
		}
		for (const std::string &p : r.implied) {
			UtilityFunctions::print("[GodotNative] also compiled as a parent script: ", p.c_str());
		}
		for (const auto &file : r.files) {
			Ref<FileAccess> f = FileAccess::open(String(GENERATED_DIR).path_join(file.first.c_str()), FileAccess::WRITE);
			f->store_string(String::utf8(file.second.c_str()));
		}
		int compiled = r.files.size() / 2;
		String root = ProjectSettings::get_singleton()->globalize_path("res://").path_join("..").simplify_path();
		Array output;
		int code = OS::get_singleton()->execute("scons", { "-C", root, "target=template_release", "-j8" }, output, true);
		if (code != 0) {
			UtilityFunctions::push_error("[GodotNative] scons failed:\n", output);
			return;
		}
		UtilityFunctions::print("[GodotNative] compiled ", compiled, " file(s)");
	}
};

class NativeEditorPlugin : public EditorPlugin {
	GDCLASS(NativeEditorPlugin, EditorPlugin)

	Ref<NativeExportPlugin> exporter;

protected:
	static void _bind_methods() {}

public:
	void _enter_tree() override {
		exporter.instantiate();
		add_export_plugin(exporter);
	}
	void _exit_tree() override {
		remove_export_plugin(exporter);
		exporter.unref();
	}
};

} // namespace godot

void gn_register_editor() {
	GDREGISTER_INTERNAL_CLASS(NativeExportPlugin);
	GDREGISTER_INTERNAL_CLASS(NativeEditorPlugin);
}

void gn_editor_plugin() {
	EditorPlugins::add_by_type<NativeEditorPlugin>();
}
