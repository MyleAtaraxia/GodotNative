#!/usr/bin/env python
import os

os.environ.setdefault("SCONS_CACHE_MSVC_CONFIG", "1")
SetOption("implicit_cache", 1)

ARGUMENTS.setdefault("api_version", "4.4")
project = ARGUMENTS.get("project", "demo")
env = SConscript("godot-cpp/SConstruct")
env.Decider("MD5-timestamp")

ours = env.Clone()
ours.Append(CPPPATH=["src/"])
ours.Append(CCFLAGS=["/EHsc"] if env.get("is_msvc") else ["-fexceptions"])
ours.Append(CXXFLAGS=["/std:c++20"] if env.get("is_msvc") else ["-std=c++20"])

sources = Glob("src/*.cpp")
if env["target"] == "editor":
    sources += Glob("src/editor/*.cpp")
else:
    sources += Glob(project + "/addons/godot_native/generated/*.cpp")

ours.Default(ours.SharedLibrary(
    "{}/addons/godot_native/bin/godot_native{}{}".format(project, env["suffix"], env["SHLIBSUFFIX"]),
    source=sources,
))
