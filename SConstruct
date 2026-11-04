#!/usr/bin/env python
import os

SetOption("implicit_cache", 1)

ARGUMENTS.setdefault("api_version", "4.4")
ARGUMENTS.setdefault("use_mingw", "yes")
ARGUMENTS.setdefault("use_llvm", "yes")
project = ARGUMENTS.get("project", "demo")
env = SConscript("godot-cpp/SConstruct")
env.Decider("MD5-timestamp")

ours = env.Clone()
ours.Append(CPPPATH=["src/"])
# --gc-sections on MinGW drops the static initializers that register godot-cpp engine classes.
ours["LINKFLAGS"] = [f for f in ours["LINKFLAGS"] if f != "-Wl,--gc-sections"]
ours["SHOBJSUFFIX"] = env["suffix"] + env["SHOBJSUFFIX"]
ours["SHLINK"] = env["LINK"]
ours.Append(CCFLAGS=["-fexceptions"])
ours.Append(CXXFLAGS=["-std=c++20"])

sources = Glob("src/*.cpp")
if env["target"] == "editor":
    sources += Glob("src/editor/*.cpp")
else:
    generated = Glob(project + "/addons/godot_native/generated/*.cpp")
    sources += generated
    # shortcut: OpenMP is only enabled on Windows and Linux, other platforms run parallel loops serially until their toolchains are set up
    if env["platform"] in ("windows", "linux") and any("#pragma omp" in open(str(f), encoding="utf-8").read() for f in generated):
        ours.Append(CCFLAGS=["-fopenmp"])
        if env["platform"] == "windows":
            toolchain = os.path.dirname(env.WhereIs(env["CXX"], os.environ["PATH"]))
            ours.Append(LINKFLAGS=["-Wl,-Bdynamic", "-lomp", "-Wl,-Bstatic"])
            ours.Default(ours.Install(project + "/addons/godot_native/bin", toolchain + "/libomp.dll"))
        else:
            ours.Append(LINKFLAGS=["-fopenmp"])

ours.Default(ours.SharedLibrary(
    "{}/addons/godot_native/bin/godot_native{}{}".format(project, env["suffix"], env["SHLIBSUFFIX"]),
    source=sources,
))
