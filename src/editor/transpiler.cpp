#include "transpiler.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <set>
#include <sstream>

namespace gn {

namespace {

// shortcut: hand parser for a typed subset, not a full GDScript frontend. Upgrade by porting modules/gdscript before claiming full language coverage.

enum class TK { Id, Int, Float, Str, Op, NL, Indent, Dedent, Eof };

struct Tok {
	TK k;
	std::string s;
	int line;
};

struct Err {
	int line;
	std::string msg;
};

struct Retry {};

std::string first_line(const std::string &src) {
	std::istringstream in(src);
	std::string l;
	while (std::getline(in, l)) {
		size_t b = l.find_first_not_of(" \t\r");
		if (b != std::string::npos) {
			size_t e = l.find_last_not_of(" \t\r");
			return l.substr(b, e - b + 1);
		}
	}
	return "";
}

std::vector<Tok> tokenize(const std::string &src) {
	std::vector<Tok> out;
	std::vector<int> indents{ 0 };
	size_t i = 0;
	int line = 1, parens = 0;
	bool bol = true;
	std::vector<int> pending_lambdas;
	std::vector<std::pair<int, size_t>> lambda_blocks;
	while (i < src.size()) {
		if (bol && parens == 0) {
			int w = 0;
			while (i < src.size() && (src[i] == ' ' || src[i] == '\t')) {
				w += src[i] == '\t' ? 4 : 1;
				i++;
			}
			if (i >= src.size()) {
				break;
			}
			if (src[i] == '\n' || src[i] == '\r' || src[i] == '#') {
				while (i < src.size() && src[i] != '\n') {
					i++;
				}
				i++;
				line++;
				continue;
			}
			if (w > indents.back()) {
				indents.push_back(w);
				out.push_back({ TK::Indent, "", line });
			}
			while (w < indents.back()) {
				indents.pop_back();
				out.push_back({ TK::Dedent, "", line });
				if (!lambda_blocks.empty() && lambda_blocks.back().second == indents.size()) {
					parens = lambda_blocks.back().first;
					lambda_blocks.pop_back();
					break;
				}
			}
			bol = false;
		}
		char c = src[i];
		if (c == '\n') {
			while (!pending_lambdas.empty() && pending_lambdas.back() > parens) {
				pending_lambdas.pop_back();
			}
			if (parens > 0 && !pending_lambdas.empty() && pending_lambdas.back() == parens && !out.empty() && out.back().s == ":") {
				pending_lambdas.pop_back();
				lambda_blocks.push_back({ parens, indents.size() });
				parens = 0;
			}
			if (parens == 0) {
				out.push_back({ TK::NL, "", line });
				bol = true;
			}
			line++;
			i++;
		} else if (c == ' ' || c == '\t' || c == '\r') {
			i++;
		} else if (c == '#') {
			while (i < src.size() && src[i] != '\n') {
				i++;
			}
		} else if (isalpha((unsigned char)c) || c == '_') {
			size_t b = i;
			while (i < src.size() && (isalnum((unsigned char)src[i]) || src[i] == '_')) {
				i++;
			}
			out.push_back({ TK::Id, src.substr(b, i - b), line });
			if (parens > 0 && out.back().s == "func") {
				pending_lambdas.push_back(parens);
			}
		} else if (isdigit((unsigned char)c)) {
			std::string n;
			bool flt = false;
			if (c == '0' && i + 1 < src.size() && (src[i + 1] == 'x' || src[i + 1] == 'X')) {
				n = "0x";
				i += 2;
				while (i < src.size() && (isxdigit((unsigned char)src[i]) || src[i] == '_')) {
					if (src[i] != '_') {
						n += src[i];
					}
					i++;
				}
			} else {
				while (i < src.size() && (isdigit((unsigned char)src[i]) || src[i] == '_' || src[i] == '.' || src[i] == 'e')) {
					if (src[i] == '.' || src[i] == 'e') {
						flt = true;
					}
					if (src[i] != '_') {
						n += src[i];
					}
					i++;
					if (n.back() == 'e' && i < src.size() && (src[i] == '-' || src[i] == '+')) {
						n += src[i++];
					}
				}
			}
			out.push_back({ flt ? TK::Float : TK::Int, n, line });
		} else if (c == '"' || c == '\'') {
			const std::string close(3, c);
			bool triple = src.compare(i, 3, close) == 0;
			int start = line;
			std::string s;
			i += triple ? 3 : 1;
			for (;;) {
				if (i >= src.size()) {
					throw Err{ start, "unterminated string" };
				}
				if (triple ? src.compare(i, 3, close) == 0 : src[i] == c) {
					break;
				}
				if (src[i] == '\r') {
					i++;
				} else if (src[i] == '\n') {
					if (!triple) {
						throw Err{ start, "unterminated string" };
					}
					s += "\\n";
					line++;
					i++;
				} else if (src[i] == '\\' && src.compare(i + 1, 1, "\n") == 0) {
					line++;
					i += 2;
				} else if (src[i] == '\\' && src.compare(i + 1, 2, "\r\n") == 0) {
					line++;
					i += 3;
				} else {
					if (src[i] == '\\') {
						s += src[i++];
					} else if (src[i] == '"') {
						s += '\\';
					}
					s += src[i++];
				}
			}
			i += triple ? 3 : 1;
			out.push_back({ TK::Str, s, start });
		} else {
			static const char *ops[] = { "**=", "<<=", ">>=", "**", "<<", ">>", "==", "!=", "<=", ">=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", ":=", "->", "&&", "||" };
			std::string op(1, c);
			for (const char *o : ops) {
				if (src.compare(i, strlen(o), o) == 0) {
					op = o;
					break;
				}
			}
			i += op.size();
			if (op == "(" || op == "[" || op == "{") {
				parens++;
			}
			if (op == ")" || op == "]" || op == "}") {
				parens--;
			}
			out.push_back({ TK::Op, op, line });
		}
	}
	if (!out.empty() && out.back().k != TK::NL) {
		out.push_back({ TK::NL, "", line });
	}
	while (indents.size() > 1) {
		indents.pop_back();
		out.push_back({ TK::Dedent, "", line });
	}
	out.push_back({ TK::Eof, "", line });
	return out;
}

const char *cty(Ty t) {
	switch (t) {
		case Ty::Void: return "void";
		case Ty::Bool: return "bool";
		case Ty::Int: return "int64_t";
		case Ty::Float: return "double";
		case Ty::Str: return "String";
		case Ty::V2: return "Vector2";
		case Ty::V3: return "Vector3";
		case Ty::PF64: return "PackedFloat64Array";
		case Ty::PI64: return "PackedInt64Array";
		case Ty::Arr: return "Array";
		case Ty::Dict: return "Dictionary";
		case Ty::Var: return "Variant";
	}
	return "";
}

std::string sanitize(const std::string &path) {
	std::string r;
	for (char c : path.compare(0, 6, "res://") == 0 ? path.substr(6) : path) {
		r += isalnum((unsigned char)c) ? c : '_';
	}
	return r;
}

std::string class_header(const std::string &name) {
	std::string r;
	for (size_t i = 0; i < name.size(); i++) {
		bool upper = isupper((unsigned char)name[i]);
		bool word = upper && i > 0 && (islower((unsigned char)name[i - 1]) || (i + 1 < name.size() && islower((unsigned char)name[i + 1])));
		if (word && !isdigit((unsigned char)name[i - 1])) {
			r += '_';
		}
		r += (char)tolower((unsigned char)name[i]);
	}
	return r;
}

const char *vty(Ty t) {
	switch (t) {
		case Ty::Bool: return "BOOL";
		case Ty::Int: return "INT";
		case Ty::Float: return "FLOAT";
		case Ty::Str: return "STRING";
		case Ty::V2: return "VECTOR2";
		case Ty::V3: return "VECTOR3";
		case Ty::PF64: return "PACKED_FLOAT64_ARRAY";
		case Ty::PI64: return "PACKED_INT64_ARRAY";
		case Ty::Arr: return "ARRAY";
		case Ty::Dict: return "DICTIONARY";
		default: return "NIL";
	}
}

struct TypeRef {
	std::string builtin;
	std::string cls;
	std::string script;
	std::string elem;
	Ty elem_t = Ty::Var;
	bool same(const TypeRef &o) const { return builtin == o.builtin && cls == o.cls && script == o.script && elem == o.elem; }
	bool any() const { return !builtin.empty() || !cls.empty() || !script.empty(); }
};

struct E {
	std::string c;
	Ty t = Ty::Var;
	std::string set;
	bool lv = false;
	std::string local;
	TypeRef ref;
	bool literal = false;
	std::string raw;
	Ty elem_t = Ty::Var;
};

struct Param {
	std::string name;
	Ty t = Ty::Var;
	TypeRef ref;
	size_t init = 0;
};

struct Fn {
	std::string name;
	std::vector<Param> params;
	Ty ret = Ty::Var;
	TypeRef ret_ref;
	size_t body = 0;
	int line = 0;
	Ty declared = Ty::Var;
	bool coroutine = false;
	bool async_shape = false;
	bool abstract = false;
	bool is_static = false;
	std::string accessor_of;
	int required() const {
		int n = 0;
		for (const Param &a : params) {
			n += a.init == 0;
		}
		return n;
	}
};

struct Member {
	std::string name;
	Ty t = Ty::Var;
	TypeRef ref;
	bool declared = false;
	bool onready = false;
	bool is_static = false;
	int usage = 4096;
	bool enum_auto = false;
	std::string enum_prev;
	std::string getter, setter;
	size_t init = 0;
	int line = 0;
};

struct Const {
	std::string c;
	Ty t;
};

struct Local {
	std::string c;
	Ty t;
	bool weak;
	std::string key;
	TypeRef ref;
};

struct KnownMethod {
	Ty base;
	const char *name;
	std::vector<Ty> args;
	Ty ret;
};

const std::vector<KnownMethod> &known_methods() {
	static const std::vector<KnownMethod> k = {
		{ Ty::V3, "length", {}, Ty::Float }, { Ty::V3, "length_squared", {}, Ty::Float }, { Ty::V3, "normalized", {}, Ty::V3 },
		{ Ty::V3, "dot", { Ty::V3 }, Ty::Float }, { Ty::V3, "cross", { Ty::V3 }, Ty::V3 }, { Ty::V3, "distance_to", { Ty::V3 }, Ty::Float },
		{ Ty::V2, "length", {}, Ty::Float }, { Ty::V2, "length_squared", {}, Ty::Float }, { Ty::V2, "normalized", {}, Ty::V2 },
		{ Ty::V2, "dot", { Ty::V2 }, Ty::Float }, { Ty::V2, "distance_to", { Ty::V2 }, Ty::Float },
		{ Ty::PF64, "size", {}, Ty::Int }, { Ty::PF64, "resize", { Ty::Int }, Ty::Int }, { Ty::PF64, "push_back", { Ty::Float }, Ty::Bool }, { Ty::PF64, "append", { Ty::Float }, Ty::Bool },
		{ Ty::PI64, "size", {}, Ty::Int }, { Ty::PI64, "resize", { Ty::Int }, Ty::Int }, { Ty::PI64, "push_back", { Ty::Int }, Ty::Bool }, { Ty::PI64, "append", { Ty::Int }, Ty::Bool },
		{ Ty::Arr, "size", {}, Ty::Int }, { Ty::Str, "length", {}, Ty::Int }, { Ty::Dict, "size", {}, Ty::Int },
	};
	return k;
}

struct KnownUtility {
	const char *name;
	std::vector<Ty> args;
	Ty ret;
};

const std::vector<KnownUtility> &known_utilities() {
	const Ty F = Ty::Float, I = Ty::Int, B = Ty::Bool;
	static const std::vector<KnownUtility> k = {
		{ "lerpf", { F, F, F }, F }, { "minf", { F, F }, F }, { "maxf", { F, F }, F }, { "clampf", { F, F, F }, F }, { "absf", { F }, F }, { "signf", { F }, F },
		{ "mini", { I, I }, I }, { "maxi", { I, I }, I }, { "clampi", { I, I, I }, I }, { "absi", { I }, I }, { "signi", { I }, I },
		{ "floorf", { F }, F }, { "ceilf", { F }, F }, { "roundf", { F }, F }, { "floori", { F }, I }, { "ceili", { F }, I }, { "roundi", { F }, I },
		{ "asin", { F }, F }, { "acos", { F }, F }, { "atan", { F }, F }, { "atan2", { F, F }, F }, { "sinh", { F }, F }, { "cosh", { F }, F }, { "tanh", { F }, F },
		{ "pow", { F, F }, F }, { "fmod", { F, F }, F }, { "fposmod", { F, F }, F }, { "posmod", { I, I }, I }, { "snappedf", { F, F }, F },
		{ "wrapf", { F, F, F }, F }, { "wrapi", { I, I, I }, I }, { "move_toward", { F, F, F }, F }, { "smoothstep", { F, F, F }, F }, { "inverse_lerp", { F, F, F }, F },
		{ "deg_to_rad", { F }, F }, { "rad_to_deg", { F }, F }, { "is_nan", { F }, B }, { "is_inf", { F }, B },
		{ "randf", {}, F }, { "randi", {}, I }, { "randf_range", { F, F }, F }, { "randi_range", { I, I }, I },
	};
	return k;
}

const std::map<std::string, std::string> &builtin_ctors() {
	static const std::map<std::string, std::string> k = {
		{ "Vector2i", "VECTOR2I" }, { "Vector3i", "VECTOR3I" }, { "Vector4", "VECTOR4" }, { "Vector4i", "VECTOR4I" }, { "Rect2", "RECT2" }, { "Rect2i", "RECT2I" },
		{ "Transform2D", "TRANSFORM2D" }, { "Transform3D", "TRANSFORM3D" }, { "Plane", "PLANE" }, { "Quaternion", "QUATERNION" }, { "AABB", "AABB" }, { "Basis", "BASIS" },
		{ "Projection", "PROJECTION" }, { "Color", "COLOR" }, { "StringName", "STRING_NAME" }, { "NodePath", "NODE_PATH" }, { "PackedByteArray", "PACKED_BYTE_ARRAY" },
		{ "PackedInt32Array", "PACKED_INT32_ARRAY" }, { "PackedFloat32Array", "PACKED_FLOAT32_ARRAY" }, { "PackedStringArray", "PACKED_STRING_ARRAY" },
		{ "PackedVector2Array", "PACKED_VECTOR2_ARRAY" }, { "PackedVector3Array", "PACKED_VECTOR3_ARRAY" }, { "PackedColorArray", "PACKED_COLOR_ARRAY" }, { "PackedVector4Array", "PACKED_VECTOR4_ARRAY" },
	};
	return k;
}

class Compiler {
	std::vector<Tok> t;
	size_t p = 0;
	const std::string path;
	const ProjectQuery &classes;
	bool unsafe;
	bool resolved = false;

	std::string base = "RefCounted", global, const_block, const_defs;
	Fn init_fn, static_fn;
	std::vector<Fn> fns;
	std::vector<Member> members;
	std::vector<std::string> signals;
	std::set<std::string> enums;
	std::map<std::string, Const> consts;
	TypeRef last_ref;
	bool next_onready = false, next_abstract = false, is_abstract = false;
	int next_usage = 4096;
	std::vector<std::pair<int, int>> quiet;
	size_t lambda_end = size_t(-1);
	int lambda_inline = 0;
	std::set<std::string> warnings;

	std::vector<std::map<std::string, Local>> scopes;
	std::set<std::string> demoted;
	const Fn *cur = nullptr;
	std::string out;
	int tmp = 0;

	const Tok &tk() const { return t[p]; }
	int ln() const { return tk().line; }
	bool is(const char *s) const { return (tk().k == TK::Op || tk().k == TK::Id) && tk().s == s; }
	bool eat(const char *s) {
		if (is(s)) {
			p++;
			return true;
		}
		return false;
	}
	void expect(const char *s) {
		if (!eat(s)) {
			throw Err{ ln(), std::string("expected '") + s + "', found '" + tk().s + "'" };
		}
	}
	std::string ident() {
		if (tk().k != TK::Id) {
			throw Err{ ln(), "expected identifier, found '" + tk().s + "'" };
		}
		return t[p++].s;
	}
	void nl() {
		if (p == lambda_end) {
			return;
		}
		if (lambda_inline > 0 && (is(")") || is(",") || is("]") || is("}"))) {
			return;
		}
		if (tk().k != TK::NL) {
			throw Err{ ln(), "expected end of line, found '" + tk().s + "'" };
		}
		p++;
	}
	std::string L(int line) const { return "GN_L(" + std::to_string(line) + ")"; }
	std::string temp() { return "t_" + std::to_string(tmp++); }
	void warn(int line, const std::string &m) {
		for (const std::pair<int, int> &q : quiet) {
			if (line >= q.first && line <= q.second) {
				return;
			}
		}
		warnings.insert(path + ":" + std::to_string(line) + ": " + m);
	}

	size_t past_annotation(size_t q, bool *names_untyped = nullptr) const {
		q += 2;
		if (t[q].s != "(") {
			return q;
		}
		int depth = 0;
		do {
			depth += t[q].s == "(";
			depth -= t[q].s == ")";
			if (names_untyped && t[q].k == TK::Str && t[q].s == "untyped_declaration") {
				*names_untyped = true;
			}
			q++;
		} while (depth > 0 && t[q].k != TK::Eof);
		return q;
	}

	int statement_end(size_t q) const {
		while (t[q].k == TK::NL || t[q].s == "@") {
			q = t[q].k == TK::NL ? q + 1 : past_annotation(q);
		}
		int depth = 0;
		while (t[q].k != TK::Eof && (t[q].k != TK::NL || depth > 0)) {
			depth += t[q].s == "(" || t[q].s == "[" || t[q].s == "{";
			depth -= t[q].s == ")" || t[q].s == "]" || t[q].s == "}";
			q++;
		}
		if (t[q + 1].k == TK::Indent) {
			q++;
			do {
				depth += t[q].k == TK::Indent;
				depth -= t[q].k == TK::Dedent;
				q++;
			} while (depth > 0 && t[q].k != TK::Eof);
			q--;
		}
		return t[q].line;
	}

	void scan_warning_ignores() {
		int region = 0;
		for (size_t q = 0; q + 1 < t.size(); q++) {
			if (t[q].k != TK::Op || t[q].s != "@" || t[q + 1].s.rfind("warning_ignore", 0) != 0) {
				continue;
			}
			bool named = false;
			size_t after = past_annotation(q, &named);
			if (!named) {
				continue;
			}
			if (t[q + 1].s == "warning_ignore_start") {
				region = region ? region : t[q].line;
			} else if (t[q + 1].s == "warning_ignore_restore") {
				quiet.push_back({ region, t[q].line });
				region = 0;
			} else {
				quiet.push_back({ t[q].line, statement_end(after) });
			}
		}
		if (region) {
			quiet.push_back({ region, t.back().line });
		}
	}

	static bool numeric(Ty x) { return x == Ty::Int || x == Ty::Float; }

	Ty parse_type() {
		std::string n = ident();
		if (eat(".")) {
			ident();
			last_ref = TypeRef();
			return Ty::Int;
		}
		std::string elem;
		Ty elem_t = Ty::Var;
		if (eat("[")) {
			elem_t = parse_type();
			elem = element_args(elem_t, last_ref);
			while (eat(",")) {
				parse_type();
			}
			expect("]");
		}
		last_ref = TypeRef();
		if (n == "Array") {
			last_ref.elem = elem;
			last_ref.elem_t = elem_t;
		}
		static const std::map<std::string, Ty> m = { { "void", Ty::Void }, { "bool", Ty::Bool }, { "int", Ty::Int }, { "float", Ty::Float }, { "String", Ty::Str }, { "Vector2", Ty::V2 }, { "Vector3", Ty::V3 }, { "PackedFloat64Array", Ty::PF64 }, { "PackedInt64Array", Ty::PI64 }, { "Array", Ty::Arr }, { "Dictionary", Ty::Dict } };
		auto it = m.find(n);
		if (it != m.end()) {
			return it->second;
		}
		if (enums.count(n)) {
			return Ty::Int;
		}
		auto b = builtin_ctors().find(n);
		if (b != builtin_ctors().end()) {
			last_ref.builtin = b->second;
		} else if (n == "Signal" || n == "Callable" || n == "RID") {
			last_ref.builtin = n == "RID" ? "RID" : n == "Signal" ? "SIGNAL" : "CALLABLE";
		} else if (classes.class_exists(n)) {
			last_ref.cls = n;
		} else if (n != "Variant") {
			last_ref.script = classes.global_class_path(n);
		}
		return Ty::Var;
	}

	static std::string element_args(Ty ty, const TypeRef &r) {
		if (ty != Ty::Var) {
			return std::string("Variant::") + vty(ty) + ", StringName(), nullptr";
		}
		if (!r.builtin.empty()) {
			return "Variant::" + r.builtin + ", StringName(), nullptr";
		}
		if (!r.cls.empty()) {
			return "Variant::OBJECT, GN_SN(\"" + r.cls + "\"), nullptr";
		}
		return r.script.empty() ? "" : "Variant::OBJECT, StringName(), \"" + r.script + "\"";
	}

	std::string guard(const std::string &v, const TypeRef &r, const std::string &loc) const {
		if (!r.elem.empty()) {
			return unsafe ? v : "gn::check_array(" + v + ", " + r.elem + ", " + loc + ")";
		}
		if (!r.builtin.empty()) {
			return "gn::check_builtin(" + v + ", Variant::" + r.builtin + ", " + loc + ")";
		}
		if (unsafe) {
			return v;
		}
		if (!r.cls.empty()) {
			return "gn::check_class(" + v + ", GN_SN(\"" + r.cls + "\"), " + loc + ")";
		}
		if (!r.script.empty()) {
			return "gn::check_script(" + v + ", \"" + r.script + "\", " + loc + ")";
		}
		return v;
	}

	std::string guarded(const E &e, Ty to, const TypeRef &r, int line) {
		std::string v = conv(e, to, line);
		if (e.literal && !r.elem.empty()) {
			return "gn::typed_array(" + v + ", " + r.elem + ", " + L(line) + ")";
		}
		return (to == Ty::Var || to == Ty::Arr) && !e.ref.same(r) ? guard(v, r, L(line)) : v;
	}

	std::string conv(const E &e, Ty to, int line) {
		if (e.t == to) {
			return e.c;
		}
		if (e.t == Ty::Void) {
			throw Err{ line, "void value used" };
		}
		if (to == Ty::Var) {
			return "Variant(" + e.c + ")";
		}
		if (e.t == Ty::Int && to == Ty::Float) {
			return "double(" + e.c + ")";
		}
		if (e.t == Ty::Float && to == Ty::Int) {
			return "int64_t(" + e.c + ")";
		}
		if (e.t == Ty::Var) {
			if (unsafe) {
				return "(" + e.c + ").operator " + cty(to) + "()";
			}
			return std::string("gn::cast<") + cty(to) + ">(" + e.c + ", Variant::" + vty(to) + ", " + L(line) + ")";
		}
		throw Err{ line, std::string("cannot convert ") + cty(e.t) + " to " + cty(to) };
	}

	std::string truth(const E &e) {
		if (e.t == Ty::Bool) {
			return e.c;
		}
		if (numeric(e.t)) {
			return "(" + e.c + " != 0)";
		}
		return "Variant(" + e.c + ").booleanize()";
	}

	E variant_op(const char *op, const E &a, const E &b, int line) {
		return { std::string("gn::op(Variant::") + op + ", " + conv(a, Ty::Var, line) + ", " + conv(b, Ty::Var, line) + ", " + L(line) + ")", Ty::Var };
	}

	E bin(const std::string &op, const E &a, const E &b, int line) {
		static const std::map<std::string, const char *> vops = { { "+", "OP_ADD" }, { "-", "OP_SUBTRACT" }, { "*", "OP_MULTIPLY" }, { "/", "OP_DIVIDE" }, { "%", "OP_MODULE" }, { "**", "OP_POWER" }, { "<<", "OP_SHIFT_LEFT" }, { ">>", "OP_SHIFT_RIGHT" }, { "&", "OP_BIT_AND" }, { "|", "OP_BIT_OR" }, { "^", "OP_BIT_XOR" }, { "==", "OP_EQUAL" }, { "!=", "OP_NOT_EQUAL" }, { "<", "OP_LESS" }, { "<=", "OP_LESS_EQUAL" }, { ">", "OP_GREATER" }, { ">=", "OP_GREATER_EQUAL" }, { "in", "OP_IN" } };
		bool cmp = op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
		bool ints = a.t == Ty::Int && b.t == Ty::Int;
		if (numeric(a.t) && numeric(b.t)) {
			if (cmp) {
				return { "(" + a.c + " " + op + " " + b.c + ")", Ty::Bool };
			}
			if (ints) {
				if (op == "+" || op == "-" || op == "*") {
					const char *f = op == "+" ? "add" : op == "-" ? "sub" : "mul";
					return { std::string("gn::") + f + "(" + a.c + ", " + b.c + ")", Ty::Int };
				}
				if (op == "/" || op == "%") {
					if (unsafe) {
						return { "(" + a.c + " " + op + " " + b.c + ")", Ty::Int };
					}
					return { std::string("gn::") + (op == "/" ? "idiv" : "imod") + "(" + a.c + ", " + b.c + ", " + L(line) + ")", Ty::Int };
				}
				if (op == "&" || op == "|" || op == "^" || op == ">>") {
					return { "(" + a.c + " " + op + " " + b.c + ")", Ty::Int };
				}
				if (op == "<<") {
					return { "int64_t(uint64_t(" + a.c + ") << " + b.c + ")", Ty::Int };
				}
			} else if (op == "+" || op == "-" || op == "*" || op == "/") {
				return { "(double(" + a.c + ") " + op + " double(" + b.c + "))", Ty::Float };
			}
		}
		bool av = a.t == Ty::V2 || a.t == Ty::V3, bv = b.t == Ty::V2 || b.t == Ty::V3;
		if (av && a.t == b.t && (op == "+" || op == "-" || op == "*" || op == "/")) {
			return { "(" + a.c + " " + op + " " + b.c + ")", a.t };
		}
		if (av && a.t == b.t && (op == "==" || op == "!=")) {
			return { "(" + a.c + " " + op + " " + b.c + ")", Ty::Bool };
		}
		if (av && numeric(b.t) && (op == "*" || op == "/")) {
			return { "(" + a.c + " " + op + " real_t(" + b.c + "))", a.t };
		}
		if (bv && numeric(a.t) && op == "*") {
			return { "(" + b.c + " * real_t(" + a.c + "))", b.t };
		}
		if (a.t == Ty::Str && b.t == Ty::Str && (op == "+" || op == "==" || op == "!=")) {
			return { "(" + a.c + " " + op + " " + b.c + ")", op == "+" ? Ty::Str : Ty::Bool };
		}
		return variant_op(vops.at(op), a, b, line);
	}

	E expr() {
		E a = e_or();
		if (is("if")) {
			int line = ln();
			p++;
			E c = e_or();
			expect("else");
			E b = expr();
			if (a.t == b.t && a.t != Ty::Void) {
				return { "(" + truth(c) + " ? " + a.c + " : " + b.c + ")", a.t };
			}
			return { "(" + truth(c) + " ? " + conv(a, Ty::Var, line) + " : " + conv(b, Ty::Var, line) + ")", Ty::Var };
		}
		return a;
	}
	E e_or() {
		E a = e_and();
		while (is("or") || is("||")) {
			p++;
			E b = e_and();
			a = { "(" + truth(a) + " || " + truth(b) + ")", Ty::Bool };
		}
		return a;
	}
	E e_and() {
		E a = e_not();
		while (is("and") || is("&&")) {
			p++;
			E b = e_not();
			a = { "(" + truth(a) + " && " + truth(b) + ")", Ty::Bool };
		}
		return a;
	}
	E e_not() {
		if (is("not") || is("!")) {
			p++;
			E a = e_not();
			return { "(!" + truth(a) + ")", Ty::Bool };
		}
		return e_cmp();
	}
	E e_cmp() {
		E a = e_bit(0);
		for (;;) {
			int line = ln();
			if (eat("is")) {
				bool negate = eat("not");
				E r = type_test(a, line);
				a = negate ? E{ "(!" + r.c + ")", Ty::Bool } : r;
			} else if (is("not") && t[p + 1].s == "in") {
				p += 2;
				E b = e_bit(0);
				a = { "(!" + truth(bin("in", a, b, line)) + ")", Ty::Bool };
			} else if (is("<") || is(">") || is("==") || is("!=") || is("<=") || is(">=") || is("in")) {
				std::string op = t[p++].s;
				E b = e_bit(0);
				a = bin(op, a, b, line);
			} else {
				return a;
			}
		}
	}

	E type_test(const E &a, int line) {
		Ty ty = parse_type();
		std::string v = conv(a, Ty::Var, line);
		if (ty != Ty::Var) {
			return { "gn::is_builtin(" + v + ", Variant::" + vty(ty) + ")", Ty::Bool };
		}
		if (!last_ref.builtin.empty()) {
			return { "gn::is_builtin(" + v + ", Variant::" + last_ref.builtin + ")", Ty::Bool };
		}
		if (!last_ref.cls.empty()) {
			return { "gn::is_class(" + v + ", GN_SN(\"" + last_ref.cls + "\"))", Ty::Bool };
		}
		if (!last_ref.script.empty()) {
			return { "gn::is_script(" + v + ", \"" + last_ref.script + "\")", Ty::Bool };
		}
		throw Err{ line, "unknown type after 'is'" };
	}

	E e_as() {
		E a = e_unary();
		while (is("as")) {
			int line = ln();
			p++;
			Ty ty = parse_type();
			TypeRef ref = last_ref;
			std::string v = conv(a, Ty::Var, line);
			if (ty != Ty::Var) {
				a = { conv(a, ty, line), ty };
			} else if (!ref.builtin.empty()) {
				a = { "gn::check_builtin(" + v + ", Variant::" + ref.builtin + ", " + L(line) + ")", Ty::Var };
			} else if (!ref.cls.empty()) {
				a = { "gn::as_class(" + v + ", GN_SN(\"" + ref.cls + "\"))", Ty::Var };
			} else if (!ref.script.empty()) {
				a = { "gn::as_script(" + v + ", \"" + ref.script + "\")", Ty::Var };
			}
			a.ref = ref;
		}
		return a;
	}
	E e_bit(int level) {
		static const std::vector<std::vector<std::string>> lv = { { "|" }, { "^" }, { "&" }, { "<<", ">>" }, { "+", "-" }, { "*", "/", "%" } };
		if (level == (int)lv.size()) {
			return e_as();
		}
		E a = e_bit(level + 1);
		for (;;) {
			bool hit = false;
			for (const std::string &o : lv[level]) {
				if (tk().k == TK::Op && tk().s == o) {
					int line = ln();
					p++;
					E b = e_bit(level + 1);
					a = bin(o, a, b, line);
					hit = true;
					break;
				}
			}
			if (!hit) {
				return a;
			}
		}
	}
	E e_unary() {
		int line = ln();
		if (eat("-")) {
			E a = e_unary();
			if (a.t == Ty::Int) {
				return { "gn::neg(" + a.c + ")", Ty::Int };
			}
			if (a.t == Ty::Float || a.t == Ty::V2 || a.t == Ty::V3) {
				return { "(-" + a.c + ")", a.t };
			}
			return { "gn::op(Variant::OP_NEGATE, " + conv(a, Ty::Var, line) + ", Variant(), " + L(line) + ")", Ty::Var };
		}
		if (eat("+")) {
			return e_unary();
		}
		if (eat("~")) {
			E a = e_unary();
			return { "(~" + conv(a, Ty::Int, line) + ")", Ty::Int };
		}
		return e_pow();
	}
	E e_pow() {
		E a = e_post();
		if (is("**")) {
			int line = ln();
			p++;
			E b = e_unary();
			return bin("**", a, b, line);
		}
		return a;
	}

	std::vector<E> args() {
		std::vector<E> a;
		while (!is(")")) {
			a.push_back(expr());
			if (!eat(",")) {
				break;
			}
		}
		expect(")");
		return a;
	}

	std::string var_args(const std::vector<E> &a, int line) {
		std::string s;
		for (const E &e : a) {
			s += (s.empty() ? "" : ", ") + conv(e, Ty::Var, line);
		}
		return s;
	}

	std::string native() const { return parent ? parent->native() : base; }

	const Const *find_const(const std::string &n) const {
		auto c = consts.find(n);
		if (c != consts.end()) {
			return &c->second;
		}
		return parent ? parent->find_const(n) : nullptr;
	}

	const Local *find_local(const std::string &n) const {
		for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
			auto f = it->find(n);
			if (f != it->end()) {
				return &f->second;
			}
		}
		return nullptr;
	}

	const Member *find_member(const std::string &n) const {
		for (const Member &m : members) {
			if (m.name == n) {
				return &m;
			}
		}
		return parent ? parent->find_member(n) : nullptr;
	}

	const Compiler *member_owner(const std::string &n) const {
		for (const Member &m : members) {
			if (m.name == n) {
				return this;
			}
		}
		return parent ? parent->member_owner(n) : nullptr;
	}

	bool find_signal(const std::string &n) const {
		if (std::find(signals.begin(), signals.end(), n) != signals.end()) {
			return true;
		}
		return parent ? parent->find_signal(n) : false;
	}

	const Compiler *owner_of(const std::string &n) const {
		for (const Fn &f : fns) {
			if (f.name == n) {
				return this;
			}
		}
		return parent ? parent->owner_of(n) : nullptr;
	}

	const Fn *find_fn(const std::string &n) const {
		for (const Fn &f : fns) {
			if (f.name == n) {
				return &f;
			}
		}
		return parent ? parent->find_fn(n) : nullptr;
	}

	std::string struct_name() const { return "C_" + sanitize(path); }

	E super_call(const std::string &n, int line) {
		expect("(");
		std::vector<E> a = args();
		const Fn *f = parent ? parent->find_fn(n) : nullptr;
		if (f == nullptr) {
			return { "((void)0)", Ty::Void };
		}
		E e = self_call(f, a, line);
		e.c = parent->struct_name() + "::" + e.c;
		return e;
	}

	E self_call(const Fn *f, const std::vector<E> &a, int line) {
		if ((int)a.size() < f->required() || a.size() > f->params.size()) {
			throw Err{ line, "wrong argument count calling " + f->name };
		}
		if (cur->is_static && !f->is_static) {
			throw Err{ line, "cannot call instance function " + f->name + " from a static function" };
		}
		std::string s = "f_" + f->name + "(";
		for (size_t i = 0; i < a.size(); i++) {
			s += (i ? ", " : "") + guarded(a[i], f->params[i].t, f->params[i].ref, line);
		}
		E e{ s + ")", f->ret };
		if (!f->async_shape) {
			e.ref = f->ret_ref;
		}
		return e;
	}

	E through_accessors(E e, const Member &m, int line) {
		if (!m.getter.empty() && cur->name != m.getter) {
			e.c = conv(self_call(find_fn(m.getter), {}, line), m.t, line);
			e.lv = false;
		}
		if (!m.setter.empty() && cur->name != m.setter) {
			E value{ "%v", m.t };
			value.ref = m.ref;
			e.set = self_call(find_fn(m.setter), { value }, line).c;
			e.lv = false;
		}
		return e;
	}

	E native_call(const std::string &n, NativeMember m, const std::vector<E> &a, int line) {
		if (m.direct && a.size() <= m.args.size() && a.size() + m.defaults >= m.args.size()) {
			std::string call = "static_cast<" + native() + " *>(owner)->" + n + "(";
			for (size_t i = 0; i < a.size(); i++) {
				call += (i ? ", " : "") + conv(a[i], m.args[i], line);
			}
			call += ")";
			return { m.type == Ty::Void ? call : std::string(cty(m.type)) + "(" + call + ")", m.type };
		}
		E typed;
		if (bound_call("(void *)owner->_owner", native(), n, a, line, typed)) {
			return typed;
		}
		std::string list = var_args(a, line);
		std::string call = "owner->call(GN_SN(\"" + n + "\")" + (list.empty() ? "" : ", " + list) + ")";
		if (m.type == Ty::Void) {
			return { call, Ty::Void };
		}
		if (m.type == Ty::Var) {
			E e{ call, Ty::Var };
			e.ref.cls = object_return(native(), n);
			return e;
		}
		return { "(" + call + ").operator " + cty(m.type) + "()", m.type };
	}

	bool api_type(const std::string &name, Ty &ty, std::string &cpp, std::string &variant) const {
		static const std::map<std::string, Ty> simple = { { "int", Ty::Int }, { "float", Ty::Float }, { "bool", Ty::Bool }, { "String", Ty::Str }, { "Vector2", Ty::V2 }, { "Vector3", Ty::V3 }, { "PackedFloat64Array", Ty::PF64 }, { "PackedInt64Array", Ty::PI64 }, { "Array", Ty::Arr }, { "Dictionary", Ty::Dict }, { "Variant", Ty::Var } };
		std::string n = name;
		variant.clear();
		if (n.rfind("enum::", 0) == 0 || n.rfind("bitfield::", 0) == 0) {
			n = "int";
		} else if (n.rfind("typedarray::", 0) == 0) {
			n = "Array";
		} else if (n.rfind("typeddictionary::", 0) == 0) {
			n = "Dictionary";
		}
		auto it = simple.find(n);
		if (it != simple.end()) {
			ty = it->second;
			cpp = cty(ty);
			return true;
		}
		auto b = builtin_ctors().find(n);
		if (b != builtin_ctors().end() || n == "RID" || n == "Callable" || n == "Signal") {
			ty = Ty::Var;
			cpp = n;
			variant = n == "RID" ? "RID" : n == "Callable" ? "CALLABLE" : n == "Signal" ? "SIGNAL" : b->second;
			return true;
		}
		return false;
	}

	std::string object_return(const std::string &cls, const std::string &n) const {
		NativeBind m = classes.bind(cls, n);
		Ty ty;
		std::string cpp, variant;
		if (!m.found || m.ret == "void" || api_type(m.ret, ty, cpp, variant) || !classes.class_exists(m.ret)) {
			return "";
		}
		return m.ret;
	}

	bool bound_call(const std::string &object, const std::string &cls, const std::string &n, const std::vector<E> &a, int line, E &r) {
		NativeBind m = classes.bind(cls, n);
		Ty ret = Ty::Void;
		std::string ret_cpp, ret_variant;
		if (!m.found || !m.ptr || a.size() > m.args.size() || (m.ret != "void" && !api_type(m.ret, ret, ret_cpp, ret_variant))) {
			return false;
		}
		std::string tail;
		for (size_t i = a.size(); i < m.args.size(); i++) {
			Ty ty;
			std::string cpp, variant;
			api_type(m.args[i], ty, cpp, variant);
			const std::string &d = m.defaults[i];
			bool number = !d.empty() && d.find_first_not_of("-0123456789.e") == std::string::npos;
			if ((ty == Ty::Bool && (d == "true" || d == "false")) || ((ty == Ty::Int || ty == Ty::Float) && number)) {
				tail += ", " + cpp + "(" + d + ")";
			} else {
				return false;
			}
		}
		std::string list;
		for (size_t i = 0; i < a.size(); i++) {
			Ty ty;
			std::string cpp, variant;
			api_type(m.args[i], ty, cpp, variant);
			std::string value;
			if (!variant.empty()) {
				std::string v = conv(a[i], Ty::Var, line);
				value = unsafe ? "(" + v + ").operator " + cpp + "()" : "gn::cast<" + cpp + ">(" + v + ", Variant::" + variant + ", " + L(line) + ")";
			} else {
				value = cpp + "(" + conv(a[i], ty, line) + ")";
			}
			list += ", " + value;
		}
		list += tail;
		std::string head = "GN_MB(\"" + m.owner + "\", \"" + n + "\", " + std::to_string(m.hash) + "LL), " + object;
		if (m.ret == "void") {
			r = { "gn::pcall_void(" + head + list + ")", Ty::Void };
			return true;
		}
		std::string call = "gn::pcall<" + ret_cpp + ">(" + head + list + ")";
		r = { ret_variant.empty() ? call : "Variant(" + call + ")", ret };
		r.ref.builtin = ret_variant;
		return true;
	}

	std::string object_of(const E &b, int line) {
		std::string v = conv(b, Ty::Var, line);
		return unsafe ? "gn::raw_object(" + v + ")" : "gn::live_object(" + v + ", " + L(line) + ")";
	}

	E lambda(int line) {
		Fn fn;
		fn.ret = Ty::Var;
		fn.declared = Ty::Var;
		fn.name = "lambda";
		fn.line = line;
		if (tk().k == TK::Id) {
			p++;
		}
		expect("(");
		const Fn *saved_cur = cur;
		std::string saved_out;
		saved_out.swap(out);
		cur = &fn;
		scopes.emplace_back();
		int index = 0, required = 0;
		std::string head;
		while (!is(")")) {
			std::string name = ident();
			Ty ty = Ty::Var;
			TypeRef ref;
			if (eat(":")) {
				ty = parse_type();
				ref = last_ref;
			}
			std::string at = "*args[" + std::to_string(index) + "]";
			std::string value = guard(ty == Ty::Var ? at : unsafe ? "(" + at + ").operator " + cty(ty) + "()" : std::string("gn::cast<") + cty(ty) + ">(" + at + ", Variant::" + vty(ty) + ", " + L(line) + ")", ref, L(line));
			if (eat("=")) {
				value = "argc > " + std::to_string(index) + " ? " + value + " : " + conv(expr(), ty, line);
			} else {
				required++;
			}
			head += std::string(cty(ty)) + " l_" + name + " = " + value + ";\n";
			declare(name, ty, false, "", ref);
			index++;
			if (!eat(",")) {
				break;
			}
		}
		expect(")");
		if (eat("->")) {
			parse_type();
		}
		expect(":");
		fn.coroutine = lambda_awaits();
		scopes.emplace_back();
		if (tk().k == TK::NL) {
			p++;
			if (tk().k != TK::Indent) {
				throw Err{ ln(), "expected an indented block" };
			}
			p++;
			int inline_saved = lambda_inline;
			lambda_inline = 0;
			while (tk().k != TK::Dedent) {
				stmt();
			}
			lambda_inline = inline_saved;
			p++;
		} else {
			lambda_inline++;
			stmt();
			lambda_inline--;
		}
		lambda_end = p;
		scopes.pop_back();
		scopes.pop_back();
		std::string body;
		body.swap(out);
		out.swap(saved_out);
		cur = saved_cur;
		std::string code = "gn::lambda(owner, " + std::to_string(required) + ", " + std::to_string(index) + ", [=](const Variant **args, int argc) -> Variant {\n";
		if (fn.coroutine) {
			code += "auto run = std::make_shared<std::function<gn::Task()>>([=]() mutable -> gn::Task {\n" + head + body + "co_return Variant();\n});\nreturn gn::run((*run)(), run);\n})";
		} else {
			code += "auto run = [=]() mutable -> Variant {\n" + head + body + "return {};\n};\nreturn run();\n})";
		}
		E e{ code, Ty::Var };
		e.ref.builtin = "CALLABLE";
		return e;
	}

	bool awaits_between(size_t from, size_t to) const {
		for (size_t q = from; q < to; q++) {
			if (t[q].k == TK::Id && t[q].s == "await") {
				return true;
			}
		}
		return false;
	}

	bool lambda_awaits() const {
		size_t q = p;
		int depth = 0;
		if (t[q].k == TK::NL) {
			q++;
			do {
				depth += t[q].k == TK::Indent;
				depth -= t[q].k == TK::Dedent;
				q++;
			} while (depth > 0 && t[q].k != TK::Eof);
			return awaits_between(p, q);
		}
		while (t[q].k != TK::NL && t[q].k != TK::Eof) {
			const std::string &o = t[q].s;
			if (depth == 0 && (o == ")" || o == "," || o == "]" || o == "}")) {
				break;
			}
			depth += o == "(" || o == "[" || o == "{";
			depth -= o == ")" || o == "]" || o == "}";
			q++;
		}
		return awaits_between(p, q);
	}

	bool global_call(const std::string &n, E &r, int line) {
		static const std::set<std::string> f1 = { "sqrt", "sin", "cos", "tan", "exp", "log" };
		static const std::map<std::string, Ty> ctors = { { "Vector3", Ty::V3 }, { "Vector2", Ty::V2 }, { "PackedFloat64Array", Ty::PF64 }, { "PackedInt64Array", Ty::PI64 }, { "Array", Ty::Arr }, { "Dictionary", Ty::Dict } };
		if (n == "print") {
			p += 2;
			std::vector<E> a = args();
			r = { a.empty() ? "UtilityFunctions::print(\"\")" : "UtilityFunctions::print(" + var_args(a, line) + ")", Ty::Void };
			return true;
		}
		if (n == "str") {
			p += 2;
			std::vector<E> a = args();
			r = { "UtilityFunctions::str(" + var_args(a, line) + ")", Ty::Str };
			return true;
		}
		if (f1.count(n)) {
			p += 2;
			std::vector<E> a = args();
			if (a.size() != 1) {
				throw Err{ line, n + " takes one argument" };
			}
			r = { "double(Math::" + n + "(" + conv(a[0], Ty::Float, line) + "))", Ty::Float };
			return true;
		}
		if (n == "abs" || n == "floor" || n == "ceil") {
			p += 2;
			std::vector<E> a = args();
			if (a.size() != 1 || !numeric(a[0].t)) {
				throw Err{ line, n + " needs a typed number" };
			}
			if (a[0].t == Ty::Int) {
				r = n == "abs" ? E{ "int64_t(::llabs(" + a[0].c + "))", Ty::Int } : a[0];
			} else {
				r = { "double(Math::" + n + "(" + a[0].c + "))", Ty::Float };
			}
			return true;
		}
		if (n == "int" || n == "float" || n == "bool") {
			p += 2;
			std::vector<E> a = args();
			Ty to = n == "int" ? Ty::Int : n == "float" ? Ty::Float : Ty::Bool;
			if (a.size() != 1) {
				throw Err{ line, n + " takes one argument" };
			}
			if (to == Ty::Bool) {
				r = { truth(a[0]), Ty::Bool };
			} else if (numeric(a[0].t)) {
				r = { conv(a[0], to, line), to };
			} else {
				r = { "Variant(" + a[0].c + ").operator " + cty(to) + "()", to };
			}
			return true;
		}
		if (n == "len") {
			p += 2;
			std::vector<E> a = args();
			if (a.size() != 1) {
				throw Err{ line, "len takes one argument" };
			}
			r = { "gn::len(" + conv(a[0], Ty::Var, line) + ", " + L(line) + ")", Ty::Int };
			return true;
		}
		if (n == "range") {
			p += 2;
			std::vector<E> a = args();
			if (a.empty() || a.size() > 3) {
				throw Err{ line, "range takes one to three arguments" };
			}
			std::string list;
			for (const E &e : a) {
				list += (list.empty() ? "" : ", ") + conv(e, Ty::Int, line);
			}
			r = { "gn::range({" + list + "}, " + L(line) + ")", Ty::Arr };
			return true;
		}
		if (n == "char") {
			p += 2;
			std::vector<E> a = args();
			r = { "gn::chr(" + conv(a.at(0), Ty::Int, line) + ")", Ty::Str };
			return true;
		}
		if (n == "ord") {
			p += 2;
			std::vector<E> a = args();
			r = { "gn::ord(" + conv(a.at(0), Ty::Str, line) + ")", Ty::Int };
			return true;
		}
		if (n == "Color8") {
			p += 2;
			std::vector<E> a = args();
			if (a.size() < 3 || a.size() > 4) {
				throw Err{ line, "Color8 takes three or four arguments" };
			}
			r = { "gn::color8(" + conv(a[0], Ty::Int, line) + ", " + conv(a[1], Ty::Int, line) + ", " + conv(a[2], Ty::Int, line) + ", " + (a.size() > 3 ? conv(a[3], Ty::Int, line) : std::string("int64_t(255)")) + ")", Ty::Var };
			return true;
		}
		if (n == "type_exists") {
			p += 2;
			std::vector<E> a = args();
			r = { "gn::type_exists(StringName(" + conv(a.at(0), Ty::Str, line) + "))", Ty::Bool };
			return true;
		}
		if (n == "convert") {
			p += 2;
			std::vector<E> a = args();
			r = { "UtilityFunctions::type_convert(" + var_args(a, line) + ")", Ty::Var };
			return true;
		}
		if (n == "is_instance_of") {
			p += 2;
			std::vector<E> a = args();
			r = { "gn::is_instance_of(" + conv(a.at(0), Ty::Var, line) + ", " + conv(a.at(1), Ty::Var, line) + ")", Ty::Bool };
			return true;
		}
		if (n == "is_instance_valid") {
			p += 2;
			std::vector<E> a = args();
			r = { "gn::is_instance_valid(" + conv(a.at(0), Ty::Var, line) + ")", Ty::Bool };
			return true;
		}
		if (n == "instance_from_id") {
			p += 2;
			std::vector<E> a = args();
			r = { "gn::instance_from_id(" + conv(a.at(0), Ty::Int, line) + ")", Ty::Var };
			return true;
		}
		if (n == "preload") {
			if (t[p + 2].k != TK::Str) {
				throw Err{ line, "preload needs a constant path" };
			}
			std::string target = classes.absolute(path, t[p + 2].s);
			p += 3;
			expect(")");
			r = { "gn::load(String(\"" + target + "\"))", Ty::Var };
			return true;
		}
		if (n == "load") {
			p += 2;
			std::vector<E> a = args();
			if (a.size() != 1) {
				throw Err{ line, "load takes one argument" };
			}
			r = { "gn::load(" + conv(a[0], Ty::Str, line) + ")", Ty::Var };
			return true;
		}
		for (const KnownUtility &u : known_utilities()) {
			if (n == u.name) {
				p += 2;
				std::vector<E> a = args();
				if (a.size() != u.args.size()) {
					throw Err{ line, n + " takes " + std::to_string(u.args.size()) + " argument(s)" };
				}
				std::string s = "UtilityFunctions::" + n + "(";
				for (size_t i = 0; i < a.size(); i++) {
					s += (i ? ", " : "") + conv(a[i], u.args[i], line);
				}
				r = { std::string(cty(u.ret)) + "(" + s + "))", u.ret };
				return true;
			}
		}
		std::string ret;
		bool vararg = false;
		if (n == "print_debug" || n == "typeof" || classes.utility(n, ret, vararg)) {
			std::string fn = n == "typeof" ? "type_of" : n == "print_debug" ? "print" : n;
			ret = n == "typeof" ? "int" : n == "print_debug" ? "void" : ret;
			vararg = vararg || n == "print_debug";
			p += 2;
			std::vector<E> a = args();
			std::string list = var_args(a, line);
			std::string call = "UtilityFunctions::" + fn + "(" + (list.empty() && vararg ? "\"\"" : list) + ")";
			static const std::map<std::string, Ty> rets = { { "void", Ty::Void }, { "int", Ty::Int }, { "float", Ty::Float }, { "bool", Ty::Bool }, { "String", Ty::Str } };
			auto rt = rets.find(ret);
			Ty ty = rt == rets.end() ? Ty::Var : rt->second;
			r = { ty == Ty::Void ? call : (ty == Ty::Var ? "Variant(" : std::string(cty(ty)) + "(") + call + ")", ty };
			return true;
		}
		auto c = ctors.find(n);
		if (c != ctors.end()) {
			p += 2;
			std::vector<E> a = args();
			bool vector = c->second == Ty::V3 || c->second == Ty::V2;
			if (!vector && !a.empty()) {
				r = { std::string("(gn::construct(Variant::") + vty(c->second) + ", {" + var_args(a, line) + "}, " + L(line) + ")).operator " + cty(c->second) + "()", c->second };
				return true;
			}
			std::string s = std::string(cty(c->second)) + "(";
			for (size_t i = 0; i < a.size(); i++) {
				s += (i ? ", " : "") + ("real_t(" + conv(a[i], Ty::Float, line) + ")");
			}
			r = { s + ")", c->second };
			return true;
		}
		auto b = builtin_ctors().find(n);
		if (b != builtin_ctors().end()) {
			p += 2;
			std::vector<E> a = args();
			r = { "gn::construct(Variant::" + b->second + ", {" + var_args(a, line) + "}, " + L(line) + ")", Ty::Var };
			return true;
		}
		return false;
	}

	E primary() {
		const Tok tok = tk();
		int line = tok.line;
		switch (tok.k) {
			case TK::Int:
				p++;
				return { "int64_t(" + tok.s + "LL)", Ty::Int };
			case TK::Float: {
				p++;
				bool plain = tok.s.find('.') == std::string::npos && tok.s.find('e') == std::string::npos;
				std::string s = tok.s;
				if (s.back() == '.') {
					s += "0";
				}
				return { "double(" + s + (plain ? ".0" : "") + ")", Ty::Float };
			}
			case TK::Str:
				p++;
				return { "String(\"" + tok.s + "\")", Ty::Str };
			case TK::Op:
				if (eat("(")) {
					E e = expr();
					expect(")");
					e.lv = false;
					e.set.clear();
					e.local.clear();
					return e;
				}
				if (eat("[")) {
					std::string v = temp();
					std::string s = "([&]() { Array " + v + ";";
					while (!is("]")) {
						E e = expr();
						s += " " + v + ".push_back(" + conv(e, Ty::Var, line) + ");";
						if (!eat(",")) {
							break;
						}
					}
					expect("]");
					E list{ s + " return " + v + "; }())", Ty::Arr };
					list.literal = true;
					return list;
				}
				if (eat("{")) {
					std::string v = temp();
					std::string s = "([&]() { Dictionary " + v + ";";
					while (!is("}")) {
						std::string key;
						if (tk().k == TK::Id && t[p + 1].s == "=") {
							key = "Variant(GN_SN(\"" + t[p].s + "\"))";
							p += 2;
						} else {
							key = conv(expr(), Ty::Var, line);
							expect(":");
						}
						s += " " + v + "[" + key + "] = " + conv(expr(), Ty::Var, line) + ";";
						if (!eat(",")) {
							break;
						}
					}
					expect("}");
					return { s + " return " + v + "; }())", Ty::Dict };
				}
				if (is("$") || is("%")) {
					bool unique = is("%");
					p++;
					std::string node = unique ? "%" : "";
					if (tk().k == TK::Str) {
						node += t[p++].s;
					} else {
						while (eat(".")) {
							node += ".";
						}
						if (is("/")) {
							p++;
							node += "/";
						}
						node += ident();
						while (is("/")) {
							p++;
							node += "/" + ident();
						}
					}
					E e{ "owner->call(GN_SN(\"get_node\"), Variant(NodePath(\"" + node + "\")))", Ty::Var };
					e.ref.cls = "Node";
					return e;
				}
				if ((is("&") || is("^")) && t[p + 1].k == TK::Str) {
					bool name = is("&");
					const std::string text = t[p + 1].s;
					p += 2;
					E e{ name ? "Variant(GN_SN(\"" + text + "\"))" : "Variant(NodePath(\"" + text + "\"))", Ty::Var };
					e.ref.builtin = name ? "STRING_NAME" : "NODE_PATH";
					return e;
				}
				break;
			case TK::Id:
				break;
			default:
				break;
		}
		if (tok.k != TK::Id) {
			throw Err{ line, "unsupported expression at '" + tok.s + "'" };
		}
		const std::string &n = tok.s;
		if (n == "func") {
			p++;
			return lambda(line);
		}
		if (n == "await") {
			if (!cur || !cur->coroutine) {
				throw Err{ line, "await is only supported inside functions" };
			}
			p++;
			E value = e_unary();
			std::string v = temp();
			out += "Variant " + v + " = co_await gn::wait(owner, " + conv(value, Ty::Var, line) + ");\n";
			return { v, Ty::Var };
		}
		if (n == "yield") {
			throw Err{ line, "'" + n + "' is not supported in a native file" };
		}
		if (n == "super") {
			p++;
			if (eat(".")) {
				std::string m = ident();
				return super_call(m, line);
			}
			return super_call(cur->name, line);
		}
		if (n == "true" || n == "false") {
			p++;
			return { n, Ty::Bool };
		}
		if (n == "null") {
			p++;
			return { "Variant()", Ty::Var };
		}
		if (n == "PI" || n == "TAU") {
			p++;
			return { n == "PI" ? "3.14159265358979323846" : "6.28318530717958647692", Ty::Float };
		}
		if (n == "INF" || n == "NAN") {
			p++;
			return { n == "INF" ? "double(INFINITY)" : "double(NAN)", Ty::Float };
		}
		if (n == "self") {
			if (cur->is_static) {
				throw Err{ line, "cannot use self in a static function" };
			}
			p++;
			if (is(".")) {
				p++;
				std::string m = ident();
				return is("(") ? named(m, line) : field(m, line);
			}
			return { "Variant(owner)", Ty::Var };
		}
		bool dotted = t[p + 1].k == TK::Op && t[p + 1].s == "." && !find_local(n) && !find_member(n) && !find_const(n);
		if (dotted) {
			const std::string m = t[p + 2].s;
			if (const Const *c = find_const(n + "." + m)) {
				p += 3;
				return { c->c, c->t };
			}
			static const std::map<std::string, std::pair<std::string, Ty>> typed = { { "Vector2", { "VECTOR2", Ty::V2 } }, { "Vector3", { "VECTOR3", Ty::V3 } }, { "String", { "STRING", Ty::Str } }, { "Array", { "ARRAY", Ty::Arr } }, { "Dictionary", { "DICTIONARY", Ty::Dict } } };
			std::string vt;
			Ty ty = Ty::Var;
			auto tp = typed.find(n);
			auto bt = builtin_ctors().find(n);
			if (tp != typed.end()) {
				vt = tp->second.first;
				ty = tp->second.second;
			} else if (bt != builtin_ctors().end()) {
				vt = bt->second;
			}
			if (!vt.empty()) {
				if (t[p + 3].s == "(") {
					p += 4;
					std::vector<E> a = args();
					return { "gn::builtin_static(Variant::" + vt + ", GN_SN(\"" + m + "\"), {" + var_args(a, line) + "}, " + L(line) + ")", Ty::Var };
				}
				if (!classes.has_builtin_constant(n, m)) {
					throw Err{ line, "unknown constant " + n + "." + m };
				}
				p += 3;
				E e{ std::string("GN_BC(") + cty(ty) + ", Variant::" + vt + ", \"" + m + "\")", ty };
				if (ty == Ty::Var) {
					e.ref.builtin = vt;
				}
				return e;
			}
		}
		if (t[p + 1].k == TK::Op && t[p + 1].s == "." && !find_local(n) && !find_member(n) && !find_const(n) && classes.class_exists(n)) {
			const std::string m = t[p + 2].s;
			int64_t value = 0;
			if (m == "new") {
				p += 3;
				expect("(");
				if (!args().empty()) {
					throw Err{ line, n + ".new() takes no arguments" };
				}
				E e{ "gn::instantiate(GN_SN(\"" + n + "\"))", Ty::Var };
				e.ref.cls = n;
				return e;
			}
			NativeMember called = classes.method(n, m);
			if (called.found && called.is_static && t[p + 3].s == "(") {
				p += 4;
				std::vector<E> a = args();
				NativeBind found = classes.bind(n, m);
				if (!found.found) {
					throw Err{ line, "no method bind for " + n + "." + m };
				}
				return { "gn::static_call(GN_MB(\"" + found.owner + "\", \"" + m + "\", " + std::to_string(found.hash) + "LL), {" + var_args(a, line) + "}, " + L(line) + ")", Ty::Var };
			}
			if (classes.int_constant(n, m, value)) {
				p += 3;
				return { "int64_t(" + std::to_string(value) + "LL)", Ty::Int };
			}
			if (classes.is_singleton(n)) {
				p++;
				E e{ "GN_SINGLETON(\"" + n + "\")", Ty::Var };
				e.ref.cls = n;
				return e;
			}
			throw Err{ line, "static member " + n + "." + m + " is not supported" };
		}
		if (dotted) {
			std::string target = classes.global_class_path(n);
			if (!target.empty()) {
				p++;
				return { "gn::load(String(\"" + target + "\"))", Ty::Var };
			}
		}
		if (t[p + 1].k == TK::Op && t[p + 1].s == "(") {
			if (find_fn(n) == nullptr) {
				E r;
				if (global_call(n, r, line)) {
					return r;
				}
			}
			p++;
			return named(n, line);
		}
		p++;
		return field(n, line);
	}

	E named(const std::string &n, int line) {
		expect("(");
		std::vector<E> a = args();
		if (const Fn *f = find_fn(n)) {
			return self_call(f, a, line);
		}
		NativeMember m = classes.method(native(), n);
		if (!m.found) {
			throw Err{ line, "unknown function " + n };
		}
		if (cur->is_static) {
			throw Err{ line, "cannot call instance method " + n + " from a static function" };
		}
		return native_call(n, m, a, line);
	}

	E field(const std::string &n, int line) {
		if (const Local *l = find_local(n)) {
			return { l->c, l->t, "(" + l->c + " = %v)", true, n, l->ref };
		}
		if (const Member *m = find_member(n)) {
			if (m->is_static) {
				std::string c = member_owner(n)->struct_name() + "::s_" + n + "()";
				return through_accessors({ c, m->t, "(" + c + " = %v)", true, "", m->ref }, *m, line);
			}
			if (cur->is_static) {
				throw Err{ line, "cannot use instance member " + n + " from a static function" };
			}
			return through_accessors({ "m_" + n, m->t, "(m_" + n + " = %v)", true, "", m->ref }, *m, line);
		}
		if (const Const *c = find_const(n)) {
			return { c->c, c->t };
		}
		if (find_signal(n) || classes.has_signal(native(), n)) {
			return { "Variant(Signal(owner, GN_SN(\"" + n + "\")))", Ty::Var };
		}
		if (find_fn(n)) {
			return { "Variant(Callable(owner, GN_SN(\"" + n + "\")))", Ty::Var };
		}
		NativeMember m = classes.property(native(), n);
		if (m.found) {
			if (cur->is_static) {
				throw Err{ line, "cannot use instance property " + n + " from a static function" };
			}
			std::string get = "owner->get(GN_SN(\"" + n + "\"))";
			E e{ m.type == Ty::Var ? get : "(" + get + ").operator " + cty(m.type) + "()", m.type };
			e.set = "owner->set(GN_SN(\"" + n + "\"), Variant(%v))";
			typed_property("(void *)owner->_owner", native(), n, e, line);
			return e;
		}
		if (classes.method(native(), n).found) {
			return { "Variant(Callable(owner, GN_SN(\"" + n + "\")))", Ty::Var };
		}
		int64_t value = 0;
		if (classes.global_constant(n, value)) {
			return { "int64_t(" + std::to_string(value) + "LL)", Ty::Int };
		}
		throw Err{ line, "unknown identifier " + n };
	}

	E e_post() {
		E a = primary();
		for (;;) {
			int line = ln();
			if (eat(".")) {
				std::string n = ident();
				if (is("(")) {
					p++;
					std::vector<E> args_ = args();
					a = method(a, n, args_, line);
				} else {
					a = attr(a, n, line);
				}
			} else if (eat("[")) {
				E i = expr();
				expect("]");
				a = index(a, i, line);
			} else {
				return a;
			}
		}
	}

	E method(const E &b, const std::string &n, const std::vector<E> &a, int line) {
		for (const KnownMethod &k : known_methods()) {
			if (k.base == b.t && n == k.name && k.args.size() == a.size()) {
				std::string s = b.c + "." + n + "(";
				for (size_t i = 0; i < a.size(); i++) {
					s += (i ? ", " : "") + conv(a[i], k.args[i], line);
				}
				s += ")";
				return { std::string(cty(k.ret)) + "(" + s + ")", k.ret };
			}
		}
		if (b.t == Ty::PF64 || b.t == Ty::PI64) {
			throw Err{ line, "unsupported packed array method " + n };
		}
		if (!b.ref.cls.empty()) {
			E typed;
			if (bound_call(object_of(b, line), b.ref.cls, n, a, line, typed)) {
				return typed;
			}
		}
		std::string list = var_args(a, line);
		E e{ "gn::mcall(" + conv(b, Ty::Var, line) + ", GN_SN(\"" + n + "\"), {" + list + "}, " + L(line) + ")", Ty::Var };
		if (!b.ref.cls.empty()) {
			e.ref.cls = object_return(b.ref.cls, n);
		}
		return e;
	}

	E attr(const E &b, const std::string &n, int line) {
		if ((b.t == Ty::V3 && (n == "x" || n == "y" || n == "z")) || (b.t == Ty::V2 && (n == "x" || n == "y"))) {
			E e{ "double(" + b.c + "." + n + ")", Ty::Float };
			if (b.lv) {
				e.set = "(" + b.c + "." + n + " = real_t(%v))";
			} else if (!b.set.empty()) {
				std::string v = temp();
				e.set = "[&]() { " + std::string(cty(b.t)) + " " + v + " = " + b.c + "; " + v + "." + n + " = real_t(%v); " + subst(b.set, v) + "; }()";
			}
			return e;
		}
		std::string name = "GN_SN(\"" + n + "\")";
		E e{ "gn::get_named(" + conv(b, Ty::Var, line) + ", " + name + ", " + L(line) + ")", Ty::Var };
		std::string s = temp();
		e.set = "[&]() { Variant " + s + " = " + conv(b, Ty::Var, line) + "; gn::set_named(" + s + ", " + name + ", %v, " + L(line) + ");" + write_back(b, s) + " }()";
		if (!b.ref.cls.empty()) {
			typed_property(object_of(b, line), b.ref.cls, n, e, line);
		}
		return e;
	}

	void typed_property(const std::string &object, const std::string &cls, const std::string &n, E &e, int line) {
		std::string getter, setter;
		if (!classes.accessors(cls, n, getter, setter)) {
			return;
		}
		std::string fallback = e.set;
		E typed;
		if (bound_call(object, cls, getter, {}, line, typed)) {
			e = typed;
			e.set = fallback;
		} else {
			e.ref.cls = object_return(cls, getter);
		}
		E value{ "%v", e.t };
		E stored;
		if (!setter.empty() && bound_call(object, cls, setter, { value }, line, stored) && stored.t == Ty::Void) {
			e.set = stored.c;
		}
	}

	std::string write_back(const E &b, const std::string &s) {
		if (b.set.empty()) {
			return "";
		}
		std::string value = b.t == Ty::Var ? s : s + ".operator " + cty(b.t) + "()";
		return " if (" + s + ".get_type() != Variant::OBJECT) { " + subst(b.set, value) + "; }";
	}

	E index(const E &b, const E &i, int line) {
		bool packed = b.t == Ty::PF64 || b.t == Ty::PI64;
		if ((packed || b.t == Ty::Arr) && b.lv) {
			std::string idx = conv(i, Ty::Int, line);
			idx = unsafe ? "gn::wrap(" + idx + ", " + b.c + ")" : "gn::index(" + idx + ", " + b.c + ".size(), " + L(line) + ")";
			if (packed) {
				return { b.c + ".ptr()[" + idx + "]", b.t == Ty::PF64 ? Ty::Float : Ty::Int, "(" + b.c + ".ptrw()[" + idx + "] = %v)" };
			}
			E item{ b.c + "[" + idx + "]", Ty::Var, "(" + b.c + "[" + idx + "] = %v)" };
			item.raw = item.set;
			item.elem_t = b.ref.elem_t;
			if (!b.ref.elem.empty() && !unsafe) {
				std::string s = temp();
				item.set = "[&]() { Variant " + s + " = " + b.c + "; gn::set_index(" + s + ", Variant(" + idx + "), %v, " + L(line) + "); }()";
			}
			return item;
		}
		std::string v = conv(i, Ty::Var, line), sets;
		if (b.t == Ty::Var && b.lv) {
			sets = "gn::set_index(" + b.c + ", " + v + ", %v, " + L(line) + ")";
		} else {
			std::string s = temp();
			sets = "[&]() { Variant " + s + " = " + conv(b, Ty::Var, line) + "; gn::set_index(" + s + ", " + v + ", %v, " + L(line) + ");" + write_back(b, s) + " }()";
		}
		return { "gn::get_index(" + conv(b, Ty::Var, line) + ", " + v + ", " + L(line) + ")", Ty::Var, sets };
	}

	static std::string subst(std::string s, const std::string &v) {
		size_t at = s.find("%v");
		while (at != std::string::npos) {
			s.replace(at, 2, v);
			at = s.find("%v", at + v.size());
		}
		return s;
	}

	void assign(const E &l, E r, int line) {
		if (l.set.empty()) {
			throw Err{ line, "cannot assign to this expression" };
		}
		if (!l.local.empty()) {
			const Local *loc = find_local(l.local);
			if (loc->weak && r.t != loc->t) {
				demoted.insert(loc->key);
				warn(line, "'" + l.local + "' changes type, so it stays a Variant");
				throw Retry{};
			}
		}
		if (numeric(l.elem_t) && numeric(r.t)) {
			r = { conv(r, l.elem_t, line), l.elem_t };
		}
		std::string v = guarded(r, l.t, l.ref, line);
		out += subst(l.elem_t != Ty::Var && r.t == l.elem_t ? l.raw : l.set, v) + ";\n";
	}

	void declare(const std::string &n, Ty ty, bool weak, const std::string &key, const TypeRef &ref = TypeRef()) {
		scopes.back()[n] = { "l_" + n, ty, weak, key, ref };
	}

	void block() {
		nl();
		if (tk().k != TK::Indent) {
			throw Err{ ln(), "expected an indented block" };
		}
		p++;
		scopes.emplace_back();
		out += "{\n";
		while (tk().k != TK::Dedent) {
			stmt();
		}
		p++;
		out += "}\n";
		scopes.pop_back();
	}

	void var_stmt() {
		int line = ln();
		std::string n = ident();
		Ty ty = Ty::Var;
		TypeRef ref;
		bool typed = false, infer = false;
		if (eat(":=")) {
			infer = true;
		} else if (eat(":")) {
			ty = parse_type();
			ref = last_ref;
			typed = true;
			if (eat("=")) {
				infer = false;
			} else {
				nl();
				declare(n, ty, false, "", ref);
				out += ref.elem.empty() ? std::string(cty(ty)) + " l_" + n + "{};\n" : "Array l_" + n + " = gn::typed_array(Array(), " + ref.elem + ", " + L(line) + ");\n";
				return;
			}
		} else if (!eat("=")) {
			nl();
			warn(line, "'" + n + "' has no type and stays a Variant");
			declare(n, Ty::Var, false, "");
			out += "Variant l_" + n + ";\n";
			return;
		}
		E e = expr();
		nl();
		std::string key = cur->name + "@" + n + "@" + std::to_string(line);
		bool weak = !typed && !infer;
		if (infer) {
			ty = e.t;
			ref = e.ref;
		} else if (weak) {
			ty = demoted.count(key) ? Ty::Var : e.t;
			if (ty == Ty::Var) {
				warn(line, "could not infer a type for '" + n + "', so it stays a Variant");
			}
		}
		std::string v = guarded(e, ty, ref, line);
		declare(n, ty, weak && ty != Ty::Var, key, ref);
		out += std::string(cty(ty)) + " l_" + n + " = " + v + ";\n";
	}

	void for_stmt() {
		int line = ln();
		std::string n = ident();
		if (eat(":")) {
			parse_type();
		}
		expect("in");
		std::string open;
		Ty each = Ty::Int;
		std::string v;
		if (is("range") && t[p + 1].s == "(") {
			p += 2;
			std::vector<E> a = args();
			if (a.empty() || a.size() > 3) {
				throw Err{ line, "range takes one to three arguments" };
			}
			std::string from = a.size() > 1 ? conv(a[0], Ty::Int, line) : "int64_t(0)";
			std::string to = conv(a[a.size() > 1 ? 1 : 0], Ty::Int, line);
			std::string step = a.size() > 2 ? conv(a[2], Ty::Int, line) : "int64_t(1)";
			std::string i = temp(), e = temp(), s = temp();
			open = "{ const int64_t " + e + " = " + to + "; const int64_t " + s + " = " + step + ";\nfor (int64_t " + i + " = " + from + "; " + s + " > 0 ? " + i + " < " + e + " : (" + s + " < 0 && " + i + " > " + e + "); " + i + " += " + s + ") {\n";
			v = i;
		} else {
			E e = expr();
			std::string i = temp(), c = temp();
			if (e.t == Ty::Int) {
				open = "{ const int64_t " + c + " = " + e.c + ";\nfor (int64_t " + i + " = 0; " + i + " < " + c + "; " + i + "++) {\n";
				v = i;
			} else if (e.t == Ty::PF64 || e.t == Ty::PI64 || e.t == Ty::Arr) {
				open = "{ " + std::string(cty(e.t)) + " " + c + " = " + e.c + ";\nfor (int64_t " + i + " = 0; " + i + " < " + c + ".size(); " + i + "++) {\n";
				each = e.t == Ty::PF64 ? Ty::Float : e.t == Ty::PI64 ? Ty::Int : Ty::Var;
				v = c + (e.t == Ty::Arr ? "[" + i + "]" : ".ptr()[" + i + "]");
			} else {
				open = "{ Array " + c + " = gn::iter_list(" + conv(e, Ty::Var, line) + ", " + L(line) + ");\nfor (int64_t " + i + " = 0; " + i + " < " + c + ".size(); " + i + "++) {\n";
				each = Ty::Var;
				v = c + "[" + i + "]";
			}
		}
		expect(":");
		out += open;
		scopes.emplace_back();
		declare(n, each, false, "");
		out += std::string(cty(each)) + " l_" + n + " = " + v + ";\n";
		block();
		scopes.pop_back();
		out += "} }\n";
	}

	std::string pattern(const std::string &v, int line) {
		if (is("_") ) {
			p++;
			return "true";
		}
		if (eat("var")) {
			std::string n = ident();
			declare(n, Ty::Var, false, "");
			return "(l_" + n + " = " + v + ", true)";
		}
		if (is("[")) {
			p++;
			std::string body;
			int count = 0;
			bool rest = false;
			while (!is("]")) {
				if (is(".") && t[p + 1].s == ".") {
					p += 2;
					rest = true;
				} else {
					body += " && " + pattern("gn::get_index(" + v + ", Variant(int64_t(" + std::to_string(count) + ")), " + L(line) + ")", line);
					count++;
				}
				if (!eat(",")) {
					break;
				}
			}
			expect("]");
			return "(gn::match_array(" + v + ", " + std::to_string(count) + ", " + (rest ? "true" : "false") + ")" + body + ")";
		}
		if (is("{")) {
			p++;
			std::string body;
			int count = 0;
			bool rest = false;
			while (!is("}")) {
				if (is(".") && t[p + 1].s == ".") {
					p += 2;
					rest = true;
				} else {
					std::string key = conv(expr(), Ty::Var, line);
					count++;
					body += " && gn::has_key(" + v + ", " + key + ")";
					if (eat(":")) {
						body += " && " + pattern("gn::get_index(" + v + ", " + key + ", " + L(line) + ")", line);
					}
				}
				if (!eat(",")) {
					break;
				}
			}
			expect("}");
			return "(gn::match_dict(" + v + ", " + std::to_string(count) + ", " + (rest ? "true" : "false") + ")" + body + ")";
		}
		return "gn::match_value(" + v + ", " + conv(e_or(), Ty::Var, line) + ", " + L(line) + ")";
	}

	void match_stmt() {
		int line = ln();
		E value = expr();
		expect(":");
		nl();
		if (tk().k != TK::Indent) {
			throw Err{ ln(), "expected an indented block" };
		}
		p++;
		std::string v = temp(), done = temp();
		out += "{ Variant " + v + " = " + conv(value, Ty::Var, line) + "; bool " + done + " = false;\n";
		while (tk().k != TK::Dedent) {
			int arm = ln();
			scopes.emplace_back();
			std::string test;
			do {
				test += (test.empty() ? "" : " || ") + pattern(v, arm);
			} while (eat(","));
			std::string bound;
			for (const auto &kv : scopes.back()) {
				bound += "Variant l_" + kv.first + ";\n";
			}
			if (eat("when")) {
				test = "(" + test + ") && " + truth(expr());
			}
			expect(":");
			out += "{\n" + bound + "if (!" + done + " && (" + test + ")) {\n" + done + " = true;\n";
			block();
			out += "}\n}\n";
			scopes.pop_back();
		}
		p++;
		out += "}\n";
	}

	void if_stmt() {
		E c = expr();
		expect(":");
		out += "if (" + truth(c) + ") ";
		block();
		int closes = 0;
		while (eat("elif")) {
			size_t mark = out.size();
			E e = expr();
			expect(":");
			std::string hoisted = out.substr(mark);
			out.erase(mark);
			if (hoisted.empty()) {
				out += "else if (" + truth(e) + ") ";
			} else {
				out += "else {\n" + hoisted + "if (" + truth(e) + ") ";
				closes++;
			}
			block();
		}
		if (eat("else")) {
			expect(":");
			out += "else ";
			block();
		}
		out += std::string(closes, '}');
	}

	void stmt() {
		int line = ln();
		if (is("@")) {
			if (t[p + 1].s.rfind("warning_ignore", 0) != 0) {
				throw Err{ line, "annotation @" + t[p + 1].s + " is not supported on a statement" };
			}
			p = past_annotation(p);
			if (tk().k == TK::NL) {
				p++;
			}
			return;
		}
		if (eat("pass")) {
			nl();
			return;
		}
		if (eat("var")) {
			var_stmt();
			return;
		}
		if (eat("for")) {
			for_stmt();
			return;
		}
		if (eat("if")) {
			if_stmt();
			return;
		}
		if (eat("while")) {
			size_t mark = out.size();
			E c = expr();
			expect(":");
			std::string hoisted = out.substr(mark);
			out.erase(mark);
			if (hoisted.empty()) {
				out += "while (" + truth(c) + ") ";
				block();
			} else {
				out += "while (true) {\n" + hoisted + "if (!" + truth(c) + ") { break; }\n";
				block();
				out += "}\n";
			}
			return;
		}
		if (eat("break")) {
			nl();
			out += "break;\n";
			return;
		}
		if (eat("continue")) {
			nl();
			out += "continue;\n";
			return;
		}
		if (eat("return")) {
			if (tk().k == TK::NL) {
				p++;
				out += cur->coroutine ? "co_return Variant();\n" : cur->ret == Ty::Void ? "return;\n" : "return {};\n";
				return;
			}
			E e = expr();
			nl();
			std::string value = guarded(e, cur->declared, cur->ret_ref, line);
			out += cur->coroutine ? "co_return Variant(" + value + ");\n" : cur->ret == Ty::Var ? "return Variant(" + value + ");\n" : "return " + value + ";\n";
			return;
		}
		if (eat("match")) {
			match_stmt();
			return;
		}
		if (eat("const")) {
			std::string n = ident();
			Ty ty = Ty::Void;
			if (eat(":")) {
				ty = parse_type();
			}
			if (!eat(":=")) {
				expect("=");
			}
			E e = expr();
			nl();
			ty = ty == Ty::Void ? e.t : ty;
			declare(n, ty, false, "");
			out += std::string("const ") + cty(ty) + " l_" + n + " = " + conv(e, ty, line) + ";\n";
			return;
		}
		if (is("assert") && t[p + 1].s == "(") {
			p += 2;
			std::vector<E> a = args();
			nl();
			if (a.empty() || a.size() > 2) {
				throw Err{ line, "assert takes one or two arguments" };
			}
			out += "\n#ifdef DEBUG_ENABLED\nif (!" + truth(a[0]) + ") { gn::fail(\"Assertion failed.\", " + L(line) + "); }\n#endif\n";
			return;
		}
		E l = expr();
		static const std::set<std::string> assign_ops = { "=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=", "**=" };
		if (tk().k == TK::Op && assign_ops.count(tk().s)) {
			std::string op = t[p++].s;
			E r = expr();
			nl();
			if (op != "=") {
				r = bin(op.substr(0, op.size() - 1), l, r, line);
			}
			assign(l, r, line);
			return;
		}
		nl();
		out += l.c + ";\n";
	}

	void skip_line() {
		int depth = 0;
		while (tk().k != TK::NL || depth > 0) {
			if (tk().k == TK::Eof) {
				return;
			}
			depth += tk().s == "(" || tk().s == "[" || tk().s == "{";
			depth -= tk().s == ")" || tk().s == "]" || tk().s == "}";
			p++;
		}
		p++;
	}

	void skip_indented() {
		int d = 0;
		do {
			d += tk().k == TK::Indent;
			d -= tk().k == TK::Dedent;
			p++;
		} while (d > 0);
	}

	void skip_block() {
		bool one_line = tk().k != TK::NL;
		skip_line();
		if (one_line) {
			return;
		}
		if (tk().k != TK::Indent) {
			throw Err{ ln(), "expected an indented block" };
		}
		skip_indented();
	}

	void skip_value() {
		skip_line();
		if (tk().k == TK::Indent) {
			skip_indented();
		}
	}

	void finish_fn(Fn &f) {
		expect(":");
		f.body = p;
		skip_block();
		f.declared = f.ret;
		f.coroutine = awaits_between(f.body, p);
		if (f.coroutine) {
			f.ret = Ty::Var;
			f.async_shape = true;
		}
		fns.push_back(f);
	}

	size_t property_colon() const {
		int depth = 0;
		for (size_t q = p; t[q].k != TK::Eof && (t[q].k != TK::NL || depth > 0); q++) {
			if (t[q].k == TK::Id && t[q].s == "func") {
				return 0;
			}
			depth += t[q].s == "(" || t[q].s == "[" || t[q].s == "{";
			depth -= t[q].s == ")" || t[q].s == "]" || t[q].s == "}";
			if (depth == 0 && t[q].k == TK::Op && t[q].s == ":" && property_follows(q)) {
				return q;
			}
		}
		return 0;
	}

	bool property_follows(size_t colon) const {
		const Tok &next = t[colon + 1];
		return next.k == TK::NL || (next.k == TK::Id && (next.s == "get" || next.s == "set"));
	}

	void skip_initializer() {
		size_t colon = property_colon();
		if (colon) {
			p = colon;
		} else {
			skip_value();
		}
	}

	void property_accessor(Member &m, bool getter) {
		Fn f;
		f.line = ln();
		f.name = "gs_" + m.name + (getter ? "_get" : "_set");
		f.accessor_of = m.name;
		f.is_static = m.is_static;
		if (getter) {
			if (eat("(")) {
				expect(")");
			}
			m.getter = f.name;
		} else {
			expect("(");
			Param a;
			a.name = ident();
			expect(")");
			f.params.push_back(a);
			f.ret = Ty::Void;
			m.setter = f.name;
		}
		finish_fn(f);
	}

	void property_block(Member &m) {
		expect(":");
		bool block = tk().k == TK::NL;
		if (block) {
			while (tk().k == TK::NL) {
				p++;
			}
			if (tk().k != TK::Indent) {
				throw Err{ ln(), "expected an indented block" };
			}
			p++;
		}
		for (;;) {
			bool getter = is("get");
			if (!getter && !is("set")) {
				throw Err{ ln(), "expected get or set in property '" + m.name + "'" };
			}
			p++;
			bool more = false;
			if (eat("=")) {
				(getter ? m.getter : m.setter) = ident();
				more = eat(",");
				if (!more) {
					nl();
				}
			} else {
				property_accessor(m, getter);
			}
			if (!block && !more) {
				return;
			}
			while (block && tk().k == TK::NL) {
				p++;
			}
			if (block && tk().k == TK::Dedent) {
				p++;
				return;
			}
		}
	}

	void top_level() {
		while (tk().k != TK::Eof) {
			int line = ln();
			bool stat = eat("static");
			if (tk().k == TK::NL) {
				p++;
			} else if (eat("signal")) {
				signals.push_back(ident());
				if (eat("(")) {
					int depth = 1;
					while (depth > 0) {
						depth += is("(");
						depth -= is(")");
						p++;
					}
				}
				nl();
			} else if (eat("enum")) {
				std::string name = tk().k == TK::Id ? ident() : "";
				if (!name.empty()) {
					enums.insert(name);
				}
				expect("{");
				std::string prev;
				while (!is("}")) {
					Member m;
					m.name = (name.empty() ? "" : name + ".") + ident();
					m.line = line;
					m.t = Ty::Int;
					m.declared = true;
					if (eat("=")) {
						m.init = p;
						int depth = 0;
						while (depth > 0 || (!is(",") && !is("}"))) {
							depth += is("(");
							depth -= is(")");
							p++;
						}
					} else {
						m.enum_auto = true;
						m.enum_prev = prev;
					}
					prev = m.name;
					pending_consts.push_back(m);
					if (!eat(",")) {
						break;
					}
				}
				expect("}");
				nl();
			} else if (eat("extends")) {
				if (tk().k == TK::Str) {
					extends_ref = classes.absolute(path, t[p++].s);
				} else {
					base = ident();
					if (!classes.class_exists(base)) {
						extends_ref = classes.global_class_path(base);
						if (extends_ref.empty()) {
							throw Err{ line, "unknown class " + base };
						}
					}
				}
				extends_line = line;
				nl();
			} else if (eat("class_name")) {
				global = ident();
				nl();
			} else if (eat("const")) {
				Member m;
				m.name = ident();
				m.line = line;
				if (eat(":")) {
					m.t = parse_type();
					m.declared = true;
				}
				if (!eat(":=")) {
					expect("=");
				}
				m.init = p;
				skip_value();
				pending_consts.push_back(m);
			} else if (eat("@")) {
				std::string a = ident();
				bool exported = a.compare(0, 6, "export") == 0;
				bool known = exported || a == "onready" || a == "tool" || a == "icon" || a == "static_unload" || a.rfind("warning_ignore", 0) == 0 || a == "abstract";
				if (!known) {
					throw Err{ line, "annotation @" + a + " is not supported in a native file" };
				}
				next_onready = next_onready || a == "onready";
				if (exported && a != "export_group" && a != "export_subgroup" && a != "export_category") {
					next_usage = a == "export_storage" ? 4098 : 4102;
				}
				p = past_annotation(p - 2);
				if (a == "abstract") {
					size_t q = p;
					while (t[q].k == TK::NL) {
						q++;
					}
					(t[q].s == "func" ? next_abstract : is_abstract) = true;
				}
			} else if (eat("var")) {
				Member m;
				m.name = ident();
				m.line = line;
				m.onready = next_onready;
				m.is_static = stat;
				m.usage = next_usage;
				next_onready = false;
				next_usage = 4096;
				if (eat(":=")) {
					m.init = p;
					m.declared = true;
					m.t = Ty::Void;
					skip_initializer();
				} else {
					if (is(":") && !property_follows(p)) {
						p++;
						m.t = parse_type();
						m.ref = last_ref;
						m.declared = true;
					}
					if (tk().k == TK::NL) {
						p++;
					} else if (!is(":")) {
						expect("=");
						m.init = p;
						skip_initializer();
					}
				}
				if (is(":")) {
					property_block(m);
				}
				if (!m.declared) {
					warn(line, "member '" + m.name + "' has no type and stays a Variant");
				}
				members.push_back(m);
			} else if (eat("func")) {
				Fn f;
				f.name = ident();
				f.line = line;
				f.abstract = next_abstract;
				f.is_static = stat;
				next_abstract = false;
				expect("(");
				while (!is(")")) {
					Param a;
					a.name = ident();
					a.t = Ty::Var;
					bool typed = false;
					if (eat(":")) {
						a.t = parse_type();
						a.ref = last_ref;
						typed = true;
					}
					if (is("=") || is(":=")) {
						if (is(":=")) {
							a.t = Ty::Void;
							typed = true;
						}
						p++;
						a.init = p;
						int depth = 0;
						while (depth > 0 || (!is(",") && !is(")"))) {
							depth += is("(") || is("[") || is("{");
							depth -= is(")") || is("]") || is("}");
							p++;
						}
					}
					if (!typed) {
						warn(line, "parameter '" + a.name + "' has no type and stays a Variant");
					}
					f.params.push_back(a);
					if (!eat(",")) {
						break;
					}
				}
				expect(")");
				if (eat("->")) {
					f.ret = parse_type();
					f.ret_ref = last_ref;
				} else {
					warn(line, "function '" + f.name + "' has no return type and returns a Variant");
				}
				if (f.abstract) {
					nl();
					f.declared = f.ret;
					fns.push_back(f);
					continue;
				}
				finish_fn(f);
			} else {
				throw Err{ line, "'" + tk().s + "' is not supported at class level in a native file" };
			}
		}
	}

	std::vector<Member> pending_consts;

	void resolve_top() {
		bool checked = unsafe;
		unsafe = true;
		for (const Member &m : pending_consts) {
			std::string id = m.name;
			std::replace(id.begin(), id.end(), '.', '_');
			E e;
			if (m.enum_auto) {
				e = { m.enum_prev.empty() ? "int64_t(0LL)" : "(" + consts[m.enum_prev].c + " + 1)", Ty::Int };
			} else {
				p = m.init;
				e = expr();
			}
			Ty ty = m.declared ? m.t : e.t;
			std::string value = conv(e, ty, m.line);
			if ((ty == Ty::Int || ty == Ty::Float || ty == Ty::Bool) && value.find("()") == std::string::npos) {
				out += std::string("\tstatic inline const ") + cty(ty) + " k_" + id + " = " + value + ";\n";
				consts[m.name] = { "k_" + id, ty };
				continue;
			}
			out += std::string("\tstatic const ") + cty(ty) + " &k_" + id + "();\n";
			const_defs += std::string("const ") + cty(ty) + " &C::k_" + id + "() {\n\tstatic const " + cty(ty) + " *v = new " + cty(ty) + "(" + value + ");\n\treturn *v;\n}\n\n";
			consts[m.name] = { "k_" + id + "()", ty };
		}
		unsafe = checked;
		for (Fn &f : fns) {
			scopes.assign(1, {});
			for (Param &a : f.params) {
				if (a.t == Ty::Void) {
					p = a.init;
					E e = expr();
					a.t = e.t;
					a.ref = e.ref;
				}
				declare(a.name, a.t, false, "", a.ref);
			}
		}
		scopes.assign(1, {});
		for (Member &m : members) {
			if (m.t == Ty::Void) {
				p = m.init;
				E e = expr();
				m.t = e.t;
				m.ref = e.ref;
			}
			for (Fn &f : fns) {
				if (f.accessor_of != m.name) {
					continue;
				}
				if (!f.params.empty()) {
					f.params[0].t = m.t;
					f.params[0].ref = m.ref;
				} else if (!f.coroutine) {
					f.ret = f.declared = m.t;
					f.ret_ref = m.ref;
				}
			}
			if ((!m.getter.empty() && !find_fn(m.getter)) || (!m.setter.empty() && !find_fn(m.setter))) {
				throw Err{ m.line, "property '" + m.name + "' names an unknown function" };
			}
		}
	}

	static std::string owner_local(const std::string &body) {
		return body.find("owner") == std::string::npos ? "" : "Object *owner = gn::script_object(GN_FILE);\n";
	}

	std::string emit_fn(const Fn &f) {
		if (f.abstract) {
			return "gn::fail(\"Cannot call abstract function '" + f.name + "'.\", " + L(f.line) + ");\n";
		}
		for (;;) {
			try {
				cur = &f;
				lambda_inline = 0;
				out.clear();
				tmp = 0;
				scopes.clear();
				scopes.emplace_back();
				for (const Param &a : f.params) {
					declare(a.name, a.t, false, "", a.ref);
				}
				p = f.body;
				scopes.emplace_back();
				if (tk().k != TK::NL) {
					stmt();
					break;
				}
				nl();
				if (tk().k != TK::Indent) {
					throw Err{ ln(), "expected an indented block" };
				}
				p++;
				while (tk().k != TK::Dedent) {
					stmt();
				}
				break;
			} catch (const Retry &) {
			}
		}
		std::string body = (f.is_static ? owner_local(out) : "") + out;
		if (f.coroutine) {
			return body + "co_return Variant();\n";
		}
		std::string fail = f.ret == Ty::Void ? "return;" : "return {};";
		std::string tail = f.ret == Ty::Void ? "" : "return {};\n";
		if (unsafe) {
			return body + tail;
		}
		return "try {\n" + body + tail + "} catch (const gn::Abort &) { " + fail + " }\n";
	}

public:
	Compiler *parent = nullptr;
	std::string extends_ref, chain;
	int extends_line = 0;

	Compiler(std::vector<Tok> toks, std::string p_path, const ProjectQuery &q, bool p_unsafe) :
			t(std::move(toks)), path(std::move(p_path)), classes(q), unsafe(p_unsafe) {}

	void scan() {
		scan_warning_ignores();
		top_level();
	}
	const std::string &script_path() const { return path; }
	std::string file_id() const { return sanitize(path); }
	const std::set<std::string> &warning_set() const { return warnings; }

	void resolve() {
		if (resolved) {
			return;
		}
		resolved = true;
		if (parent) {
			parent->resolve();
		}
		out.clear();
		init_fn.name = "init";
		cur = &init_fn;
		scopes.assign(1, {});
		resolve_top();
		const_block = out;
	}

	void collect_virtuals(std::set<std::string> &v) {
		if (!parent) {
			return;
		}
		for (Fn &f : fns) {
			Fn *up = const_cast<Fn *>(parent->find_fn(f.name));
			if (f.name == "_init" || !up || f.is_static || up->is_static) {
				continue;
			}
			if (f.async_shape != up->async_shape) {
				f.async_shape = up->async_shape = true;
				f.ret = up->ret = Ty::Var;
			}
			bool same = up->ret == f.ret && up->params.size() == f.params.size();
			for (size_t i = 0; same && i < f.params.size(); i++) {
				same = up->params[i].t == f.params[i].t;
			}
			if (!same) {
				throw Err{ f.line, "override of '" + f.name + "' must keep the same parameter and return types as its parent" };
			}
			v.insert(f.name);
		}
	}

	std::string arg_value(const Fn &f, const Param &a, int i) const {
	 	std::string index = "a[" + std::to_string(i) + "]";
		std::string loc = "gn::Loc{ \"" + path + "\", \"" + f.name + "\", " + std::to_string(f.line) + " }";
		if (a.t == Ty::Var) {
			return guard("*" + index, a.ref, loc);
		}
		if (unsafe) {
			return index + "->operator " + cty(a.t) + "()";
		}
		return guard(std::string("gn::cast<") + cty(a.t) + ">(*" + index + ", Variant::" + vty(a.t) + ", " + loc + ")", a.ref, loc);
	}

	std::string dispatch(const Fn &f, const std::string &target, bool keep) const {
		std::string body;
		int total = (int)f.params.size(), least = f.required();
		for (int n = total; n >= least; n--) {
			std::string call = target + "(";
			for (int i = 0; i < n; i++) {
				call += (i ? ", " : "") + arg_value(f, f.params[i], i);
			}
			call += ")";
			std::string one = keep && f.ret != Ty::Void ? "r = Variant(" + call + ");" : call + ";" + (keep ? " r = Variant();" : "");
			body += total == least ? one : "if (n == " + std::to_string(n) + ") { " + one + " return; } ";
		}
		return body;
	}

	void emit_default_overloads(const Fn &f, std::string &decl, std::string &defs) {
		cur = &f;
		for (size_t count = f.required(); count < f.params.size(); count++) {
			std::string list, pass;
			scopes.assign(1, {});
			for (size_t i = 0; i < count; i++) {
				const Param &a = f.params[i];
				list += std::string(i ? ", " : "") + cty(a.t) + " l_" + a.name;
				pass += std::string(i ? ", " : "") + "l_" + a.name;
				declare(a.name, a.t, false, "", a.ref);
			}
			const Param &next = f.params[count];
			p = next.init;
			E value = expr();
			pass += std::string(count ? ", " : "") + guarded(value, next.t, next.ref, f.line);
			decl += std::string(f.is_static ? "\tstatic " : "\t") + cty(f.ret) + " f_" + f.name + "(" + list + ");\n";
			defs += std::string(cty(f.ret)) + " C::f_" + f.name + "(" + list + ") {\n\treturn f_" + f.name + "(" + pass + ");\n}\n\n";
		}
		scopes.assign(1, {});
	}

	void emit(const std::set<std::string> &virtuals, std::string &header, std::string &source) {
		const std::string id = file_id(), self = struct_name();
		std::string decl, defs, mtab, ptab, stab, members_init, ready_init, statics_init, statics_clear;
		static_fn.name = "static_init";
		static_fn.is_static = true;
		for (const Member &m : members) {
			if (m.is_static) {
				const std::string ty = cty(m.t), s_name = "s_" + m.name;
				decl += "\tstatic " + ty + " &" + s_name + "();\n";
				defs += ty + " &C::" + s_name + "() {\n\tstatic " + ty + " *v = new " + ty + "();\n\treturn *v;\n}\n\n";
				statics_clear += s_name + "() = " + ty + "();\n";
				cur = &static_fn;
				if (m.init) {
					p = m.init;
					E e = expr();
					statics_init += s_name + "() = " + guarded(e, m.t, m.ref, m.line) + ";\n";
				} else if (!m.ref.elem.empty()) {
					statics_init += s_name + "() = gn::typed_array(Array(), " + m.ref.elem + ", " + L(m.line) + ");\n";
				}
				cur = &init_fn;
				continue;
			}
			decl += std::string("\t") + cty(m.t) + " m_" + m.name + "{};\n";
			if (m.init) {
				p = m.init;
				E e = expr();
				(m.onready ? ready_init : members_init) += "m_" + m.name + " = " + guarded(e, m.t, m.ref, m.line) + ";\n";
			} else if (!m.ref.elem.empty()) {
				members_init += "m_" + m.name + " = gn::typed_array(Array(), " + m.ref.elem + ", " + L(m.line) + ");\n";
			}
		}
		for (const Fn &f : fns) {
			std::string sig = std::string(cty(f.ret)) + " C::f_" + f.name + "(";
			std::string d = std::string("\t") + (f.is_static ? "static " : virtuals.count(f.name) ? "virtual " : "") + cty(f.ret) + " f_" + f.name + "(";
			int i = 0;
			std::string names;
			for (const Param &a : f.params) {
				std::string one = std::string(i++ ? ", " : "") + cty(a.t) + " l_" + a.name;
				sig += one;
				d += one;
				names += std::string(i > 1 ? ", " : "") + "l_" + a.name;
			}
			decl += d + ");\n";
			if (f.coroutine) {
				std::string params = sig.substr(sig.find('(') + 1);
				decl += std::string(f.is_static ? "\tstatic " : "\t") + "gn::Task cf_" + f.name + "(" + params + ");\n";
				defs += sig + ") {\n\treturn gn::run(cf_" + f.name + "(" + names + "));\n}\n\n";
				defs += "gn::Task C::cf_" + f.name + "(" + params + ") {\n" + emit_fn(f) + "}\n\n";
			} else {
				defs += sig + ") {\n" + emit_fn(f) + "}\n\n";
			}
			emit_default_overloads(f, decl, defs);
		}

		std::vector<const Compiler *> lineage;
		for (const Compiler *c = this; c; c = c->parent) {
			lineage.insert(lineage.begin(), c);
		}
		std::map<std::string, const Fn *> table;
		std::vector<std::string> all_signals;
		int props = 0, statics = 0, rows = 0;
		bool has_onready = false;
		std::string notify, constants;
		for (const Compiler *c : lineage) {
			all_signals.insert(all_signals.end(), c->signals.begin(), c->signals.end());
			for (const auto &kv : c->consts) {
				size_t dot = kv.first.find('.');
				if (dot == std::string::npos) {
					constants += "\td[String(\"" + kv.first + "\")] = Variant(C::" + kv.second.c + ");\n";
				} else {
					std::string group = "String(\"" + kv.first.substr(0, dot) + "\")";
					constants += "\t{ Dictionary e = d.get(" + group + ", Dictionary()); e[String(\"" + kv.first.substr(dot + 1) + "\")] = Variant(C::" + kv.second.c + "); d[" + group + "] = e; }\n";
				}
			}
			for (const Fn &f : c->fns) {
				if (f.name == "_notification") {
					notify += "((C *)s)->" + c->struct_name() + "::f__notification(int64_t(*a[0])); ";
				} else if (f.name != "_init" && f.accessor_of.empty()) {
					table[f.name] = &f;
				}
			}
			for (const Member &m : c->members) {
				has_onready = has_onready || m.onready;
				std::string loc = "gn::Loc{ \"" + c->path + "\", \"" + m.name + "\", " + std::to_string(m.line) + " }";
				if (m.is_static) {
					std::string v = c->guard(m.t == Ty::Var ? "v" : c->unsafe ? std::string("v.operator ") + cty(m.t) + "()" : std::string("gn::cast<") + cty(m.t) + ">(v, Variant::" + vty(m.t) + ", " + loc + ")", m.ref, loc);
					std::string slot = c->struct_name() + "::s_" + m.name + "()";
					std::string read = m.getter.empty() ? slot : "C::f_" + m.getter + "()";
					std::string write = m.setter.empty() ? slot + " = " + v : "C::f_" + m.setter + "(" + v + ")";
					stab += "\t{ \"" + m.name + "\", (GDExtensionVariantType)Variant::" + vty(m.t) + ", [](void *, Variant &r) { r = Variant(" + read + "); }, [](void *, const Variant &v) { " + write + "; }, 0 },\n";
					statics++;
					continue;
				}
				std::string setv = c->guard(m.t == Ty::Var ? "v" : c->unsafe ? std::string("v.operator ") + cty(m.t) + "()" : std::string("gn::cast<") + cty(m.t) + ">(v, Variant::" + vty(m.t) + ", " + loc + ")", m.ref, loc);
				std::string read = "((C *)s)->" + (m.getter.empty() ? "m_" + m.name : "f_" + m.getter + "()");
				std::string write = m.setter.empty() ? "((C *)s)->m_" + m.name + " = " + setv : "((C *)s)->f_" + m.setter + "(" + setv + ")";
				ptab += "\t{ \"" + m.name + "\", (GDExtensionVariantType)Variant::" + vty(m.t) + ", [](void *s, Variant &r) { r = Variant(" + read + "); }, [](void *s, const Variant &v) { " + write + "; }, " + std::to_string(m.usage) + " },\n";
				props++;
			}
		}
		for (const auto &kv : table) {
			const Fn &f = *kv.second;
			std::string body = (has_onready && f.name == "_ready" ? "((C *)s)->ready_init(); " : "") + dispatch(f, (f.is_static ? "C::f_" : "((C *)s)->f_") + f.name, true);
			mtab += "\t{ \"" + f.name + "\", " + std::to_string(f.params.size()) + ", " + std::to_string(f.required()) + ", [](void *s, const Variant **a, int n, Variant &r) { " + body + " }, " + (f.is_static ? "true" : "false") + " },\n";
			rows++;
		}
		if (has_onready && !table.count("_ready")) {
			mtab += "\t{ \"_ready\", 0, 0, [](void *s, const Variant **a, int n, Variant &r) { ((C *)s)->ready_init(); r = Variant(); } },\n";
			rows++;
		}
		if (!notify.empty()) {
			mtab += "\t{ \"_notification\", 1, 1, [](void *s, const Variant **a, int n, Variant &r) { " + notify + "r = Variant(); } },\n";
			rows++;
		}

		std::string call_init;
		if (const Compiler *o = owner_of("_init")) {
			const Fn &f = *find_fn("_init");
			call_init = "\t\tif (n < " + std::to_string(f.required()) + " || n > " + std::to_string(f.params.size()) + ") {\n\t\t\tgn::fail(\"Wrong argument count calling _init.\", gn::Loc{ GN_FILE, \"_init\", " + std::to_string(f.line) + " });\n\t\t}\n";
			call_init += "\t\t" + dispatch(f, "c->" + o->struct_name() + "::f__init", false) + "\n";
		} else {
			call_init = "\t\tif (n != 0) {\n\t\t\tgn::fail(\"Too many arguments for this script's new().\", gn::Loc{ GN_FILE, \"new\", 1 });\n\t\t}\n";
		}
		std::string signal_list;
		for (const std::string &n : all_signals) {
			signal_list += "\"" + n + "\", ";
		}

		header = "// Generated by GodotNative from " + path + ". Do not edit.\n#pragma once\n#include <gn_runtime.h>\n";
		header += "#include <godot_cpp/classes/" + class_header(native()) + ".hpp>\n#include <godot_cpp/variant/builtin_types.hpp>\n#include <cstdlib>\n";
		if (parent) {
			header += "#include \"" + parent->file_id() + ".gen.h\"\n";
		}
		header += "\nnamespace gn_gen {\n\nusing namespace godot;\n\nstruct " + self + (parent ? " : " + parent->struct_name() : "") + " {\n";
		header += parent ? "" : "\tObject *owner = nullptr;\n";
		header += const_block + decl + "\tvoid init();\n\tvoid ready_init();\n\tstatic void static_init();\n\tstatic void static_clear();\n};\n\n} // namespace gn_gen\n";

		source = "// Generated by GodotNative from " + path + ". Do not edit.\n#define GN_FILE \"" + path + "\"\n#include \"" + id + ".gen.h\"\n\nnamespace gn_gen {\n\nusing C = " + self + ";\n\n";
		source += const_defs + "void C::init() {\n" + (parent ? parent->struct_name() + "::init();\n" : std::string()) + members_init + "}\n\n";
		source += "void C::ready_init() {\n" + (parent ? parent->struct_name() + "::ready_init();\n" : std::string()) + ready_init + "}\n\n" + defs;
		bool has_static_init = std::any_of(fns.begin(), fns.end(), [](const Fn &f) { return f.name == "_static_init"; });
		source += "void C::static_init() {\n\tstatic bool done = false;\n\tif (done) {\n\t\treturn;\n\t}\n\tdone = true;\n" + (parent ? parent->struct_name() + "::static_init();\n" : std::string()) + owner_local(statics_init) + statics_init + (has_static_init ? "f__static_init();\n" : "") + "}\n\n";
		source += "void C::static_clear() {\n" + statics_clear + "}\n\n";
		source += "namespace {\n\nconst gn::Method methods[] = {\n" + mtab + "\t{ nullptr, 0, 0, nullptr },\n};\n";
		source += "const gn::Prop props[] = {\n" + ptab + "\t{ nullptr, GDEXTENSION_VARIANT_TYPE_NIL, nullptr, nullptr, 0 },\n};\n";
		source += "const char *const signal_names[] = { " + signal_list + "nullptr };\n";
		source += "const gn::Prop statics[] = {\n" + stab + "\t{ nullptr, GDEXTENSION_VARIANT_TYPE_NIL, nullptr, nullptr, 0 },\n};\n\n";
		source += "void constants(Dictionary &d) {\n" + constants + "}\n\n";
		source += "void *create(Object *owner, const Variant **a, int n) {\n\tC *c = new C();\n\tc->owner = owner;\n\tauto run = [&]() {\n\t\tc->init();\n" + call_init + "\t};\n\ttry {\n\t\trun();\n\t} catch (const gn::Abort &) {\n\t}\n\treturn c;\n}\n\n";
		source += "void destroy(void *self) { delete (C *)self; }\n\n";
		source += "const gn::Class klass = { \"" + path + "\", \"" + native() + "\", \"" + global + "\", create, destroy, methods, " + std::to_string(rows) + ", props, " + std::to_string(props) + ", " + (parent ? "\"" + parent->path + "\"" : std::string("nullptr")) + ", signal_names, " + std::to_string(all_signals.size()) + ", " + (is_abstract ? "true" : "false") + ", statics, " + std::to_string(statics) + ", C::static_init, C::static_clear, constants };\n";
		source += "gn::Registrar reg(&klass);\n\n} // namespace\n\n} // namespace gn_gen\n";
	}
};

struct ProjectError {
	std::string msg;
};

class Project {
	const ProjectQuery &query;
	std::map<std::string, std::unique_ptr<Compiler>> units;
	std::vector<Compiler *> order;

	Compiler *load(const std::string &path, const std::string &chain) {
		auto found = units.find(path);
		std::string where = chain.empty() ? path : chain + " -> " + path;
		if (found != units.end()) {
			if (std::find(order.begin(), order.end(), found->second.get()) == order.end()) {
				throw ProjectError{ where + ": inheritance cycle" };
			}
			return found->second.get();
		}
		std::string src;
		if (!query.read(path, src)) {
			throw ProjectError{ where + ": could not read the file" };
		}
		Compiler *c = nullptr;
		try {
			auto u = std::make_unique<Compiler>(tokenize(src), path, query, first_line(src) == "#pragma native unsafe");
			c = u.get();
			units[path] = std::move(u);
			c->chain = where;
			c->scan();
		} catch (const Err &e) {
			throw ProjectError{ where + ":" + std::to_string(e.line) + ": " + e.msg };
		}
		if (!c->extends_ref.empty()) {
			c->parent = load(c->extends_ref, where);
		}
		order.push_back(c);
		return c;
	}

	template <typename F>
	void guarded(Compiler *c, F f) {
		try {
			f();
		} catch (const Err &e) {
			throw ProjectError{ c->chain + ":" + std::to_string(e.line) + ": " + e.msg };
		}
	}

public:
	explicit Project(const ProjectQuery &q) :
			query(q) {}

	void run(const std::vector<std::string> &toggled, Output &r) {
		for (const std::string &path : toggled) {
			load(path, "");
		}
		std::set<std::string> virtuals;
		for (Compiler *c : order) {
			guarded(c, [&]() {
				c->resolve();
				c->collect_virtuals(virtuals);
				c->collect_virtuals(virtuals);
			});
		}
		for (Compiler *c : order) {
			std::string h, cpp;
			guarded(c, [&]() { c->emit(virtuals, h, cpp); });
			r.files.push_back({ c->file_id() + ".gen.h", h });
			r.files.push_back({ c->file_id() + ".cpp", cpp });
			r.warnings.insert(r.warnings.end(), c->warning_set().begin(), c->warning_set().end());
			if (std::find(toggled.begin(), toggled.end(), c->script_path()) == toggled.end()) {
				r.implied.push_back(c->script_path());
			}
		}
	}
};

} // namespace

bool is_toggled(const std::string &source) {
	std::string l = first_line(source);
	return l == "#pragma native" || l == "#pragma native unsafe";
}

Output transpile_project(const std::vector<std::string> &toggled, const ProjectQuery &query) {
	Output r;
	try {
		Project(query).run(toggled, r);
		r.ok = true;
	} catch (const ProjectError &e) {
		r.error = e.msg;
	}
	return r;
}

} // namespace gn
