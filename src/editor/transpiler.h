#pragma once

#include <map>
#include <string>
#include <vector>

namespace gn {

enum class Ty { Void, Bool, Int, Float, Str, V2, V3, PF64, PI64, Arr, Dict, Var };

struct NativeMember {
	bool found = false;
	Ty type = Ty::Var;
	bool direct = false;
	std::vector<Ty> args;
	int defaults = 0;
};

class ClassQuery {
public:
	virtual ~ClassQuery() {}
	virtual bool class_exists(const std::string &name) const = 0;
	virtual NativeMember method(const std::string &cls, const std::string &name) const = 0;
	virtual NativeMember property(const std::string &cls, const std::string &name) const = 0;
};

struct Result {
	bool ok = false;
	std::string cpp;
	std::string error;
	std::vector<std::string> warnings;
};

bool is_toggled(const std::string &source);
Result transpile(const std::string &source, const std::string &path, const ClassQuery &classes);

} // namespace gn
