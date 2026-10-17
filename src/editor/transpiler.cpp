#include "transpiler.h"

#include <cstring>
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
			}
			bol = false;
		}
		char c = src[i];
		if (c == '\n') {
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
			if (src.compare(i, 3, std::string(3, c)) == 0) {
				throw Err{ line, "multiline strings are not supported" };
			}
			std::string s;
			i++;
			while (i < src.size() && src[i] != c) {
				if (src[i] == '\n') {
					throw Err{ line, "unterminated string" };
				}
				if (src[i] == '\\') {
					s += src[i++];
				}
				s += src[i++];
			}
			i++;
			out.push_back({ TK::Str, s, line });
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

struct E {
	std::string c;
	Ty t = Ty::Var;
	std::string set;
	bool lv = false;
	std::string local;
};

struct Param {
	std::string name;
	Ty t;
};

struct Fn {
	std::string name;
	std::vector<Param> params;
	Ty ret = Ty::Var;
	size_t body = 0;
	int line = 0;
};

struct Member {
	std::string name;
	Ty t = Ty::Var;
	bool declared = false;
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

class Compiler {
	std::vector<Tok> t;
	size_t p = 0;
	const std::string path;
	const ClassQuery &classes;
	bool unsafe;

	std::string base = "RefCounted", global;
	std::vector<Fn> fns;
	std::vector<Member> members;
	std::map<std::string, Const> consts;
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
		if (tk().k != TK::NL) {
			throw Err{ ln(), "expected end of line, found '" + tk().s + "'" };
		}
		p++;
	}
	std::string L(int line) const { return "GN_L(" + std::to_string(line) + ")"; }
	std::string temp() { return "t_" + std::to_string(tmp++); }
	void warn(int line, const std::string &m) { warnings.insert(path + ":" + std::to_string(line) + ": " + m); }

	static bool numeric(Ty x) { return x == Ty::Int || x == Ty::Float; }

	Ty parse_type() {
		std::string n = ident();
		if (eat("[")) {
			parse_type();
			expect("]");
		}
		static const std::map<std::string, Ty> m = { { "void", Ty::Void }, { "bool", Ty::Bool }, { "int", Ty::Int }, { "float", Ty::Float }, { "String", Ty::Str }, { "Vector2", Ty::V2 }, { "Vector3", Ty::V3 }, { "PackedFloat64Array", Ty::PF64 }, { "PackedInt64Array", Ty::PI64 }, { "Array", Ty::Arr }, { "Dictionary", Ty::Dict } };
		auto it = m.find(n);
		return it == m.end() ? Ty::Var : it->second;
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
		while (is("<") || is(">") || is("==") || is("!=") || is("<=") || is(">=") || is("in")) {
			int line = ln();
			std::string op = t[p++].s;
			E b = e_bit(0);
			a = bin(op, a, b, line);
		}
		return a;
	}
	E e_bit(int level) {
		static const std::vector<std::vector<std::string>> lv = { { "|" }, { "^" }, { "&" }, { "<<", ">>" }, { "+", "-" }, { "*", "/", "%" } };
		if (level == (int)lv.size()) {
			return e_unary();
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
		return nullptr;
	}

	const Fn *find_fn(const std::string &n) const {
		for (const Fn &f : fns) {
			if (f.name == n) {
				return &f;
			}
		}
		return nullptr;
	}

	E self_call(const Fn *f, const std::vector<E> &a, int line) {
		if (a.size() != f->params.size()) {
			throw Err{ line, "wrong argument count calling " + f->name };
		}
		std::string s = "f_" + f->name + "(";
		for (size_t i = 0; i < a.size(); i++) {
			s += (i ? ", " : "") + conv(a[i], f->params[i].t, line);
		}
		return { s + ")", f->ret };
	}

	E native_call(const std::string &n, NativeMember m, const std::vector<E> &a, int line) {
		if (m.direct && a.size() <= m.args.size() && a.size() + m.defaults >= m.args.size()) {
			std::string call = "static_cast<" + base + " *>(owner)->" + n + "(";
			for (size_t i = 0; i < a.size(); i++) {
				call += (i ? ", " : "") + conv(a[i], m.args[i], line);
			}
			call += ")";
			return { m.type == Ty::Void ? call : std::string(cty(m.type)) + "(" + call + ")", m.type };
		}
		std::string list = var_args(a, line);
		std::string call = "owner->call(GN_SN(\"" + n + "\")" + (list.empty() ? "" : ", " + list) + ")";
		if (m.type == Ty::Void) {
			return { call, Ty::Void };
		}
		if (m.type == Ty::Var) {
			return { call, Ty::Var };
		}
		return { "(" + call + ").operator " + cty(m.type) + "()", m.type };
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
			r = { "int64_t(gn::mcall(" + conv(a[0], Ty::Var, line) + ", GN_SN(\"size\"), {}, " + L(line) + "))", Ty::Int };
			return true;
		}
		auto c = ctors.find(n);
		if (c != ctors.end()) {
			p += 2;
			std::vector<E> a = args();
			Ty each = c->second == Ty::V3 || c->second == Ty::V2 ? Ty::Float : Ty::Void;
			std::string s = std::string(cty(c->second)) + "(";
			for (size_t i = 0; i < a.size(); i++) {
				if (each == Ty::Void) {
					throw Err{ line, n + " constructor arguments are not supported" };
				}
				s += (i ? ", " : "") + ("real_t(" + conv(a[i], each, line) + ")");
			}
			r = { s + ")", c->second };
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
					return { s + " return " + v + "; }())", Ty::Arr };
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
		if (n == "await" || n == "func" || n == "super" || n == "yield" || n == "preload") {
			throw Err{ line, "'" + n + "' is not supported in a native file" };
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
			return { n == "PI" ? "double(Math_PI)" : "double(Math_TAU)", Ty::Float };
		}
		if (n == "self") {
			p++;
			if (is(".")) {
				p++;
				std::string m = ident();
				return is("(") ? named(m, line) : field(m, line);
			}
			return { "Variant(owner)", Ty::Var };
		}
		if (n == "Vector3" && t[p + 1].s == ".") {
			p += 2;
			std::string m = ident();
			if (m == "ZERO") {
				return { "Vector3()", Ty::V3 };
			}
			if (m == "ONE") {
				return { "Vector3(1, 1, 1)", Ty::V3 };
			}
			if (m == "UP") {
				return { "Vector3(0, 1, 0)", Ty::V3 };
			}
			throw Err{ line, "unsupported Vector3 constant " + m };
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
		NativeMember m = classes.method(base, n);
		if (!m.found) {
			throw Err{ line, "unknown function " + n };
		}
		return native_call(n, m, a, line);
	}

	E field(const std::string &n, int line) {
		if (const Local *l = find_local(n)) {
			return { l->c, l->t, "(" + l->c + " = %v)", true, n };
		}
		if (const Member *m = find_member(n)) {
			return { "m_" + n, m->t, "(m_" + n + " = %v)", true };
		}
		auto c = consts.find(n);
		if (c != consts.end()) {
			return { c->second.c, c->second.t };
		}
		NativeMember m = classes.property(base, n);
		if (m.found) {
			std::string get = "owner->get(GN_SN(\"" + n + "\"))";
			E e{ m.type == Ty::Var ? get : "(" + get + ").operator " + cty(m.type) + "()", m.type };
			e.set = "owner->set(GN_SN(\"" + n + "\"), Variant(%v))";
			return e;
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
		std::string list = var_args(a, line);
		return { "gn::mcall(" + conv(b, Ty::Var, line) + ", GN_SN(\"" + n + "\"), {" + list + "}, " + L(line) + ")", Ty::Var };
	}

	E attr(const E &b, const std::string &n, int line) {
		if ((b.t == Ty::V3 && (n == "x" || n == "y" || n == "z")) || (b.t == Ty::V2 && (n == "x" || n == "y"))) {
			E e{ "double(" + b.c + "." + n + ")", Ty::Float };
			if (b.lv) {
				e.set = "(" + b.c + "." + n + " = real_t(%v))";
			}
			return e;
		}
		return { "gn::get_named(" + conv(b, Ty::Var, line) + ", GN_SN(\"" + n + "\"), " + L(line) + ")", Ty::Var };
	}

	E index(const E &b, const E &i, int line) {
		bool packed = b.t == Ty::PF64 || b.t == Ty::PI64;
		if ((packed || b.t == Ty::Arr) && b.lv) {
			std::string idx = conv(i, Ty::Int, line);
			if (!unsafe) {
				idx = "gn::index(" + idx + ", " + b.c + ".size(), " + L(line) + ")";
			}
			if (packed) {
				return { b.c + ".ptr()[" + idx + "]", b.t == Ty::PF64 ? Ty::Float : Ty::Int, "(" + b.c + ".ptrw()[" + idx + "] = %v)" };
			}
			return { b.c + "[" + idx + "]", Ty::Var, "(" + b.c + "[" + idx + "] = %v)" };
		}
		std::string v = conv(i, Ty::Var, line), sets;
		if (b.t == Ty::Var && b.lv) {
			sets = "gn::set_index(" + b.c + ", " + v + ", %v, " + L(line) + ")";
		} else {
			sets = "[&]() { Variant s = " + conv(b, Ty::Var, line) + "; gn::set_index(s, " + v + ", %v, " + L(line) + "); }()";
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
		std::string v = l.t == Ty::Var ? conv(r, Ty::Var, line) : conv(r, l.t, line);
		out += subst(l.set, v) + ";\n";
	}

	void declare(const std::string &n, Ty ty, bool weak, const std::string &key) {
		scopes.back()[n] = { "l_" + n, ty, weak, key };
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
		bool typed = false, infer = false;
		if (eat(":=")) {
			infer = true;
		} else if (eat(":")) {
			ty = parse_type();
			typed = true;
			if (eat("=")) {
				infer = false;
			} else {
				nl();
				declare(n, ty, false, "");
				out += std::string(cty(ty)) + " l_" + n + "{};\n";
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
		} else if (weak) {
			ty = demoted.count(key) ? Ty::Var : e.t;
			if (ty == Ty::Var) {
				warn(line, "could not infer a type for '" + n + "', so it stays a Variant");
			}
		}
		std::string v = conv(e, ty, line);
		declare(n, ty, weak && ty != Ty::Var, key);
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
				throw Err{ line, "for needs a range, an int or a typed array" };
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

	void if_stmt() {
		E c = expr();
		expect(":");
		out += "if (" + truth(c) + ") ";
		block();
		while (eat("elif")) {
			E e = expr();
			expect(":");
			out += "else if (" + truth(e) + ") ";
			block();
		}
		if (eat("else")) {
			expect(":");
			out += "else ";
			block();
		}
	}

	void stmt() {
		int line = ln();
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
			E c = expr();
			expect(":");
			out += "while (" + truth(c) + ") ";
			block();
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
				out += cur->ret == Ty::Void ? "return;\n" : "return {};\n";
				return;
			}
			E e = expr();
			nl();
			out += "return " + conv(e, cur->ret, line) + ";\n";
			return;
		}
		for (const char *no : { "match", "const", "assert", "await" }) {
			if (is(no)) {
				throw Err{ line, std::string("'") + no + "' is not supported in a native file" };
			}
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

	void skip_block() {
		skip_line();
		if (tk().k != TK::Indent) {
			throw Err{ ln(), "expected an indented block" };
		}
		int d = 0;
		do {
			d += tk().k == TK::Indent;
			d -= tk().k == TK::Dedent;
			p++;
		} while (d > 0);
	}

	void top_level() {
		while (tk().k != TK::Eof) {
			int line = ln();
			if (eat("extends")) {
				if (tk().k == TK::Str) {
					throw Err{ line, "extending a script is not supported yet" };
				}
				base = ident();
				if (!classes.class_exists(base)) {
					throw Err{ line, "extending script class " + base + " is not supported yet" };
				}
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
				skip_line();
				pending_consts.push_back(m);
			} else if (eat("@")) {
				std::string a = ident();
				if (a != "export") {
					throw Err{ line, "annotation @" + a + " is not supported in a native file" };
				}
			} else if (eat("var")) {
				Member m;
				m.name = ident();
				m.line = line;
				if (eat(":=")) {
					m.init = p;
					m.declared = true;
					m.t = Ty::Void;
					skip_line();
				} else {
					if (eat(":")) {
						m.t = parse_type();
						m.declared = true;
					}
					if (tk().k == TK::NL) {
						p++;
					} else {
						expect("=");
						m.init = p;
						skip_line();
					}
				}
				if (!m.declared) {
					warn(line, "member '" + m.name + "' has no type and stays a Variant");
				}
				members.push_back(m);
			} else if (eat("func")) {
				Fn f;
				f.name = ident();
				f.line = line;
				expect("(");
				while (!is(")")) {
					Param a;
					a.name = ident();
					a.t = Ty::Var;
					bool typed = false;
					if (eat(":")) {
						a.t = parse_type();
						typed = true;
					}
					if (is("=") || is(":=")) {
						throw Err{ line, "default arguments are not supported yet" };
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
				} else {
					warn(line, "function '" + f.name + "' has no return type and returns a Variant");
				}
				expect(":");
				f.body = p;
				skip_block();
				fns.push_back(f);
			} else {
				throw Err{ line, "'" + tk().s + "' is not supported at class level in a native file" };
			}
		}
	}

	std::vector<Member> pending_consts;

	void resolve_top() {
		for (const Member &m : pending_consts) {
			p = m.init;
			E e = expr();
			Ty ty = m.declared ? m.t : e.t;
			if (ty != Ty::Int && ty != Ty::Float && ty != Ty::Bool) {
				throw Err{ m.line, "only int, float and bool constants are supported" };
			}
			out += std::string("const ") + cty(ty) + " k_" + m.name + " = " + conv(e, ty, m.line) + ";\n";
			consts[m.name] = { "k_" + m.name, ty };
		}
		for (Member &m : members) {
			if (m.t == Ty::Void) {
				p = m.init;
				m.t = expr().t;
			}
		}
	}

	std::string emit_fn(const Fn &f) {
		cur = &f;
		for (;;) {
			try {
				out.clear();
				tmp = 0;
				scopes.clear();
				scopes.emplace_back();
				for (const Param &a : f.params) {
					declare(a.name, a.t, false, "");
				}
				p = f.body;
				nl();
				if (tk().k != TK::Indent) {
					throw Err{ ln(), "expected an indented block" };
				}
				p++;
				scopes.emplace_back();
				while (tk().k != TK::Dedent) {
					stmt();
				}
				break;
			} catch (const Retry &) {
			}
		}
		std::string body = out;
		std::string fail = f.ret == Ty::Void ? "return;" : "return {};";
		std::string tail = f.ret == Ty::Void ? "" : "return {};\n";
		if (unsafe) {
			return body + tail;
		}
		return "try {\n" + body + tail + "} catch (const gn::Abort &) { " + fail + " }\n";
	}

public:
	Compiler(std::vector<Tok> toks, std::string p_path, const ClassQuery &q, bool p_unsafe) :
			t(std::move(toks)), path(std::move(p_path)), classes(q), unsafe(p_unsafe) {}

	std::vector<std::string> warning_list() const { return { warnings.begin(), warnings.end() }; }

	std::string run() {
		top_level();
		out.clear();
		Fn init;
		init.name = "init";
		cur = &init;
		scopes.assign(1, {});
		resolve_top();
		std::string const_block = out;

		std::string decl, defs, wrappers, mtab, ptab, members_init;
		for (const Member &m : members) {
			decl += std::string("\t") + cty(m.t) + " m_" + m.name + "{};\n";
			std::string q = "\"" + m.name + "\"";
			std::string setv = m.t == Ty::Var ? "v" : std::string("v.operator ") + cty(m.t) + "()";
			ptab += "\t{ " + q + ", (GDExtensionVariantType)Variant::" + vty(m.t) + ", [](void *s, Variant &r) { r = Variant(((C *)s)->m_" + m.name + "); }, [](void *s, const Variant &v) { ((C *)s)->m_" + m.name + " = " + setv + "; } },\n";
			if (m.init) {
				p = m.init;
				E e = expr();
				members_init += "m_" + m.name + " = " + conv(e, m.t, m.line) + ";\n";
			}
		}
		for (const Fn &f : fns) {
			std::string sig = std::string(cty(f.ret)) + " C::f_" + f.name + "(";
			std::string d = std::string("\t") + cty(f.ret) + " f_" + f.name + "(";
			std::string call = "((C *)s)->f_" + f.name + "(";
			int i = 0;
			for (const Param &a : f.params) {
				std::string one = std::string(i ? ", " : "") + cty(a.t) + " l_" + a.name;
				sig += one;
				d += one;
				call += std::string(i ? ", " : "") + (a.t == Ty::Var ? "*a[" + std::to_string(i) + "]" : "a[" + std::to_string(i) + "]->operator " + cty(a.t) + "()");
				i++;
			}
			decl += d + ");\n";
			defs += sig + ") {\n" + emit_fn(f) + "}\n\n";
			std::string body = f.ret == Ty::Void ? call + "); r = Variant();" : "r = Variant(" + call + "));";
			mtab += "\t{ \"" + f.name + "\", " + std::to_string(i) + ", [](void *s, const Variant **a, Variant &r) { " + body + " } },\n";
		}

		std::string call_init = find_fn("_init") ? "\tc->f__init();\n" : "";
		std::string gen = "// Generated by GodotNative from " + path + ". Do not edit.\n";
		gen += "#define GN_FILE \"" + path + "\"\n#include <gn_runtime.h>\n#include <godot_cpp/classes/" + class_header(base) + ".hpp>\n#include <godot_cpp/variant/builtin_types.hpp>\n#include <cstdlib>\n\nusing namespace godot;\n\nnamespace {\n\n";
		gen += const_block + "\nstruct C {\n\tObject *owner;\n" + decl + "\tvoid init();\n};\n\n";
		gen += "void C::init() {\n" + members_init + "}\n\n" + defs;
		gen += "const gn::Method methods[] = {\n" + mtab + "\t{ nullptr, 0, nullptr },\n};\n";
		gen += "const gn::Prop props[] = {\n" + ptab + "\t{ nullptr, GDEXTENSION_VARIANT_TYPE_NIL, nullptr, nullptr },\n};\n\n";
		gen += "void *create(Object *owner) {\n\tC *c = new C();\n\tc->owner = owner;\n\tc->init();\n" + call_init + "\treturn c;\n}\n\n";
		gen += "void destroy(void *self) { delete (C *)self; }\n\n";
		gen += "const gn::Class klass = { \"" + path + "\", \"" + base + "\", \"" + global + "\", create, destroy, methods, " + std::to_string(fns.size()) + ", props, " + std::to_string(members.size()) + " };\n";
		gen += "gn::Registrar reg(&klass);\n\n} // namespace\n";
		return gen;
	}
};

} // namespace

bool is_toggled(const std::string &source) {
	std::string l = first_line(source);
	return l == "#pragma native" || l == "#pragma native unsafe";
}

Result transpile(const std::string &source, const std::string &path, const ClassQuery &classes) {
	Result r;
	try {
		Compiler c(tokenize(source), path, classes, first_line(source) == "#pragma native unsafe");
		r.cpp = c.run();
		r.warnings = c.warning_list();
		r.ok = true;
	} catch (const Err &e) {
		r.error = path + ":" + std::to_string(e.line) + ": " + e.msg;
	}
	return r;
}

} // namespace gn
