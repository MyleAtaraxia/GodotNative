extends "res://compiler/probe.gd"

const OUTPUT := "res://build/editor_probe.json"
const SOURCE := """#pragma native
extends RefCounted


func work(count: int) -> int:
	var total: int = 0
	# plain comment
	#pragma omp parallel for reduction(+:total) schedule(dynamic) # note
	for i in range(count):
		total += i
	return total
"""

var result := {}


func probe_editor() -> Dictionary:
	if not result.is_empty():
		return result
	DirAccess.make_dir_recursive_absolute("res://build")
	DirAccess.remove_absolute(OUTPUT)
	write_file("res://probe/editing.gd", SOURCE)
	FileAccess.open("res://project.godot", FileAccess.WRITE).store_string(project_text + "\n[editor_plugins]\n\nenabled=PackedStringArray(\"res://addons/editor_probe/plugin.cfg\")\n")
	var args := ["--headless", "--editor", "--path", ProjectSettings.globalize_path("res://"), "--quit-after", "6000"]
	var exit_code := OS.execute(OS.get_executable_path(), args, [], true)
	result = JSON.parse_string(FileAccess.get_file_as_string(OUTPUT))
	result["exit_code"] = exit_code
	return result


func test_pragma_lines_are_coloured_and_other_lines_keep_gdscript_colours() -> void:
	var editor := probe_editor()
	assert_eq(editor["exit_code"], 0)
	assert_eq(editor["highlighter"], "PragmaHighlighter")
	var colors: Dictionary = editor["colors"]
	assert_eq(colors["pragma"], colors["keyword"])
	assert_eq(colors["directive"], colors["keyword"])
	assert_eq(colors["clause"], colors["clause_color"])
	assert_eq(colors["second_clause"], colors["clause_color"])
	assert_eq(colors["inside"], colors["text"])
	assert_eq(colors["trailing_comment"], colors["comment"])
	assert_eq(colors["plain_comment"], colors["comment"])
	assert_eq(colors["code_keyword"], colors["keyword"])
	assert_ne(colors["code_name"], colors["keyword"])


func test_suggestions_follow_the_cursor() -> void:
	var seen: Dictionary = probe_editor()["suggestions"]
	assert_eq(seen["#pragma"], ["end", "omp"])
	assert_eq(seen["#pragma end"], ["critical", "parallel", "single"])
	for directive: String in ["atomic", "barrier", "critical", "for", "parallel", "parallel for", "simd", "single"]:
		assert_has(seen["#pragma omp"], directive)
	assert_does_not_have(seen["#pragma omp"], "end parallel")
	assert_eq(seen["#pragma omp par"], ["parallel", "parallel for"])
	assert_has(seen["#pragma omp parallel"], "for")
	assert_has(seen["#pragma omp parallel"], "num_threads(")
	assert_has(seen["#pragma omp parallel for"], "schedule(")
	assert_has(seen["#pragma omp for"], "nowait")
	assert_eq(seen["#pragma omp critical"], [])
	assert_eq(seen["#pragma omp parallel for schedule("], ["dynamic", "guided", "static"])
	assert_eq(seen["#pragma omp parallel for reduction("], ["*:", "+:", "max:", "min:"])
	assert_eq(seen["#pragma omp parallel for reduction(+:to"], ["total"])
	assert_has(seen["#pragma omp parallel for private("], "total")
	assert_does_not_have(seen["var plain :="], "schedule(")


func test_errors_mark_the_line_and_clear_when_fixed() -> void:
	var errors: Dictionary = probe_editor()["errors"]
	assert_eq(errors["pragma"]["line"], 7.0)
	assert_string_contains(errors["pragma"]["message"], "shedule")
	assert_eq(errors["code"]["line"], 10.0)
	assert_string_contains(errors["code"]["message"], "must return a value")
	assert_eq(errors["fixed"]["line"], -1.0)
	assert_eq(errors["plain_script"]["line"], -1.0)
