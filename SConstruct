#!/usr/bin/env python
import glob
import os
import subprocess

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
    # The plugin recreates these in projects that have no build folder.
    embedded = ["SConstruct"] + sorted(glob.glob("src/*.cpp") + glob.glob("src/*.h"))
    with open("src/editor/embedded_sources.gen.h", "w", encoding="utf-8", newline="\n") as header:
        commit = subprocess.run(["git", "rev-parse", "HEAD:godot-cpp"], capture_output=True, text=True).stdout.strip()
        header.write('const char *EMBEDDED_GODOT_CPP_COMMIT = "%s";\n' % commit)
        header.write("const struct { const char *path; const char *text; } EMBEDDED_SOURCES[] = {\n")
        for path in embedded:
            with open(path, encoding="utf-8", newline="") as f:
                header.write('{"%s", R"GN_%s(%s)GN_%s"},\n' % (path.replace(os.sep, "/"), "EMBED", f.read(), "EMBED"))
        header.write("};\n")
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
