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

Native code only exists in exports. Running the project in the editor is
interpreted in every mode, and the on-screen label says `GDScript` when it is.
