#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace gn {

enum class Ty { Void, Bool, Int, Float, Str, V2, V3, PF64, PI64, Arr, Dict, Var };

struct NativeMember {
	bool found = false;
	Ty type = Ty::Var;
	bool direct = false;
	bool is_static = false;
	std::vector<Ty> args;
	int defaults = 0;
};

struct NativeBind {
	bool found = false;
	bool ptr = false;
	std::string owner;
	uint32_t hash = 0;
	std::vector<std::string> args;
	std::vector<std::string> defaults;
	std::string ret;
};

class ClassQuery {
public:
	virtual ~ClassQuery() {}
	virtual bool class_exists(const std::string &name) const = 0;
	virtual bool inherits(const std::string &cls, const std::string &base) const = 0;
	virtual NativeMember method(const std::string &cls, const std::string &name) const = 0;
	virtual NativeMember property(const std::string &cls, const std::string &name) const = 0;
	virtual bool int_constant(const std::string &cls, const std::string &name, int64_t &value) const = 0;
	virtual bool is_singleton(const std::string &name) const = 0;
	virtual NativeBind bind(const std::string &cls, const std::string &name) const = 0;
	virtual bool accessors(const std::string &cls, const std::string &name, std::string &getter, std::string &setter) const = 0;
	virtual bool utility(const std::string &name, std::string &ret, bool &vararg) const = 0;
	virtual bool has_signal(const std::string &cls, const std::string &name) const = 0;
	virtual bool global_constant(const std::string &name, int64_t &value) const = 0;
	virtual bool has_builtin_constant(const std::string &type, const std::string &name) const = 0;
};

class ProjectQuery : public ClassQuery {
public:
	virtual bool read(const std::string &path, std::string &source) const = 0;
	virtual std::string absolute(const std::string &from, const std::string &path) const = 0;
	virtual std::string global_class_path(const std::string &name) const = 0;
};

struct Output {
	bool ok = false;
	std::vector<std::pair<std::string, std::string>> files;
	std::vector<std::string> implied;
	std::vector<std::string> warnings;
	std::string error;
};

bool is_toggled(const std::string &source);
Output transpile_project(const std::vector<std::string> &toggled, const std::vector<std::string> &all, const ProjectQuery &query);

} // namespace gn
