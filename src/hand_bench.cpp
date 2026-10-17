#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/builtin_types.hpp>

using namespace godot;

class HandBench : public Node {
	GDCLASS(HandBench, Node)

protected:
	static void _bind_methods() {
		ClassDB::bind_method(D_METHOD("int_loop", "n"), &HandBench::int_loop);
		ClassDB::bind_method(D_METHOD("int_mix", "n"), &HandBench::int_mix);
		ClassDB::bind_method(D_METHOD("vec_math", "n"), &HandBench::vec_math);
		ClassDB::bind_method(D_METHOD("packed", "n"), &HandBench::packed);
		ClassDB::bind_method(D_METHOD("engine_calls", "n"), &HandBench::engine_calls);
	}

public:
	int64_t int_loop(int64_t n) {
		int64_t s = 0;
		for (int64_t i = 0; i < n; i++) {
			s = (s + i * 3) % 1000003;
		}
		return s;
	}
	int64_t int_mix(int64_t n) {
		int64_t s = 88172645463325252;
		for (int64_t i = 0; i < n; i++) {
			s = s ^ int64_t(uint64_t(s) << 13);
			s = s ^ (s >> 7);
			s = s + i;
		}
		return s;
	}
	double vec_math(int64_t n) {
		Vector3 p(1.0, 2.0, 3.0), v(0.1, 0.2, 0.3);
		double total = 0.0;
		for (int64_t i = 0; i < n; i++) {
			p = (p + v * 0.5).normalized();
			total += p.dot(v);
		}
		return total;
	}
	double packed(int64_t n) {
		PackedFloat64Array a;
		a.resize(1024);
		double *w = a.ptrw();
		for (int64_t i = 0; i < 1024; i++) {
			w[i] = double(i);
		}
		double s = 0.0;
		for (int64_t r = 0; r < n; r++) {
			int64_t i = r & 1023;
			s += w[i] * 0.5;
			w[i] = s;
		}
		return s;
	}
	int64_t engine_calls(int64_t n) {
		int64_t c = 0;
		for (int64_t i = 0; i < n; i++) {
			c += get_child_count();
		}
		return c;
	}
};

void gn_register_hand_bench() {
	GDREGISTER_CLASS(HandBench);
}
