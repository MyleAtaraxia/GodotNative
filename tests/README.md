# Tests

Tests use [GUT](https://github.com/bitwes/Gut). Install the release that matches
your Godot version into `tests/addons/gut`: 9.7.1 for 4.7, 9.4.0 for 4.4.

From the repository root, with SCons and a C++ compiler installed:

    scons -j8 target=editor project=tests
    godot --headless --path tests --import
    mkdir -p tests/build
    godot --headless --path tests --export-pack test build/test.pck
    scons -j8 target=template_debug project=tests api_version=4.7

Set `api_version` to your Godot version. The test project needs that debug build
because GUT runs in a debug process, while `--export-pack` builds release.

Then run the suites:

    godot --headless --path tests -s addons/gut/gut_cmdln.gd -gconfig=res://.gutconfig.json
    godot --headless --path tests -s addons/gut/gut_cmdln.gd -gconfig=res://.gutconfig.json -gdir=res://compiler

`unit/` checks what `fixtures/` compute. Creating `tests/override.cfg` with
`build/native_in_debug_runs=false` under `[godot_native]` runs the same tests
interpreted, so native and GDScript must agree.

Unit tests load fixtures with `load()` and never name their classes. Naming one
makes Godot compile a GDScript copy, and the test would no longer run native.

`compiler/` runs a real export per case and needs SCons on the path.

`fallback/` checks API newer than the compiled one. It needs Godot 4.7 with
`GODOT_NATIVE_API=4.4` set, which compiles against the older API on purpose:

    GODOT_NATIVE_API=4.4 godot --headless --path tests -s addons/gut/gut_cmdln.gd -gconfig=res://.gutconfig.json -gdir=res://fallback
