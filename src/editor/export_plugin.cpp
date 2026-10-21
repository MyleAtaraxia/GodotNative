#include "transpiler.h"

#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/editor_export_plugin.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/editor_plugin.hpp>
#include <godot_cpp/classes/editor_plugin_registration.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/theme.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <map>
#include <set>

using namespace godot;

namespace {

const char *GENERATED_DIR = "res://addons/godot_native/generated";

std::string std_str(const String &s) {
	return s.utf8().get_data();
}

struct ApiMethod {
	uint32_t hash = 0;
	bool plain = false;
	std::vector<std::string> args;
	std::vector<std::string> defaults;
	std::string ret;
};

struct ApiProperty {
	std::string getter, setter;
};

struct ApiData {
	std::map<std::string, int64_t> globals;
	std::set<std::string> builtin_constants;
	std::set<std::string> builtin_types;
	std::map<std::string, ApiMethod> methods;
	std::map<std::string, ApiProperty> properties;
	std::map<std::string, std::string> parents;
	std::map<std::string, std::pair<std::string, bool>> utilities;
};

const ApiData &api_data() {
	static ApiData *data = nullptr;
	if (data) {
		return *data;
	}
	data = new ApiData();
	String root = ProjectSettings::get_singleton()->globalize_path("res://").trim_suffix("/").get_base_dir();
	Dictionary api = JSON::parse_string(FileAccess::get_file_as_string(root.path_join("api/extension_api.json")));
	Array constants = api.get("global_constants", Array());
	for (int i = 0; i < constants.size(); i++) {
		Dictionary c = constants[i];
		data->globals[std_str(c["name"])] = int64_t(double(c["value"]));
	}
	Array enums = api.get("global_enums", Array());
	for (int i = 0; i < enums.size(); i++) {
		Dictionary e = enums[i];
		Array values = e["values"];
		for (int v = 0; v < values.size(); v++) {
			Dictionary c = values[v];
			data->globals[std_str(c["name"])] = int64_t(double(c["value"]));
		}
	}
	Array utilities = api.get("utility_functions", Array());
	for (int i = 0; i < utilities.size(); i++) {
		Dictionary u = utilities[i];
		data->utilities[std_str(u["name"])] = { std_str(u.get("return_type", "void")), bool(u["is_vararg"]) };
	}
	Array builtins = api.get("builtin_classes", Array());
	for (int i = 0; i < builtins.size(); i++) {
		Dictionary b = builtins[i];
		data->builtin_types.insert(std_str(b["name"]));
		Array list = b.get("constants", Array());
		for (int c = 0; c < list.size(); c++) {
			Dictionary k = list[c];
			data->builtin_constants.insert(std_str(b["name"]) + "." + std_str(k["name"]));
		}
	}
	Array classes = api.get("classes", Array());
	for (int i = 0; i < classes.size(); i++) {
		Dictionary c = classes[i];
		std::string name = std_str(c["name"]);
		if (c.has("inherits")) {
			data->parents[name] = std_str(c["inherits"]);
		}
		Array methods = c.get("methods", Array());
		for (int m = 0; m < methods.size(); m++) {
			Dictionary d = methods[m];
			if (!d.has("hash")) {
				continue;
			}
			ApiMethod info;
			info.hash = uint32_t(double(d["hash"]));
			info.plain = !bool(d["is_vararg"]) && !bool(d["is_virtual"]) && !bool(d["is_static"]);
			Array args = d.get("arguments", Array());
			for (int a = 0; a < args.size(); a++) {
				Dictionary arg = args[a];
				info.args.push_back(std_str(arg["type"]));
				info.defaults.push_back(std_str(arg.get("default_value", "")));
			}
			Dictionary ret = d.get("return_value", Dictionary());
			info.ret = ret.has("type") ? std_str(ret["type"]) : "void";
			data->methods[name + "." + std_str(d["name"])] = info;
		}
		Array props = c.get("properties", Array());
		for (int n = 0; n < props.size(); n++) {
			Dictionary d = props[n];
			if (!d.has("index")) {
				data->properties[name + "." + std_str(d["name"])] = { std_str(d.get("getter", "")), std_str(d.get("setter", "")) };
			}
		}
	}
	return *data;
}

bool value_type(const ApiData &api, const std::string &t) {
	bool prefixed = t.rfind("enum::", 0) == 0 || t.rfind("bitfield::", 0) == 0 || t.rfind("typedarray::", 0) == 0 || t.rfind("typeddictionary::", 0) == 0;
	return prefixed || t == "Variant" || t == "int" || t == "float" || t == "bool" || api.builtin_types.count(t) > 0;
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
				r.is_static = int64_t(d["flags"]) & METHOD_FLAG_STATIC;
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
	bool int_constant(const std::string &cls, const std::string &name, int64_t &value) const override {
		ClassDBSingleton *db = ClassDBSingleton::get_singleton();
		if (!db->class_has_integer_constant(cls.c_str(), name.c_str())) {
			return false;
		}
		value = db->class_get_integer_constant(cls.c_str(), name.c_str());
		return true;
	}
	bool is_singleton(const std::string &name) const override {
		return Engine::get_singleton()->has_singleton(name.c_str());
	}
	gn::NativeBind bind(const std::string &cls, const std::string &name) const override {
		gn::NativeBind r;
		const ApiData &api = api_data();
		std::string c = cls;
		for (;;) {
			auto it = api.methods.find(c + "." + name);
			if (it != api.methods.end()) {
				const ApiMethod &m = it->second;
				r.found = true;
				r.owner = c;
				r.hash = m.hash;
				r.args = m.args;
				r.defaults = m.defaults;
				r.ret = m.ret;
				r.ptr = m.plain;
				for (const std::string &a : m.args) {
					r.ptr = r.ptr && value_type(api, a);
				}
				return r;
			}
			auto up = api.parents.find(c);
			if (up == api.parents.end()) {
				return r;
			}
			c = up->second;
		}
	}
	bool accessors(const std::string &cls, const std::string &name, std::string &getter, std::string &setter) const override {
		const ApiData &api = api_data();
		std::string c = cls;
		for (;;) {
			auto it = api.properties.find(c + "." + name);
			if (it != api.properties.end()) {
				getter = it->second.getter;
				setter = it->second.setter;
				return true;
			}
			auto up = api.parents.find(c);
			if (up == api.parents.end()) {
				return false;
			}
			c = up->second;
		}
	}
	bool utility(const std::string &name, std::string &ret, bool &vararg) const override {
		auto it = api_data().utilities.find(name);
		if (it == api_data().utilities.end()) {
			return false;
		}
		ret = it->second.first;
		vararg = it->second.second;
		return true;
	}
	bool has_signal(const std::string &cls, const std::string &name) const override {
		return ClassDBSingleton::get_singleton()->class_has_signal(cls.c_str(), name.c_str());
	}
	bool global_constant(const std::string &name, int64_t &value) const override {
		auto it = api_data().globals.find(name);
		if (it == api_data().globals.end()) {
			return false;
		}
		value = it->second;
		return true;
	}
	bool has_builtin_constant(const std::string &type, const std::string &name) const override {
		return api_data().builtin_constants.count(type + "." + name) > 0;
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

String library_path(const String &target) {
	return "res://addons/godot_native/bin/godot_native.windows." + target + ".x86_64.dll";
}

bool sync_generated(const gn::Output &r, int &sources) {
	bool changed = false;
	PackedStringArray ids;
	for (const auto &file : r.files) {
		String name = file.first.c_str();
		ids.push_back(name.get_slice(".", 0));
		String path = String(GENERATED_DIR).path_join(name);
		String text = String::utf8(file.second.c_str());
		if (!FileAccess::file_exists(path) || FileAccess::get_file_as_string(path) != text) {
			Ref<FileAccess> f = FileAccess::open(path, FileAccess::WRITE);
			f->store_string(text);
			changed = true;
			sources += name.ends_with(".cpp");
		}
	}
	Ref<DirAccess> gen = DirAccess::open(GENERATED_DIR);
	for (const String &f : gen->get_files()) {
		String id = f.get_slice(".", 0);
		if (!id.is_empty() && !ids.has(id)) {
			gen->remove(f);
			changed = true;
		}
	}
	return changed;
}

class BuildPopup {
	Window *window = nullptr;
	Control *canvas = nullptr;
	Ref<Font> font;
	int font_size = 16;
	float scale = 1.0;
	Color base, accent, text, muted;
	Size2 size;

public:
	BuildPopup() {
		EditorInterface *editor = EditorInterface::get_singleton();
		Ref<Theme> theme = editor->get_editor_theme();
		scale = editor->get_editor_scale();
		font = theme->get_font("main", "EditorFonts");
		font_size = theme->get_font_size("main_size", "EditorFonts");
		base = theme->get_color("base_color", "Editor");
		accent = theme->get_color("accent_color", "Editor");
		text = theme->get_color("font_color", "Editor");
		muted = theme->get_color("font_placeholder_color", "Editor");
		size = Size2(460, 112) * scale;
		window = memnew(Window);
		window->set_title("GodotNative");
		window->set_flag(Window::FLAG_RESIZE_DISABLED, true);
		canvas = memnew(Control);
		window->add_child(canvas);
		window->popup_exclusive_centered(editor->get_base_control(), Vector2i(size));
	}

	~BuildPopup() {
		window->hide();
		window->queue_free();
	}

	void draw(const String &status, double ratio, int64_t seconds) {
		RenderingServer *rs = RenderingServer::get_singleton();
		RID item = canvas->get_canvas_item();
		float pad = 16 * scale, line = font_size * 1.5f;
		float width = size.x - pad * 2;
		rs->canvas_item_clear(item);
		rs->canvas_item_add_rect(item, Rect2(Vector2(), size), base);
		font->draw_string(item, Vector2(pad, pad + font_size), "Compiling native scripts", HORIZONTAL_ALIGNMENT_LEFT, width, font_size, text);
		font->draw_string(item, Vector2(pad, pad + font_size), itos(seconds) + " s", HORIZONTAL_ALIGNMENT_RIGHT, width, font_size, muted);
		font->draw_string(item, Vector2(pad, pad + font_size + line), status, HORIZONTAL_ALIGNMENT_LEFT, width, font_size, muted);
		Rect2 bar(pad, size.y - pad - 8 * scale, width, 8 * scale);
		rs->canvas_item_add_rect(item, bar, Color(text, 0.15f));
		rs->canvas_item_add_rect(item, Rect2(bar.position, Vector2(bar.size.x * ratio, bar.size.y)), accent);
		DisplayServer::get_singleton()->process_events();
		rs->force_draw();
	}
};

String complete_lines(const Ref<FileAccess> &pipe, PackedByteArray &pending, bool flush) {
	for (;;) {
		PackedByteArray chunk = pipe->get_buffer(65536);
		if (chunk.is_empty()) {
			break;
		}
		pending.append_array(chunk);
	}
	int64_t end = flush ? pending.size() - 1 : pending.rfind('\n');
	if (end < 0) {
		return "";
	}
	String text = pending.slice(0, end + 1).get_string_from_utf8();
	pending = pending.slice(end + 1);
	return text;
}

int run_with_popup(const PackedStringArray &args, const String &root, const String &target, int sources, Array &output) {
	OS::get_singleton()->set_environment("PYTHONUNBUFFERED", "1");
	Dictionary process = OS::get_singleton()->execute_with_pipe("scons", args, false);
	OS::get_singleton()->unset_environment("PYTHONUNBUFFERED");
	if (process.is_empty()) {
		return -1;
	}
	Ref<FileAccess> out = process["stdio"], err = process["stderr"];
	int64_t pid = process["pid"];
	BuildPopup popup;
	String log, status = "Starting SCons";
	PackedByteArray pending_out, pending_err;
	String suffix = library_path(target).get_file().trim_prefix("godot_native").get_basename();
	std::vector<String> objects;
	uint64_t start = Time::get_singleton()->get_ticks_msec();
	auto read = [&](bool flush) {
		String text = complete_lines(out, pending_out, flush) + complete_lines(err, pending_err, flush);
		for (const String &raw : text.split("\n", false)) {
			String row = raw.strip_edges();
			log += row + "\n";
			if (row.begins_with("Compiling")) {
				PackedStringArray words = row.replace("\\", "/").split(" ", false);
				String source = words.size() > 1 ? String(words[words.size() - 2]) : String();
				objects.push_back(root.path_join(source).get_basename() + suffix);
				status = String("Compiling ") + source.get_file();
			} else if (row.begins_with("Linking")) {
				status = "Linking";
			}
		}
	};
	while (OS::get_singleton()->is_process_running(pid)) {
		read(false);
		int finished = 0;
		for (const String &object : objects) {
			finished += FileAccess::file_exists(object + String(".obj")) || FileAccess::file_exists(object + String(".os"));
		}
		double ratio = MIN(double(finished) / (MAX(sources, int(objects.size())) + 1), 0.95);
		popup.draw(status, ratio, (Time::get_singleton()->get_ticks_msec() - start) / 1000);
		OS::get_singleton()->delay_msec(33);
	}
	read(true);
	output.push_back(log);
	return OS::get_singleton()->get_process_exit_code(pid);
}

bool compile_native(const String &target, bool show_progress = false) {
	DirAccess::make_dir_recursive_absolute(GENERATED_DIR);
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
		return false;
	}
	for (const std::string &p : r.implied) {
		UtilityFunctions::print("[GodotNative] also compiled as a parent script: ", p.c_str());
	}
	// shortcut: only generated code is compared, so changing the addon's own runtime needs the old library deleted. Upgrade by hashing the runtime sources if that ever bites users.
	int sources = 0;
	if (!sync_generated(r, sources) && FileAccess::file_exists(library_path(target))) {
		return true;
	}
	String project_dir = ProjectSettings::get_singleton()->globalize_path("res://").trim_suffix("/");
	String root = project_dir.get_base_dir();
	Array output;
	PackedStringArray args = { "-C", root, "target=" + target, "project=" + project_dir.get_file(), "-j8" };
	bool popup = show_progress && DisplayServer::get_singleton()->get_name() != "headless";
	int code = popup ? run_with_popup(args, root, target, sources, output) : OS::get_singleton()->execute("scons", args, output, true);
	if (code != 0) {
		UtilityFunctions::push_error("[GodotNative] scons failed:\n", output);
		return false;
	}
	UtilityFunctions::print("[GodotNative] compiled ", int(r.files.size() / 2), " file(s) for ", target);
	return true;
}

} // namespace

namespace godot {

class NativeExportPlugin : public EditorExportPlugin {
	GDCLASS(NativeExportPlugin, EditorExportPlugin)

protected:
	static void _bind_methods() {}

public:
	String _get_name() const override { return "GodotNative"; }

	void _export_begin(const PackedStringArray &, bool is_debug, const String &, uint32_t) override {
		// shortcut: failures only print an error and remove the library, because _export_begin cannot abort. Upgrade once a real export hook exists for this.
		String target = is_debug ? "template_debug" : "template_release";
		DirAccess::remove_absolute(library_path(target));
		compile_native(target);
	}
};

class NativeEditorPlugin : public EditorPlugin {
	GDCLASS(NativeEditorPlugin, EditorPlugin)

	Ref<NativeExportPlugin> exporter;

protected:
	static void _bind_methods() {}

public:
	bool _build() override { return compile_native("template_debug", true); }
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
