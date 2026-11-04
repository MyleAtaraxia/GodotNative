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
