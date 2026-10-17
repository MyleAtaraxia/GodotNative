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
Output transpile_project(const std::vector<std::string> &toggled, const ProjectQuery &query);

} // namespace gn
