#include "pragma_editor.h"

#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/editor_export_platform.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/editor_export_plugin.hpp>
#include <godot_cpp/classes/editor_plugin.hpp>
#include <godot_cpp/classes/editor_plugin_registration.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/gd_script.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <map>
#include <set>

using namespace godot;

namespace {

const char *GENERATED_DIR = "res://addons/godot_native/generated";
const char *PROGRESS_SCRIPT = "res://addons/godot_native/GDProgress/gd_progress.gd";
const char *PROGRESS_TASK = "GodotNative";

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
	std::map<std::string, ApiMethod> builtin_methods;
	std::map<std::string, int> builtin_constructors;
	std::map<std::string, std::string> builtin_operators;
	std::map<std::string, ApiProperty> properties;
	std::map<std::string, std::string> parents;
	std::set<std::string> classes;
	std::map<std::string, std::pair<std::string, bool>> utilities;
};

String project_root() {
	String project = ProjectSettings::get_singleton()->globalize_path("res://").trim_suffix("/");
	String bundled = project.path_join("addons/godot_native/build");
	return FileAccess::file_exists(bundled.path_join("SConstruct")) ? bundled : project.get_base_dir();
}

// shortcut: only Godot 4 is searched, and an engine newer than godot-cpp's bundled files uses the newest one. Upgrade godot-cpp to reach newer API.
String native_api() {
	String forced = OS::get_singleton()->get_environment("GODOT_NATIVE_API");
	if (!forced.is_empty()) {
		return forced;
	}
	int minor = Engine::get_singleton()->get_version_info()["minor"];
	for (; minor > 4; minor--) {
		if (FileAccess::file_exists(project_root().path_join("godot-cpp/gdextension/extension_api-4-" + String::num_int64(minor) + ".json"))) {
			break;
		}
	}
	return "4." + String::num_int64(minor);
}

const ApiData &api_data() {
	static ApiData *data = nullptr;
	if (data) {
		return *data;
	}
	data = new ApiData();
	Dictionary api = JSON::parse_string(FileAccess::get_file_as_string(project_root().path_join("godot-cpp/gdextension/extension_api-" + native_api().replace(".", "-") + ".json")));
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
		std::string type = std_str(b["name"]);
		Array calls = b.get("methods", Array());
		for (int m = 0; m < calls.size(); m++) {
			Dictionary d = calls[m];
			if (bool(d.get("is_vararg", false)) || bool(d.get("is_static", false)) || !d.has("hash")) {
				continue;
			}
			ApiMethod info;
			info.hash = uint32_t(int64_t(double(d["hash"])));
			Array args = d.get("arguments", Array());
			for (int a = 0; a < args.size(); a++) {
				Dictionary arg = args[a];
				info.args.push_back(std_str(arg["type"]));
				info.defaults.push_back(std_str(arg.get("default_value", "")));
			}
			info.ret = std_str(d.get("return_type", "void"));
			data->builtin_methods[type + "." + std_str(d["name"])] = info;
		}
		Array constructors = b.get("constructors", Array());
		for (int m = 0; m < constructors.size(); m++) {
			Dictionary d = constructors[m];
			std::string key = type;
			Array args = d.get("arguments", Array());
			for (int a = 0; a < args.size(); a++) {
				Dictionary arg = args[a];
				key += "," + std_str(arg["type"]);
			}
			data->builtin_constructors[key] = int(double(d["index"]));
		}
		Array operators = b.get("operators", Array());
		for (int m = 0; m < operators.size(); m++) {
			Dictionary d = operators[m];
			if (d.has("right_type")) {
				data->builtin_operators[type + " " + std_str(d["name"]) + " " + std_str(d["right_type"])] = std_str(d["return_type"]);
			}
		}
	}
	Array classes = api.get("classes", Array());
	for (int i = 0; i < classes.size(); i++) {
		Dictionary c = classes[i];
		std::string name = std_str(c["name"]);
		data->classes.insert(name);
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
		case Variant::VECTOR2I: return gn::Ty::V2I;
		case Variant::VECTOR3I: return gn::Ty::V3I;
		case Variant::COLOR: return gn::Ty::Color;
		case Variant::RECT2: return gn::Ty::Rect2;
		case Variant::BASIS: return gn::Ty::Basis;
		case Variant::QUATERNION: return gn::Ty::Quat;
		case Variant::TRANSFORM3D: return gn::Ty::T3D;
		case Variant::TRANSFORM2D: return gn::Ty::T2D;
		case Variant::AABB: return gn::Ty::Aabb;
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
	bool inherits(const std::string &cls, const std::string &base) const override {
		return ClassDBSingleton::get_singleton()->is_parent_class(cls.c_str(), base.c_str());
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
	bool quiet = false;

	bool evaluate(const std::string &expression, Variant &value) const {
		if (quiet) {
			value = int64_t(0);
			return true;
		}
		Ref<GDScript> probe;
		probe.instantiate();
		probe->set_source_code(String("static func value():\n\treturn ") + expression.c_str() + "\n");
		if (probe->reload() != OK) {
			return false;
		}
		value = probe->call("value");
		return true;
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
	bool api_has_class(const std::string &name) const override {
		return api_data().classes.count(name) > 0;
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
		if (it != api_data().globals.end()) {
			value = it->second;
			return true;
		}
		Variant live;
		if (!evaluate(name, live) || live.get_type() != Variant::INT) {
			return false;
		}
		value = live;
		return true;
	}
	bool has_builtin_constant(const std::string &type, const std::string &name) const override {
		Variant live;
		return api_data().builtin_constants.count(type + "." + name) > 0 || evaluate(type + "." + name, live);
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
	gn::BuiltinMethod builtin_method(const std::string &type, const std::string &name) const override {
		gn::BuiltinMethod r;
		const ApiData &api = api_data();
		auto it = api.builtin_methods.find(type + "." + name);
		if (it != api.builtin_methods.end()) {
			r.found = true;
			r.hash = it->second.hash;
			r.args = it->second.args;
			r.defaults = it->second.defaults;
			r.ret = it->second.ret;
		}
		return r;
	}
	int builtin_constructor(const std::string &type, const std::vector<std::string> &args) const override {
		std::string key = type;
		for (const std::string &arg : args) {
			key += "," + arg;
		}
		const ApiData &api = api_data();
		auto it = api.builtin_constructors.find(key);
		return it == api.builtin_constructors.end() ? -1 : it->second;
	}
	std::string builtin_operator(const std::string &left, const std::string &op, const std::string &right) const override {
		const ApiData &api = api_data();
		auto it = api.builtin_operators.find(left + " " + op + " " + right);
		return it == api.builtin_operators.end() ? "" : it->second;
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

int int_setting(const char *name, int fallback) {
	return int(ProjectSettings::get_singleton()->get_setting(String("godot_native/") + name, fallback));
}

String scons_path() {
	String path = ProjectSettings::get_singleton()->get_setting("godot_native/build/scons_path", "scons");
	return path.is_empty() ? "scons" : path;
}

void add_setting(const String &name, const Variant &value, PropertyHint hint = PROPERTY_HINT_NONE, const String &hint_string = "") {
	ProjectSettings *settings = ProjectSettings::get_singleton();
	String key = "godot_native/" + name;
	if (!settings->has_setting(key)) {
		settings->set_setting(key, value);
	}
	settings->set_initial_value(key, value);
	settings->set_as_basic(key, true);
	Dictionary info;
	info["name"] = key;
	info["type"] = value.get_type();
	info["hint"] = hint;
	info["hint_string"] = hint_string;
	settings->add_property_info(info);
}

void register_settings() {
	add_setting("inheritance/children_of_native", gn::Options::CHILDREN_COMPILE, PROPERTY_HINT_ENUM, "Compile,Fail Export,Leave Interpreted");
	add_setting("errors/project_scripts", gn::Options::ERRORS_FAIL, PROPERTY_HINT_ENUM, "Fail Export,Warn and Run Interpreted");
	add_setting("errors/addon_scripts", gn::Options::ERRORS_FAIL, PROPERTY_HINT_ENUM, "Fail Export,Warn and Run Interpreted,Don't Scan Addons");
	add_setting("warnings/untyped_code", gn::Options::UNTYPED_WARN, PROPERTY_HINT_ENUM, "Ignore,Warn,Error");
	add_setting("build/native_in_debug_runs", true);
	add_setting("build/jobs", 8, PROPERTY_HINT_RANGE, "0,256,1");
	add_setting("build/scons_path", "scons");
}

void find_scripts(const String &dir, PackedStringArray &out, PackedStringArray &scenes) {
	Ref<DirAccess> d = DirAccess::open(dir);
	if (d.is_null()) {
		return;
	}
	for (const String &f : d->get_files()) {
		if (f.get_extension() == "gd") {
			out.push_back(dir.path_join(f));
		} else if (f.get_extension() == "tscn") {
			scenes.push_back(dir.path_join(f));
		}
	}
	for (const String &sub : d->get_directories()) {
		String path = dir.path_join(sub);
		bool skip_addons = path == "res://addons" && int_setting("errors/addon_scripts", 0) == gn::Options::ERRORS_SKIP_ADDONS;
		if (!sub.begins_with(".") && path != "res://addons/godot_native" && !skip_addons) {
			find_scripts(dir.path_join(sub), out, scenes);
		}
	}
}

// shortcut: only text scenes are read, so a built-in script in a binary .scn goes unchecked. Upgrade if projects ship binary scenes with built-in scripts.
void embedded_sources(const String &scene, std::vector<std::pair<std::string, std::string>> &out) {
	const std::string text = std_str(FileAccess::get_file_as_string(scene));
	const std::string key = "script/source = \"";
	for (size_t at = text.find(key); at != std::string::npos; at = text.find(key, at + 1)) {
		std::string source;
		for (size_t q = at + key.size(); q < text.size() && text[q] != '"'; q++) {
			if (text[q] == '\\' && q + 1 < text.size()) {
				q++;
				source += text[q] == 'n' ? '\n' : text[q] == 't' ? '\t' : text[q];
			} else {
				source += text[q];
			}
		}
		out.push_back({ std_str(scene), source });
	}
}

String library_path(const String &target) {
	return "res://addons/godot_native/bin/godot_native.windows." + target + ".x86_64.dll";
}

String build_stamp(const gn::Output &r) {
	std::string all = std_str(native_api()) + std::to_string(FileAccess::get_modified_time(library_path("editor")));
	for (const auto &file : r.files) {
		all += file.first + file.second;
	}
	return String::utf8(all.c_str()).md5_text();
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

Ref<Script> gd_progress() {
	ResourceLoader *loader = ResourceLoader::get_singleton();
	return loader->exists(PROGRESS_SCRIPT) ? Ref<Script>(loader->load(PROGRESS_SCRIPT)) : Ref<Script>();
}

int run_with_progress(const Ref<Script> &progress, const PackedStringArray &args, const String &root, const String &target, int sources, Array &output) {
	OS::get_singleton()->set_environment("PYTHONUNBUFFERED", "1");
	Dictionary process = OS::get_singleton()->execute_with_pipe(scons_path(), args, false);
	OS::get_singleton()->unset_environment("PYTHONUNBUFFERED");
	if (process.is_empty()) {
		return -1;
	}
	Ref<FileAccess> out = process["stdio"], err = process["stderr"];
	int64_t pid = process["pid"];
	String log, status = "Starting SCons";
	PackedByteArray pending_out, pending_err;
	String suffix = library_path(target).get_file().trim_prefix("godot_native").get_basename();
	std::vector<String> objects;
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
			finished += FileAccess::file_exists(object + String(".o")) || FileAccess::file_exists(object + String(".obj")) || FileAccess::file_exists(object + String(".os"));
		}
		double ratio = MIN(double(finished) / (MAX(sources, int(objects.size())) + 1), 0.95);
		progress->call("update", PROGRESS_TASK, status, ratio);
		OS::get_singleton()->delay_msec(33);
	}
	read(true);
	output.push_back(log);
	return OS::get_singleton()->get_process_exit_code(pid);
}

class SourceClasses : public LiveClasses {
	std::string path;
	std::string source;

public:
	SourceClasses(const std::string &p_path, const std::string &p_source) :
			path(p_path), source(p_source) {
		quiet = true;
	}
	bool read(const std::string &file, std::string &text) const override {
		if (file == path) {
			text = source;
			return true;
		}
		return LiveClasses::read(file, text);
	}
};

bool uses_openmp = false;

bool compile_native(const String &target, bool show_progress = false) {
	DirAccess::make_dir_recursive_absolute(GENERATED_DIR);
	Ref<FileAccess> ignore = FileAccess::open(String(GENERATED_DIR).path_join(".gdignore"), FileAccess::WRITE);
	ignore.unref();

	PackedStringArray scripts;
	PackedStringArray scenes;
	find_scripts("res://", scripts, scenes);
	std::vector<std::pair<std::string, std::string>> embedded;
	for (const String &scene : scenes) {
		embedded_sources(scene, embedded);
	}
	LiveClasses classes;
	std::vector<std::string> toggled, all;
	for (const String &path : scripts) {
		all.push_back(std_str(path));
		if (gn::is_toggled(std_str(FileAccess::get_file_as_string(path)))) {
			toggled.push_back(std_str(path));
		}
	}
	gn::Options options;
	options.children = int_setting("inheritance/children_of_native", options.children);
	options.project_errors = int_setting("errors/project_scripts", options.project_errors);
	options.addon_errors = int_setting("errors/addon_scripts", options.addon_errors);
	options.untyped = int_setting("warnings/untyped_code", options.untyped);
	gn::Output r = gn::transpile_project(toggled, all, embedded, options, classes);
	uses_openmp = std::any_of(r.files.begin(), r.files.end(), [](const auto &f) { return f.second.find("#pragma omp") != std::string::npos; });
	for (const std::string &f : r.fallback) {
		UtilityFunctions::push_warning("[GodotNative] running interpreted: ", f.c_str());
	}
	for (const std::string &w : r.warnings) {
		UtilityFunctions::print("[GodotNative] warning: ", w.c_str());
	}
	if (!r.ok) {
		UtilityFunctions::push_error("[GodotNative] ", r.error.c_str());
		return false;
	}
	for (const std::string &p : r.implied) {
		UtilityFunctions::print("[GodotNative] also compiled through inheritance: ", p.c_str());
	}
	int sources = 0;
	sync_generated(r, sources);
	String stamp_path = String(GENERATED_DIR).path_join(".stamp_" + target);
	String stamp = build_stamp(r);
	if (FileAccess::file_exists(library_path(target)) && FileAccess::get_file_as_string(stamp_path) == stamp) {
		return true;
	}
	String project_dir = ProjectSettings::get_singleton()->globalize_path("res://").trim_suffix("/");
	String root = project_root();
	Array output;
	int jobs = int_setting("build/jobs", 8);
	jobs = jobs > 0 ? jobs : OS::get_singleton()->get_processor_count();
	PackedStringArray args = { "-C", root, "target=" + target, "project=" + project_dir, "api_version=" + native_api(), "-j" + String::num_int64(jobs) };
	Ref<Script> progress = show_progress && DisplayServer::get_singleton()->get_name() != "headless" ? gd_progress() : Ref<Script>();
	int code = progress.is_valid() ? run_with_progress(progress, args, root, target, sources, output) : OS::get_singleton()->execute(scons_path(), args, output, true);
	if (code != 0) {
		UtilityFunctions::push_error("[GodotNative] scons failed, ran ", scons_path(), ":\n", output);
		return false;
	}
	FileAccess::open(stamp_path, FileAccess::WRITE)->store_string(stamp);
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
		// shortcut: a failure reports an export error and ships no library, but the export still writes its files because _export_begin cannot abort. Upgrade once a real export hook exists for this.
		String target = is_debug ? "template_debug" : "template_release";
		if (compile_native(target)) {
			String runtime = library_path(target).get_base_dir().path_join("libomp.dll");
			if (uses_openmp && FileAccess::file_exists(runtime)) {
				add_shared_object(runtime, PackedStringArray(), "");
			}
			return;
		}
		DirAccess::remove_absolute(library_path(target));
		Ref<EditorExportPlatform> platform = get_export_platform();
		if (platform.is_valid()) {
			platform->add_message(EditorExportPlatform::EXPORT_MESSAGE_ERROR, "GodotNative", "Native compile failed, so this export has no native library. See the Output panel.");
		}
	}
};

class NativeEditorPlugin : public EditorPlugin {
	GDCLASS(NativeEditorPlugin, EditorPlugin)

	Ref<NativeExportPlugin> exporter;

protected:
	static void _bind_methods() {}

public:
	bool _build() override {
		bool ok = true;
		if (int_setting("build/native_in_debug_runs", 1)) {
			ok = compile_native("template_debug", true);
		} else if (!FileAccess::file_exists(library_path("template_debug"))) {
			compile_native("template_debug", true);
		}
		Ref<Script> progress = gd_progress();
		if (progress.is_valid()) {
			progress->call("finish", PROGRESS_TASK);
		}
		return ok;
	}
	void _enter_tree() override {
		register_settings();
		Ref<Script> progress = gd_progress();
		if (progress.is_valid()) {
			progress->call("add_task", PROGRESS_TASK);
		}
		exporter.instantiate();
		add_export_plugin(exporter);
		gn_pragma_editor_start();
	}
	void _exit_tree() override {
		Ref<Script> progress = gd_progress();
		if (progress.is_valid()) {
			progress->call("remove_task", PROGRESS_TASK);
		}
		gn_pragma_editor_stop();
		remove_export_plugin(exporter);
		exporter.unref();
	}
};

} // namespace godot

gn::Output gn_check_source(const std::string &path, const std::string &source) {
	SourceClasses classes(path, source);
	return gn::transpile_project({ path }, { path }, {}, gn::Options(), classes);
}

void gn_register_editor() {
	gn_register_pragma_editor();
	GDREGISTER_INTERNAL_CLASS(NativeExportPlugin);
	GDREGISTER_INTERNAL_CLASS(NativeEditorPlugin);
}

void gn_editor_plugin() {
	EditorPlugins::add_by_type<NativeEditorPlugin>();
}
