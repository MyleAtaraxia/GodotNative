#pragma once

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>

#include <atomic>
#include <coroutine>
#include <functional>
#include <memory>
#include <thread>

#ifdef _OPENMP
#include <omp.h>
#endif

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/variant_internal.hpp>

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <type_traits>

namespace gn {

using namespace godot;

struct Abort {};

// A thread that fails skips the rest of a parallel block, so waiting has to end on failure instead of on a full count.
class TeamBarrier {
	std::atomic<int> arrived{ 0 };
	std::atomic<int> round{ 0 };

public:
	void wait(const std::atomic<bool> &failed) {
#ifdef _OPENMP
		const int team = omp_get_num_threads();
#else
		const int team = 1;
#endif
		const int current = round.load();
		if (arrived.fetch_add(1) + 1 == team) {
			arrived.store(0);
			round.fetch_add(1);
		}
		while (round.load() == current && !failed.load()) {
			std::this_thread::yield();
		}
		if (failed.load()) {
			throw Abort{};
		}
	}
};

// A Variant holds packed arrays by reference, so keeping one here shares writes the way GDScript does.
template <typename A>
class Packed : public Variant {
	A *items;

	void bind() { items = VariantInternal::get_internal_value<A>(static_cast<Variant *>(this)); }

public:
	Packed() :
			Variant(A()) { bind(); }
	Packed(const A &value) :
			Variant(value) { bind(); }
	Packed(const Variant &value) :
			Variant(value.get_type() == internal::VariantInternalType<A>::type ? value : Variant(value.operator A())) { bind(); }

	operator const A &() const { return *items; }
	const A &array() const { return *items; }
	int64_t size() const { return items->size(); }
	int64_t resize(int64_t count) const { return items->resize(count); }
	template <typename T>
	bool push_back(T value) const { return items->push_back(value); }
	template <typename T>
	bool append(T value) const { return items->append(value); }
	auto ptr() const { return const_cast<const A *>(items)->ptr(); }
	auto ptrw() const { return items->ptrw(); }
	Packed duplicate() const { return Packed(items->duplicate()); }
};

using PackedFloats = Packed<PackedFloat64Array>;
using PackedInts = Packed<PackedInt64Array>;

template <typename T>
struct RawType {
	using type = T;
};
template <typename A>
struct RawType<Packed<A>> {
	using type = A;
};

template <typename T>
inline const void *raw_arg(const T &value) {
	return &value;
}
template <typename A>
inline const void *raw_arg(const Packed<A> &value) {
	return &value.array();
}

inline int64_t loop_count(int64_t from, int64_t to, int64_t step) {
	if (step > 0) {
		return to > from ? (to - from - 1) / step + 1 : 0;
	}
	if (step < 0) {
		return to < from ? (from - to - 1) / -step + 1 : 0;
	}
	return 0;
}

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
	bool is_static;
};

struct Prop {
	const char *name;
	GDExtensionVariantType type;
	void (*get)(void *self, Variant &ret);
	void (*set)(void *self, const Variant &value);
	uint32_t usage;
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
	bool is_abstract;
	const Prop *statics;
	int static_count;
	void (*static_init)();
	void (*static_clear)();
	void (*constants)(Dictionary &out);
};

struct Registrar {
	explicit Registrar(const Class *c);
};

[[noreturn]] void fail(const char *msg, const Loc &loc);

// shortcut: reads the engine's Variant layout (type at 0, payload at 8, object id at 8 and pointer at 16), the 4.4 and 4.7 suites cover it, recheck if a Godot version changes Variant
template <typename T>
inline T raw_read(const Variant &v, size_t offset = 8) {
	T r;
	std::memcpy(&r, reinterpret_cast<const uint8_t *>(v._native_ptr()) + offset, sizeof(T));
	return r;
}

inline int32_t raw_type(const Variant &v) {
	return raw_read<int32_t>(v, 0);
}

template <typename T>
inline Variant raw_make(Variant::Type type, const T &value) {
	Variant r;
	int32_t tag = type;
	std::memcpy(r._native_ptr(), &tag, sizeof(tag));
	std::memcpy(reinterpret_cast<uint8_t *>(r._native_ptr()) + 8, &value, sizeof(T));
	return r;
}

Variant evaluate(Variant::Operator o, const Variant &a, const Variant &b, const Loc &loc);

inline Variant op(Variant::Operator o, const Variant &a, const Variant &b, const Loc &loc) {
	int32_t ta = raw_type(a), tb = raw_type(b);
	if (ta == Variant::INT && tb == Variant::INT) {
		int64_t x = raw_read<int64_t>(a), y = raw_read<int64_t>(b);
		switch (o) {
			case Variant::OP_ADD: return raw_make(Variant::INT, int64_t(uint64_t(x) + uint64_t(y)));
			case Variant::OP_SUBTRACT: return raw_make(Variant::INT, int64_t(uint64_t(x) - uint64_t(y)));
			case Variant::OP_MULTIPLY: return raw_make(Variant::INT, int64_t(uint64_t(x) * uint64_t(y)));
			case Variant::OP_LESS: return raw_make(Variant::BOOL, x < y);
			case Variant::OP_GREATER: return raw_make(Variant::BOOL, x > y);
			case Variant::OP_LESS_EQUAL: return raw_make(Variant::BOOL, x <= y);
			case Variant::OP_GREATER_EQUAL: return raw_make(Variant::BOOL, x >= y);
			case Variant::OP_EQUAL: return raw_make(Variant::BOOL, x == y);
			case Variant::OP_NOT_EQUAL: return raw_make(Variant::BOOL, x != y);
			default: break;
		}
	} else if ((ta == Variant::INT || ta == Variant::FLOAT) && (tb == Variant::INT || tb == Variant::FLOAT)) {
		double x = ta == Variant::INT ? double(raw_read<int64_t>(a)) : raw_read<double>(a);
		double y = tb == Variant::INT ? double(raw_read<int64_t>(b)) : raw_read<double>(b);
		switch (o) {
			case Variant::OP_ADD: return raw_make(Variant::FLOAT, x + y);
			case Variant::OP_SUBTRACT: return raw_make(Variant::FLOAT, x - y);
			case Variant::OP_MULTIPLY: return raw_make(Variant::FLOAT, x * y);
			case Variant::OP_DIVIDE: return raw_make(Variant::FLOAT, x / y);
			case Variant::OP_LESS: return raw_make(Variant::BOOL, x < y);
			case Variant::OP_GREATER: return raw_make(Variant::BOOL, x > y);
			case Variant::OP_LESS_EQUAL: return raw_make(Variant::BOOL, x <= y);
			case Variant::OP_GREATER_EQUAL: return raw_make(Variant::BOOL, x >= y);
			case Variant::OP_EQUAL: return raw_make(Variant::BOOL, x == y);
			case Variant::OP_NOT_EQUAL: return raw_make(Variant::BOOL, x != y);
			default: break;
		}
	}
	return evaluate(o, a, b, loc);
}

Variant mcall(const Variant &base, const StringName &name, std::initializer_list<Variant> args, const Loc &loc);
Variant get_named(const Variant &base, const StringName &name, const Loc &loc);
Variant get_index(const Variant &base, const Variant &index, const Loc &loc);
void set_index(Variant &base, const Variant &index, const Variant &value, const Loc &loc);
void set_named(Variant &base, const StringName &name, const Variant &value, const Loc &loc);
Variant construct(Variant::Type type, std::initializer_list<Variant> args, const Loc &loc);
Variant instantiate(const StringName &cls);
Variant singleton(const StringName &name);
Variant load(const String &path);
const Variant *keep(Variant *constant);
template <typename T>
const T *keep(T *constant) {
	return constant;
}
void release_constants();
Variant native_script(const char *path);
Object *script_object(const char *path);
Variant static_call(GDExtensionMethodBindPtr mb, std::initializer_list<Variant> args, const Loc &loc);
Variant dynamic_static(const char *cls, const char *name, std::initializer_list<Variant> args);
GDExtensionMethodBindPtr method_bind(const char *cls, const char *name, int64_t hash);
GDExtensionPtrBuiltInMethod builtin_method(Variant::Type type, const char *name, int64_t hash);

inline void *raw_object(const Variant &v) {
	return raw_read<void *>(v, 16);
}

inline bool is_instance_valid(const Variant &v) {
	if (raw_type(v) != Variant::OBJECT) {
		return false;
	}
	uint64_t id = raw_read<uint64_t>(v);
	bool counted = id >> 63;
	return id != 0 && (counted || gdextension_interface::object_get_instance_from_id(id) != nullptr);
}

inline void *live_object(const Variant &v, const Loc &loc) {
	if (!is_instance_valid(v)) {
		fail(v.get_type() == Variant::NIL ? "Cannot call a method on a null value." : "Cannot call a method on a previously freed instance.", loc);
	}
	return raw_object(v);
}

Variant builtin_static(Variant::Type type, const StringName &method, std::initializer_list<Variant> args, const Loc &loc);
Variant builtin_constant(Variant::Type type, const StringName &name);
Variant check_builtin(const Variant &v, Variant::Type type, const Loc &loc);
Variant check_class(const Variant &v, const StringName &cls, const Loc &loc);
Variant check_script(const Variant &v, const char *path, const Loc &loc);
Array typed_array(const Array &source, Variant::Type type, const StringName &cls, const char *script, const Loc &loc);
Array check_array(const Array &value, Variant::Type type, const StringName &cls, const char *script, const Loc &loc);
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
Variant lambda(Object *owner, int required, int total, std::function<Variant(const Variant **, int)> body);
int64_t len(const Variant &v, const Loc &loc);
Array range(std::initializer_list<int64_t> args, const Loc &loc);
String chr(int64_t code);
int64_t ord(const String &s);
Variant color8(int64_t r, int64_t g, int64_t b, int64_t a);
Variant instance_from_id(int64_t id);
bool is_instance_of(const Variant &v, const Variant &type);
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

template <typename T>
inline int64_t wrap(int64_t i, const T &list) {
	return i < 0 ? i + list.size() : i;
}

inline int64_t index(int64_t i, int64_t size, const Loc &loc) {
	int64_t r = i < 0 ? i + size : i;
	if (r < 0 || r >= size) {
		fail("Out of bounds get index.", loc);
	}
	return r;
}

template <typename T>
inline constexpr bool in_variant = std::is_same_v<T, bool> || std::is_same_v<T, int64_t> || std::is_same_v<T, double> || std::is_same_v<T, Vector2> || std::is_same_v<T, Vector3>;

template <typename T>
inline T cast(const Variant &v, Variant::Type t, const Loc &loc) {
	if constexpr (in_variant<T>) {
		if (raw_type(v) == t) {
			return raw_read<T>(v);
		}
	}
	Variant::Type got = v.get_type();
	bool numeric = (t == Variant::INT || t == Variant::FLOAT) && (got == Variant::INT || got == Variant::FLOAT);
	if (got != t && !numeric && !Variant::can_convert_strict(got, t)) {
		fail("Trying to assign a value of the wrong type.", loc);
	}
	if (got != t && !numeric && got != Variant::NIL && got != Variant::OBJECT) {
		return cast<T>(construct(t, { v }, loc), t, loc);
	}
	if constexpr (std::is_constructible_v<T, const Variant &> && std::is_base_of_v<Variant, T> && !std::is_same_v<T, Variant>) {
		return T(v);
	} else {
		return v.operator T();
	}
}

struct Task {
	struct promise_type {
		Variant result;
		Ref<RefCounted> state;
		std::shared_ptr<void> keep;
		Task get_return_object() { return { std::coroutine_handle<promise_type>::from_promise(*this) }; }
		std::suspend_never initial_suspend() noexcept { return {}; }
		std::suspend_always final_suspend() noexcept { return {}; }
		void return_value(const Variant &v) { result = v; }
		void unhandled_exception() {}
	};
	std::coroutine_handle<promise_type> handle;
};

struct Wait {
	Object *owner;
	Variant value;
	Variant result;
	bool await_ready();
	void await_suspend(std::coroutine_handle<Task::promise_type> handle);
	Variant await_resume() { return result; }
};

inline Wait wait(Object *owner, const Variant &value) {
	return { owner, value, Variant() };
}

Variant run(Task task, std::shared_ptr<void> keep = nullptr);

template <typename R, typename... A>
inline R pcall(GDExtensionMethodBindPtr mb, void *object, const A &...a) {
	const void *args[] = { raw_arg(a)..., nullptr };
	typename RawType<R>::type r{};
	gdextension_interface::object_method_bind_ptrcall(mb, object, args, &r);
	return r;
}

template <typename... A>
inline void pcall_void(GDExtensionMethodBindPtr mb, void *object, const A &...a) {
	const void *args[] = { raw_arg(a)..., nullptr };
	gdextension_interface::object_method_bind_ptrcall(mb, object, args, nullptr);
}

template <typename R, typename B, typename... A>
inline R bcall(GDExtensionPtrBuiltInMethod fn, const B &base, const A &...a) {
	const void *args[] = { raw_arg(a)..., nullptr };
	typename RawType<R>::type r{};
	fn(const_cast<void *>(raw_arg(base)), args, &r, int(sizeof...(A)));
	return r;
}

template <typename B, typename... A>
inline void bcall_void(GDExtensionPtrBuiltInMethod fn, const B &base, const A &...a) {
	const void *args[] = { raw_arg(a)..., nullptr };
	fn(const_cast<void *>(raw_arg(base)), args, nullptr, int(sizeof...(A)));
}

template <typename R, typename A, typename B>
inline R bop(GDExtensionPtrOperatorEvaluator fn, const A &a, const B &b) {
	typename RawType<R>::type r{};
	fn(raw_arg(a), raw_arg(b), &r);
	return r;
}

template <typename R, typename... A>
inline R bctor(GDExtensionPtrConstructor fn, const A &...a) {
	const void *args[] = { raw_arg(a)..., nullptr };
	R r{};
	fn(&r, args);
	return r;
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
#define GN_STR(s) ([]() -> const godot::String & { static const godot::String *n = new godot::String(s); return *n; }())
#define GN_SN(s) ([]() -> const godot::StringName & { static const godot::StringName *n = new godot::StringName(s); return *n; }())
#define GN_BC(T, type, name) ([]() -> const T & { static const T *v = gn::keep(new T(gn::builtin_constant_as<T>(type, GN_SN(name)))); return *v; }())
#define GN_MB(cls, name, hash) ([]() { static const GDExtensionMethodBindPtr mb = gn::method_bind(cls, name, hash); return mb; }())
#define GN_BM(type, name, hash) ([]() { static const GDExtensionPtrBuiltInMethod fn = gn::builtin_method(type, name, hash); return fn; }())
#define GN_OP(op, left, right) ([]() { static const GDExtensionPtrOperatorEvaluator fn = gdextension_interface::variant_get_ptr_operator_evaluator((GDExtensionVariantOperator)op, (GDExtensionVariantType)left, (GDExtensionVariantType)right); return fn; }())
#define GN_CTOR(type, index) ([]() { static const GDExtensionPtrConstructor fn = gdextension_interface::variant_get_ptr_constructor((GDExtensionVariantType)type, index); return fn; }())
#define GN_SINGLETON(name) ([]() -> const Variant & { static const Variant *v = new Variant(gn::singleton(GN_SN(name))); return *v; }())
