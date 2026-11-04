extends "res://compiler/probe.gd"

const CASES := [
	["method on self", "#pragma native\nextends Control\nfunc f() -> Vector2:\n\treturn get_maximum_size()\n", "dynamic call"],
	["extending a new class", "#pragma native\nextends AimModifier3D\n", "dynamic calls"],
	["inherited call on a new class", "#pragma native\nextends AimModifier3D\nfunc f() -> Node:\n\treturn get_parent()\n", "dynamic"],
	["new static method", "#pragma native\nfunc f() -> int:\n\treturn FileAccess.get_access_time(\"res://project.godot\")\n", "dynamic call"],
	["new global enum value", "#pragma native\nfunc f() -> int:\n\treturn JOY_BUTTON_MISC2\n", "file(s) for"],
]


func test_compiles_through_dynamic_calls(case: Array = use_parameters(CASES)) -> void:
	if OS.get_environment("GODOT_NATIVE_API") != "4.4" or Engine.get_version_info().minor < 7:
		pending("Needs Godot 4.7 with GODOT_NATIVE_API=4.4")
		return
	var output := export_with(case[1])
	assert_string_contains(output, "file(s) for", case[0])
	assert_string_contains(output, case[2], case[0])
