#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <cstdint>
#include <initializer_list>

namespace gn {

using namespace godot;

struct Abort {};

struct Loc {
	const char *file;
	const char *func;
	int line;
};

struct Method {
	const char *name;
	int argc;
	int min_argc;
	void (*call)(void *self, const Variant **args, int count, Variant &ret);
};

struct Prop {
	const char *name;
	GDExtensionVariantType type;
	void (*get)(void *self, Variant &ret);
	void (*set)(void *self, const Variant &value);
};

struct Class {
	const char *path;
	const char *base;
	const char *global_name;
	void *(*create)(Object *owner, const Variant **args, int count);
	void (*destroy)(void *self);
	const Method *methods;
	int method_count;
	const Prop *props;
	int prop_count;
	const char *parent;
	const char *const *signals;
	int signal_count;
};

struct Registrar {
	explicit Registrar(const Class *c);
};

[[noreturn]] void fail(const char *msg, const Loc &loc);
Variant op(Variant::Operator o, const Variant &a, const Variant &b, const Loc &loc);
Variant mcall(const Variant &base, const StringName &name, std::initializer_list<Variant> args, const Loc &loc);
Variant get_named(const Variant &base, const StringName &name, const Loc &loc);
Variant get_index(const Variant &base, const Variant &index, const Loc &loc);
void set_index(Variant &base, const Variant &index, const Variant &value, const Loc &loc);
void set_named(Variant &base, const StringName &name, const Variant &value, const Loc &loc);
Variant construct(Variant::Type type, std::initializer_list<Variant> args, const Loc &loc);
Variant instantiate(const StringName &cls);
Variant singleton(const StringName &name);
Variant load(const String &path);
Variant static_call(const StringName &cls, const StringName &method, std::initializer_list<Variant> args, const Loc &loc);
Variant builtin_static(Variant::Type type, const StringName &method, std::initializer_list<Variant> args, const Loc &loc);
Variant builtin_constant(Variant::Type type, const StringName &name);
Variant check_builtin(const Variant &v, Variant::Type type, const Loc &loc);
Variant check_class(const Variant &v, const StringName &cls, const Loc &loc);
Variant check_script(const Variant &v, const char *path, const Loc &loc);
bool is_builtin(const Variant &v, Variant::Type type);
bool is_class(const Variant &v, const StringName &cls);
bool is_script(const Variant &v, const char *path);
Variant as_class(const Variant &v, const StringName &cls);
Variant as_script(const Variant &v, const char *path);
bool match_value(const Variant &value, const Variant &pattern, const Loc &loc);
bool match_array(const Variant &value, int64_t size, bool rest);
bool match_dict(const Variant &value, int64_t size, bool rest);
bool has_key(const Variant &value, const Variant &key);
Array iter_list(const Variant &v, const Loc &loc);
int64_t len(const Variant &v, const Loc &loc);
Array range(std::initializer_list<int64_t> args, const Loc &loc);
String chr(int64_t code);
int64_t ord(const String &s);
Variant color8(int64_t r, int64_t g, int64_t b, int64_t a);
Variant instance_from_id(int64_t id);
bool is_instance_of(const Variant &v, const Variant &type);
bool is_instance_valid(const Variant &v);
bool type_exists(const StringName &name);

inline int64_t add(int64_t a, int64_t b) { return int64_t(uint64_t(a) + uint64_t(b)); }
inline int64_t sub(int64_t a, int64_t b) { return int64_t(uint64_t(a) - uint64_t(b)); }
inline int64_t mul(int64_t a, int64_t b) { return int64_t(uint64_t(a) * uint64_t(b)); }
inline int64_t neg(int64_t a) { return int64_t(0 - uint64_t(a)); }

inline int64_t idiv(int64_t a, int64_t b, const Loc &loc) {
	if (b == 0) {
		fail("Division by zero error.", loc);
	}
	return (a == INT64_MIN && b == -1) ? a : a / b;
}

inline int64_t imod(int64_t a, int64_t b, const Loc &loc) {
	if (b == 0) {
		fail("Modulo by zero error.", loc);
	}
	return b == -1 ? 0 : a % b;
}

inline int64_t index(int64_t i, int64_t size, const Loc &loc) {
	int64_t r = i < 0 ? i + size : i;
	if (r < 0 || r >= size) {
		fail("Out of bounds get index.", loc);
	}
	return r;
}

template <typename T>
inline T cast(const Variant &v, Variant::Type t, const Loc &loc) {
	Variant::Type got = v.get_type();
	bool numeric = (t == Variant::INT || t == Variant::FLOAT) && (got == Variant::INT || got == Variant::FLOAT);
	if (got != t && !numeric && !Variant::can_convert_strict(got, t)) {
		fail("Trying to assign a value of the wrong type.", loc);
	}
	return v.operator T();
}

template <typename T>
inline T builtin_constant_as(Variant::Type type, const StringName &name) {
	return builtin_constant(type, name).operator T();
}

template <>
inline Variant builtin_constant_as<Variant>(Variant::Type type, const StringName &name) {
	return builtin_constant(type, name);
}

} // namespace gn

#define GN_L(line) (gn::Loc{ GN_FILE, __func__, line })
#define GN_SN(s) ([]() -> const godot::StringName & { static const godot::StringName *n = new godot::StringName(s); return *n; }())
#define GN_BC(T, type, name) ([]() -> const T & { static const T *v = new T(gn::builtin_constant_as<T>(type, GN_SN(name))); return *v; }())
