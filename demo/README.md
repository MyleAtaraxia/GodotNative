# Boids demo

Naive O(n²) flocking, so the work is almost all GDScript. `boids_native.gd` and
`boids_native_unsafe.gd` are copies of `boids_interpreted.gd`, differing only in
the `#pragma` on the first line.

From the repository root, with SCons and a C++ compiler installed:

    mkdir -p api
    (cd api && godot --headless --dump-extension-api)
    scons -j8 target=editor project=demo

Then open `demo/` in Godot and press Play.

- Space: switch between interpreted, native and native unsafe
- Up/Down: add or remove 500 boids

Pressing Play in the editor runs native too: the addon compiles toggled files
before the game starts, showing a progress window, and a compile error cancels
the run. Debug exports work
the same way. The editor itself stays interpreted, including `@tool` scripts.
Breakpoints and live script reload don't work in native files, so remove the
`#pragma` from a file you want to step through.
