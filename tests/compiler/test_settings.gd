extends "res://compiler/probe.gd"

const UNSUPPORTED := "#pragma native\n@rpc func f() -> void:\n\tpass\n"
const UNSUPPORTED_CHILD := "extends Calc\n@rpc func f() -> void:\n\tpass\n"
const UNTYPED := "#pragma native\nfunc probe_untyped():\n\tpass\n"


func test_project_error_falls_back() -> void:
	assert_string_contains(export_with(UNSUPPORTED, "errors/project_scripts=1"), "running interpreted")


func test_addon_error_falls_back() -> void:
	assert_string_contains(export_with(UNSUPPORTED, "errors/addon_scripts=1", ADDON_PROBE), "running interpreted")


func test_fallback_cascades_to_children() -> void:
	write_file("res://probe/parent.gd", "@rpc func g() -> void:\n\tpass\n")
	var output := export_with("#pragma native\nextends \"res://probe/parent.gd\"\n", "errors/project_scripts=1")
	assert_string_contains(output, "must too")


func test_skip_addons() -> void:
	assert_false(export_with(UNSUPPORTED_CHILD, "errors/addon_scripts=2", ADDON_PROBE).contains("child of native"))


func test_children_fail() -> void:
	assert_string_contains(export_with("extends Calc\n", "inheritance/children_of_native=1"), "add #pragma native")


func test_children_left_interpreted() -> void:
	assert_string_contains(export_with(UNSUPPORTED_CHILD, "inheritance/children_of_native=2"), "left interpreted")


func test_untyped_error() -> void:
	assert_string_contains(export_with(UNTYPED, "warnings/untyped_code=2"), "[GodotNative] res://probe/probe.gd:2:")


func test_untyped_ignored() -> void:
	assert_false(export_with(UNTYPED, "warnings/untyped_code=0").contains("probe_untyped"))
