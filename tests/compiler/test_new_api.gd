extends "res://compiler/probe.gd"

const CASES := [
	["method on self", "#pragma native\nextends Control\nfunc f() -> Vector2:\n\treturn get_maximum_size()\n"],
	["extending a new class", "#pragma native\nextends AimModifier3D\n"],
	["new static method", "#pragma native\nfunc f() -> int:\n\treturn FileAccess.get_access_time(\"res://project.godot\")\n"],
	["new global enum value", "#pragma native\nfunc f() -> int:\n\treturn JOY_BUTTON_MISC2\n"],
]


func test_compiles_on_the_running_engine(case: Array = use_parameters(CASES)) -> void:
	if Engine.get_version_info().minor < 7:
		pending("This API needs Godot 4.7")
		return
	assert_string_contains(export_with(case[1]), "file(s) for", case[0])
