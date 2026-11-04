extends GutTest

const FIXTURES := [
	"res://fixtures/calc.gd",
	"res://fixtures/collections.gd",
	"res://fixtures/thing.gd",
	"res://fixtures/shape.gd",
	"res://fixtures/square.gd",
	"res://fixtures/fast.gd",
]


func expected_class() -> String:
	return "NativeScript" if ProjectSettings.get_setting("godot_native/build/native_in_debug_runs", true) else "GDScript"


func test_fixture_runs_on_expected_backend(path: String = use_parameters(FIXTURES)) -> void:
	assert_eq(load(path).get_class(), expected_class())
