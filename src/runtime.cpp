#include <runtime.h>

#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/error_macros.hpp>

#include <cstring>
#include <vector>

using namespace godot;

NativeLanguage *NativeLanguage::singleton = nullptr;

namespace {

std::vector<const gn::Class *> &registry() {
	static std::vector<const gn::Class *> r;
	return r;
}

const gn::Class *find_class(const String &path) {
	String p = path.get_extension() == "gdc" ? path.get_basename() + ".gd" : path;
	CharString c = p.utf8();
	for (const gn::Class *k : registry()) {
		if (strcmp(k->path, c.get_data()) == 0) {
			return k;
		}
	}
	return nullptr;
}

struct Instance {
	Ref<NativeScript> script;
	Object *owner;
	void *data;
};

GDExtensionBool i_set(void *self, const void *name, const void *value) {
	Instance *i = (Instance *)self;
	auto it = i->script->props.find(*(const StringName *)name);
	if (it == i->script->props.end()) {
		return false;
	}
	it->value->set(i->data, *(const Variant *)value);
	return true;
}

GDExtensionBool i_get(void *self, const void *name, void *ret) {
	Instance *i = (Instance *)self;
	auto it = i->script->props.find(*(const StringName *)name);
	if (it == i->script->props.end()) {
		return false;
	}
	it->value->get(i->data, *(Variant *)ret);
	return true;
}

const GDExtensionPropertyInfo *i_props(void *self, uint32_t *count) {
	const gn::Class *c = ((Instance *)self)->script->info;
	GDExtensionPropertyInfo *out = memnew_arr(GDExtensionPropertyInfo, c->prop_count + 1);
	for (int n = 0; n < c->prop_count; n++) {
		out[n].type = c->props[n].type;
		out[n].name = memnew(StringName(c->props[n].name));
		out[n].class_name = memnew(StringName);
		out[n].hint = 0;
		out[n].hint_string = memnew(String);
		out[n].usage = PROPERTY_USAGE_SCRIPT_VARIABLE | PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_EDITOR;
	}
	*count = c->prop_count;
	return out;
}

void i_free_props(void *, const GDExtensionPropertyInfo *list, uint32_t count) {
	for (uint32_t n = 0; n < count; n++) {
		memdelete((StringName *)list[n].name);
		memdelete((StringName *)list[n].class_name);
		memdelete((String *)list[n].hint_string);
	}
	memdelete_arr((GDExtensionPropertyInfo *)list);
}

GDExtensionBool i_has_method(void *self, const void *name) {
	return ((Instance *)self)->script->methods.has(*(const StringName *)name);
}

void i_call(void *self, const void *method, const void *const *args, GDExtensionInt argc, void *ret, GDExtensionCallError *error) {
	Instance *i = (Instance *)self;
	auto it = i->script->methods.find(*(const StringName *)method);
	if (it == i->script->methods.end()) {
		error->error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
		return;
	}
	const gn::Method *m = it->value;
	if (argc != m->argc) {
		error->error = argc < m->argc ? GDEXTENSION_CALL_ERROR_TOO_FEW_ARGUMENTS : GDEXTENSION_CALL_ERROR_TOO_MANY_ARGUMENTS;
		error->expected = m->argc;
		return;
	}
	error->error = GDEXTENSION_CALL_OK;
	m->call(i->data, (const Variant **)args, *(Variant *)ret);
}

void i_notification(void *, int32_t, GDExtensionBool) {}

GDExtensionBool i_refcount_dec(void *) { return true; }
void i_refcount_inc(void *) {}
void *i_owner(void *self) { return ((Instance *)self)->owner->_owner; }
void *i_script(void *self) { return ((Instance *)self)->script->_owner; }
GDExtensionBool i_placeholder(void *) { return false; }
void *i_language(void *) { return NativeLanguage::singleton->_owner; }

void i_free(void *self) {
	Instance *i = (Instance *)self;
	i->script->info->destroy(i->data);
	memdelete(i);
}

GDExtensionScriptInstanceInfo3 make_info() {
	GDExtensionScriptInstanceInfo3 s = {};
	s.set_func = i_set;
	s.get_func = i_get;
	s.get_property_list_func = i_props;
	s.free_property_list_func = i_free_props;
	s.has_method_func = i_has_method;
	s.call_func = i_call;
	s.notification_func = i_notification;
	s.refcount_incremented_func = i_refcount_inc;
	s.refcount_decremented_func = i_refcount_dec;
	s.get_owner_func = i_owner;
	s.get_script_func = i_script;
	s.is_placeholder_func = i_placeholder;
	s.get_language_func = i_language;
	s.free_func = i_free;
	return s;
}

const GDExtensionScriptInstanceInfo3 instance_info = make_info();

} // namespace

namespace gn {

Registrar::Registrar(const Class *c) {
	registry().push_back(c);
}

void fail(const char *msg, const Loc &loc) {
	_err_print_error(loc.func, loc.file, loc.line, msg, false, false);
	throw Abort{};
}

Variant op(Variant::Operator o, const Variant &a, const Variant &b, const Loc &loc) {
	Variant r;
	bool valid = false;
	Variant::evaluate(o, a, b, r, valid);
	if (!valid) {
		fail("Invalid operands.", loc);
	}
	return r;
}

Variant mcall(const Variant &base, const StringName &name, std::initializer_list<Variant> args, const Loc &loc) {
	Variant self = base;
	std::vector<const Variant *> ptrs;
	for (const Variant &a : args) {
		ptrs.push_back(&a);
	}
	Variant r;
	GDExtensionCallError e = {};
	gdextension_interface::variant_call(self._native_ptr(), name._native_ptr(), (const void *const *)ptrs.data(), ptrs.size(), r._native_ptr(), &e);
	if (e.error != GDEXTENSION_CALL_OK) {
		fail("Invalid call.", loc);
	}
	return r;
}

Variant get_named(const Variant &base, const StringName &name, const Loc &loc) {
	bool valid = false;
	Variant r = base.get_named(name, valid);
	if (!valid) {
		fail("Invalid access to property.", loc);
	}
	return r;
}

Variant get_index(const Variant &base, const Variant &index, const Loc &loc) {
	bool valid = false;
	Variant r = base.get(index, &valid);
	if (!valid) {
		fail("Out of bounds get index.", loc);
	}
	return r;
}

void set_named(Variant &base, const StringName &name, const Variant &value, const Loc &loc) {
	bool valid = false;
	base.set_named(name, value, valid);
	if (!valid) {
		fail("Invalid assignment to property.", loc);
	}
}

Variant construct(Variant::Type type, std::initializer_list<Variant> args, const Loc &loc) {
	std::vector<const Variant *> ptrs;
	for (const Variant &a : args) {
		ptrs.push_back(&a);
	}
	Variant r;
	GDExtensionCallError e = {};
	gdextension_interface::variant_construct((GDExtensionVariantType)type, r._native_ptr(), (const void *const *)ptrs.data(), ptrs.size(), &e);
	if (e.error != GDEXTENSION_CALL_OK) {
		fail("Invalid constructor arguments.", loc);
	}
	return r;
}

Variant instantiate(const StringName &cls) {
	return ClassDBSingleton::get_singleton()->instantiate(cls);
}

Variant singleton(const StringName &name) {
	return Engine::get_singleton()->call(GN_SN("get_singleton"), name);
}

Variant load(const String &path) {
	return ResourceLoader::get_singleton()->call(GN_SN("load"), path);
}

void set_index(Variant &base, const Variant &index, const Variant &value, const Loc &loc) {
	bool valid = false;
	base.set(index, value, &valid);
	if (!valid) {
		fail("Out of bounds set index.", loc);
	}
}

} // namespace gn

void NativeScript::_bind_methods() {
	ClassDB::bind_vararg_method(METHOD_FLAGS_DEFAULT, "new", &NativeScript::new_);
}

void NativeScript::setup(const gn::Class *p_info) {
	info = p_info;
	for (int n = 0; n < info->method_count; n++) {
		methods.insert(info->methods[n].name, &info->methods[n]);
	}
	for (int n = 0; n < info->prop_count; n++) {
		props.insert(info->props[n].name, &info->props[n]);
	}
	take_over_path(info->path);
}

Variant NativeScript::new_(const Variant **, GDExtensionInt, GDExtensionCallError &) {
	Variant instance = ClassDBSingleton::get_singleton()->instantiate(info->base);
	Object *o = instance;
	ERR_FAIL_NULL_V(o, Variant());
	o->set_script(this);
	return instance;
}

Ref<Script> NativeScript::_get_base_script() const {
	if (info->parent && base_script.is_null()) {
		base_script = ResourceLoader::get_singleton()->load(info->parent);
	}
	return base_script;
}

bool NativeScript::_inherits_script(const Ref<Script> &s) const {
	for (Ref<Script> c = Ref<Script>(const_cast<NativeScript *>(this)); c.is_valid(); c = c->get_base_script()) {
		if (c == s) {
			return true;
		}
	}
	return false;
}

void *NativeScript::_instance_create(Object *owner) const {
	Instance *i = memnew(Instance);
	i->script = Ref<NativeScript>(const_cast<NativeScript *>(this));
	i->owner = owner;
	i->data = info->create(owner);
	return gdextension_interface::script_instance_create3(&instance_info, i);
}

TypedArray<Dictionary> NativeScript::_get_script_method_list() const {
	TypedArray<Dictionary> r;
	for (int n = 0; n < info->method_count; n++) {
		Dictionary d;
		d["name"] = info->methods[n].name;
		r.push_back(d);
	}
	return r;
}

TypedArray<Dictionary> NativeScript::_get_script_property_list() const {
	TypedArray<Dictionary> r;
	for (int n = 0; n < info->prop_count; n++) {
		Dictionary d;
		d["name"] = info->props[n].name;
		d["type"] = (int)info->props[n].type;
		d["usage"] = (int)(PROPERTY_USAGE_SCRIPT_VARIABLE | PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_EDITOR);
		r.push_back(d);
	}
	return r;
}

TypedArray<StringName> NativeScript::_get_members() const {
	TypedArray<StringName> r;
	for (int n = 0; n < info->prop_count; n++) {
		r.push_back(StringName(info->props[n].name));
	}
	return r;
}

bool NativeLoader::_recognize_path(const String &path, const StringName &) const {
	return find_class(path) != nullptr;
}

String NativeLoader::_get_resource_type(const String &path) const {
	return find_class(path) ? "NativeScript" : "";
}

bool NativeLoader::_exists(const String &path) const {
	return find_class(path) != nullptr;
}

Variant NativeLoader::_load(const String &path, const String &original, bool, int32_t) const {
	const gn::Class *c = find_class(original.is_empty() ? path : original);
	if (!c) {
		return Variant();
	}
	Ref<NativeScript> s;
	s.instantiate();
	s->setup(c);
	return s;
}
