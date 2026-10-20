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
	void (*call)(void *self, const Variant **args, Variant &ret);
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
	void *(*create)(Object *owner);
	void (*destroy)(void *self);
	const Method *methods;
	int method_count;
	const Prop *props;
	int prop_count;
	const char *parent;
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
	if (got != t && !numeric) {
		fail("Trying to assign a value of the wrong type.", loc);
	}
	return v.operator T();
}

} // namespace gn

#define GN_L(line) (gn::Loc{ GN_FILE, __func__, line })
#define GN_SN(s) ([]() -> const godot::StringName & { static const godot::StringName *n = new godot::StringName(s); return *n; }())
