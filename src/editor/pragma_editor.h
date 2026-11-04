#pragma once

#include "transpiler.h"

gn::Output gn_check_source(const std::string &path, const std::string &source);
void gn_register_pragma_editor();
void gn_pragma_editor_start();
void gn_pragma_editor_stop();
