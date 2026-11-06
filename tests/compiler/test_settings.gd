extends "res://compiler/probe.gd"

const UNSUPPORTED := "#pragma native\n@rpc func f() -> void:\n\tpass\n"
const UNSUPPORTED_CHILD := "extends Calc\n@rpc func f() -> void:\n\tpass\n"
const UNTYPED := "#pragma native\nfunc probe_untyped():\n\tpass\n"
const WIDENED := "#pragma native\nclass Base:\n\tfunc kind():\n\t\treturn 1\nclass Child extends Base:\n\tfunc kind() -> int:\n\t\treturn 2\n"


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


func test_typed_override_of_untyped_parent() -> void:
	assert_false(export_with(WIDENED).contains("must keep the same"))


func test_untyped_ignored() -> void:
	assert_false(export_with(UNTYPED, "warnings/untyped_code=0").contains("probe_untyped"))


func generated_header() -> String:
	return FileAccess.get_file_as_string("res://addons/godot_native/generated/probe_probe_gd.gen.h")


func test_inferred_returns_and_members() -> void:
	export_with("#pragma native\nvar speed = 1.5\nfunc answer():\n\treturn 42\nfunc mixed(flag: bool):\n\tif flag:\n\t\treturn 1\n\treturn 'x'\n")
	var header := generated_header()
	assert_string_contains(header, "int64_t f_answer")
	assert_string_contains(header, "double m_speed")
	assert_string_contains(header, "Variant f_mixed")


func test_member_written_elsewhere_stays_dynamic() -> void:
	write_file("res://probe/writer.gd", "extends RefCounted\nfunc write(other: Variant) -> void:\n\tother.speed = 'fast'\n")
	export_with("#pragma native\nvar speed = 1.5\n")
	assert_string_contains(generated_header(), "Variant m_speed")


func test_parameters_get_a_typed_copy() -> void:
	export_with("#pragma native\nfunc add(a, b):\n\treturn a + b\nfunc total():\n\treturn add(1, 2.5)\n")
	var header := generated_header()
	assert_string_contains(header, "double f_add__typed(int64_t l_a, double l_b)")
	assert_string_contains(header, "Variant f_add(Variant l_a, Variant l_b)")
	assert_string_contains(header, "double f_total")


func test_parameter_guesses_from_defaults_and_the_engine() -> void:
	export_with("#pragma native\nextends Node\nvar seen = 0.0\nfunc _process(delta):\n\tseen = seen + delta\nfunc scaled(by = 2.0):\n\treturn by * 2.0\n")
	var header := generated_header()
	assert_string_contains(header, "f__process__typed(double l_delta)")
	assert_string_contains(header, "double f_scaled__typed(double l_by)")


func test_parameter_guesses_from_interpreted_callers() -> void:
	write_file("res://probe/caller.gd", "extends RefCounted\nfunc call_it(other: Variant) -> void:\n\tother.speed_up(-1.5, 'x', true)\n")
	export_with("#pragma native\nfunc speed_up(by, name, flag):\n\treturn by\n")
	assert_string_contains(generated_header(), "double f_speed_up__typed(double l_by, String l_name, bool l_flag)")


func test_rewritten_parameter_stays_dynamic() -> void:
	export_with("#pragma native\nfunc rewrite(a):\n\ta = 'x'\n\treturn a\nfunc total():\n\treturn rewrite(1)\n")
	assert_false(generated_header().contains("__typed"))
