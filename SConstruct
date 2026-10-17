#!/usr/bin/env python
import os

ARGUMENTS.setdefault("custom_api_file", os.path.abspath(ARGUMENTS.get("api", "api/extension_api.json")))
project = ARGUMENTS.get("project", "demo")
env = SConscript("godot-cpp/SConstruct")

ours = env.Clone()
ours.Append(CPPPATH=["src/"])
ours.Append(CCFLAGS=["/EHsc"] if env.get("is_msvc") else ["-fexceptions"])

sources = Glob("src/*.cpp")
if env["target"] == "editor":
    sources += Glob("src/editor/*.cpp")
else:
    sources += Glob(project + "/addons/godot_native/generated/*.cpp")

ours.Default(ours.SharedLibrary(
    "{}/addons/godot_native/bin/godot_native{}{}".format(project, env["suffix"], env["SHLIBSUFFIX"]),
    source=sources,
))
