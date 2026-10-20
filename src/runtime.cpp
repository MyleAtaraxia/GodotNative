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
	try {
		it->value->set(i->data, *(const Variant *)value);
	} catch (const gn::Abort &) {
	}
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
	if (argc < m->min_argc || argc > m->argc) {
		error->error = argc < m->min_argc ? GDEXTENSION_CALL_ERROR_TOO_FEW_ARGUMENTS : GDEXTENSION_CALL_ERROR_TOO_MANY_ARGUMENTS;
		error->expected = argc < m->min_argc ? m->min_argc : m->argc;
		return;
	}
	error->error = GDEXTENSION_CALL_OK;
	try {
		m->call(i->data, (const Variant **)args, int(argc), *(Variant *)ret);
	} catch (const gn::Abort &) {
	}
}

void i_notification(void *self, int32_t what, GDExtensionBool) {
	Instance *i = (Instance *)self;
	auto it = i->script->methods.find(GN_SN("_notification"));
	if (it == i->script->methods.end()) {
		return;
	}
	Variant arg = what, ret;
	const Variant *args[] = { &arg };
	try {
		it->value->call(i->data, args, 1, ret);
	} catch (const gn::Abort &) {
	}
}

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

static void check_live(const Variant &base, const Loc &loc) {
	if (base.get_type() == Variant::OBJECT && !is_instance_valid(base)) {
		fail("Cannot use a previously freed instance.", loc);
	}
}

Variant mcall(const Variant &base, const StringName &name, std::initializer_list<Variant> args, const Loc &loc) {
	check_live(base, loc);
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
	check_live(base, loc);
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

GDExtensionMethodBindPtr method_bind(const char *cls, const char *name, int64_t hash) {
	return gdextension_interface::classdb_get_method_bind(StringName(cls)._native_ptr(), StringName(name)._native_ptr(), hash);
}

Variant static_call(GDExtensionMethodBindPtr mb, std::initializer_list<Variant> args, const Loc &loc) {
	std::vector<GDExtensionConstVariantPtr> ptrs;
	for (const Variant &a : args) {
		ptrs.push_back(a._native_ptr());
	}
	Variant r;
	GDExtensionCallError e = {};
	gdextension_interface::object_method_bind_call(mb, nullptr, ptrs.data(), ptrs.size(), r._native_ptr(), &e);
	if (e.error != GDEXTENSION_CALL_OK) {
		fail("Invalid static call.", loc);
	}
	return r;
}

void *raw_object(const Variant &v) {
	static const GDExtensionTypeFromVariantConstructorFunc read = gdextension_interface::get_variant_to_type_constructor(GDEXTENSION_VARIANT_TYPE_OBJECT);
	GDExtensionObjectPtr o = nullptr;
	read(&o, const_cast<Variant &>(v)._native_ptr());
	return o;
}

void *live_object(const Variant &v, const Loc &loc) {
	if (!is_instance_valid(v)) {
		fail(v.get_type() == Variant::NIL ? "Cannot call a method on a null value." : "Cannot call a method on a previously freed instance.", loc);
	}
	return raw_object(v);
}

Variant builtin_static(Variant::Type type, const StringName &method, std::initializer_list<Variant> args, const Loc &loc) {
	std::vector<const Variant *> ptrs;
	for (const Variant &a : args) {
		ptrs.push_back(&a);
	}
	Variant r;
	GDExtensionCallError e = {};
	Variant::callp_static(type, method, ptrs.data(), ptrs.size(), r, e);
	if (e.error != GDEXTENSION_CALL_OK) {
		fail("Invalid static call.", loc);
	}
	return r;
}

Variant builtin_constant(Variant::Type type, const StringName &name) {
	Variant r;
	gdextension_interface::variant_get_constant_value((GDExtensionVariantType)type, name._native_ptr(), r._native_ptr());
	return r;
}

bool is_builtin(const Variant &v, Variant::Type type) {
	return v.get_type() == type;
}

bool is_class(const Variant &v, const StringName &cls) {
	if (v.get_type() != Variant::OBJECT || !v.booleanize()) {
		return false;
	}
	Variant self = v;
	return self.call(GN_SN("is_class"), cls).booleanize();
}

bool is_script(const Variant &v, const char *path) {
	if (v.get_type() != Variant::OBJECT || !v.booleanize()) {
		return false;
	}
	Variant self = v;
	Variant script = self.call(GN_SN("get_script"));
	String want = path;
	while (script.get_type() == Variant::OBJECT && script.booleanize()) {
		bool valid = false;
		if (String(script.get_named(GN_SN("resource_path"), valid)) == want) {
			return true;
		}
		script = script.call(GN_SN("get_base_script"));
	}
	return false;
}

Variant check_builtin(const Variant &v, Variant::Type type, const Loc &loc) {
	if (v.get_type() == type) {
		return v;
	}
	if (!Variant::can_convert_strict(v.get_type(), type)) {
		fail("Trying to assign a value of the wrong type.", loc);
	}
	return construct(type, { v }, loc);
}

Variant check_class(const Variant &v, const StringName &cls, const Loc &loc) {
	if (v.get_type() == Variant::NIL) {
		return v;
	}
	if (!is_class(v, cls)) {
		fail("Trying to assign a value of the wrong class.", loc);
	}
	return v;
}

Variant check_script(const Variant &v, const char *path, const Loc &loc) {
	if (v.get_type() == Variant::NIL) {
		return v;
	}
	if (!is_script(v, path)) {
		fail("Trying to assign a value of the wrong class.", loc);
	}
	return v;
}

Variant as_class(const Variant &v, const StringName &cls) {
	return is_class(v, cls) ? v : Variant();
}

Variant as_script(const Variant &v, const char *path) {
	return is_script(v, path) ? v : Variant();
}

bool match_value(const Variant &value, const Variant &pattern, const Loc &loc) {
	Variant::Type a = value.get_type(), b = pattern.get_type();
	bool strings = (a == Variant::STRING || a == Variant::STRING_NAME) && (b == Variant::STRING || b == Variant::STRING_NAME);
	if (a != b && !strings) {
		return false;
	}
	return op(Variant::OP_EQUAL, value, pattern, loc).booleanize();
}

bool match_array(const Variant &value, int64_t size, bool rest) {
	if (value.get_type() != Variant::ARRAY) {
		return false;
	}
	int64_t n = Array(value).size();
	return rest ? n >= size : n == size;
}

bool match_dict(const Variant &value, int64_t size, bool rest) {
	if (value.get_type() != Variant::DICTIONARY) {
		return false;
	}
	int64_t n = Dictionary(value).size();
	return rest ? n >= size : n == size;
}

bool has_key(const Variant &value, const Variant &key) {
	return Dictionary(value).has(key);
}

Array iter_list(const Variant &v, const Loc &loc) {
	switch (v.get_type()) {
		case Variant::ARRAY:
			return v;
		case Variant::DICTIONARY:
			return Dictionary(v).keys();
		case Variant::INT: {
			Array r;
			for (int64_t i = 0; i < int64_t(v); i++) {
				r.push_back(i);
			}
			return r;
		}
		case Variant::STRING:
		case Variant::STRING_NAME: {
			Array r;
			String s = v;
			for (int64_t i = 0; i < s.length(); i++) {
				r.push_back(s.substr(i, 1));
			}
			return r;
		}
		default:
			break;
	}
	if (v.get_type() >= Variant::PACKED_BYTE_ARRAY) {
		return construct(Variant::ARRAY, { v }, loc);
	}
	fail("Unable to iterate on this value.", loc);
}

int64_t len(const Variant &v, const Loc &loc) {
	switch (v.get_type()) {
		case Variant::STRING:
		case Variant::STRING_NAME:
		case Variant::NODE_PATH:
			return String(v).length();
		case Variant::OBJECT:
			fail("Object can't be used with len().", loc);
		default:
			return int64_t(mcall(v, GN_SN("size"), {}, loc));
	}
}

Array range(std::initializer_list<int64_t> args, const Loc &loc) {
	std::vector<int64_t> a(args);
	int64_t from = a.size() > 1 ? a[0] : 0, to = a.size() > 1 ? a[1] : a[0], step = a.size() > 2 ? a[2] : 1;
	if (step == 0) {
		fail("Step argument is zero!", loc);
	}
	Array r;
	for (int64_t i = from; step > 0 ? i < to : i > to; i += step) {
		r.push_back(i);
	}
	return r;
}

String chr(int64_t code) {
	return String::chr(code);
}

int64_t ord(const String &s) {
	return s.is_empty() ? 0 : s.unicode_at(0);
}

Variant color8(int64_t r, int64_t g, int64_t b, int64_t a) {
	return Color::from_rgba8(r, g, b, a);
}

Variant instance_from_id(int64_t id) {
	static const GDExtensionVariantFromTypeConstructorFunc make = gdextension_interface::get_variant_from_type_constructor(GDEXTENSION_VARIANT_TYPE_OBJECT);
	GDExtensionObjectPtr o = gdextension_interface::object_get_instance_from_id(id);
	Variant r;
	if (o) {
		make(r._native_ptr(), &o);
	}
	return r;
}

bool type_exists(const StringName &name) {
	return ClassDBSingleton::get_singleton()->class_exists(name);
}

bool is_instance_valid(const Variant &v) {
	static const GDExtensionPtrUtilityFunction fn = gdextension_interface::variant_get_ptr_utility_function(GN_SN("is_instance_valid")._native_ptr(), 996128841);
	bool r = false;
	const void *args[] = { v._native_ptr() };
	fn(&r, args, 1);
	return r;
}

bool is_instance_of(const Variant &v, const Variant &type) {
	if (type.get_type() == Variant::INT) {
		return v.get_type() == Variant::Type(int64_t(type));
	}
	if (type.get_type() != Variant::OBJECT || !type.booleanize()) {
		return false;
	}
	bool valid = false;
	return is_script(v, String(type.get_named(GN_SN("resource_path"), valid)).utf8().get_data());
}

void set_named(Variant &base, const StringName &name, const Variant &value, const Loc &loc) {
	check_live(base, loc);
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

static thread_local const Variant **pending_args = nullptr;
static thread_local int pending_count = 0;

Variant NativeScript::new_(const Variant **args, GDExtensionInt argc, GDExtensionCallError &) {
	pending_args = args;
	pending_count = int(argc);
	Variant instance = ClassDBSingleton::get_singleton()->instantiate(info->base);
	Object *o = instance;
	ERR_FAIL_NULL_V(o, Variant());
	o->set_script(this);
	pending_args = nullptr;
	pending_count = 0;
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
	const Variant **args = pending_args;
	int count = pending_count;
	pending_args = nullptr;
	pending_count = 0;
	i->data = info->create(owner, args, count);
	return gdextension_interface::script_instance_create3(&instance_info, i);
}

bool NativeScript::_has_script_signal(const StringName &name) const {
	for (int n = 0; n < info->signal_count; n++) {
		if (name == StringName(info->signals[n])) {
			return true;
		}
	}
	return false;
}

TypedArray<Dictionary> NativeScript::_get_script_signal_list() const {
	TypedArray<Dictionary> r;
	for (int n = 0; n < info->signal_count; n++) {
		Dictionary d;
		d["name"] = info->signals[n];
		d["args"] = Array();
		r.push_back(d);
	}
	return r;
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
