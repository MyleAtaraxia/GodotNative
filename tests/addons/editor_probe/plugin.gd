@tool
extends EditorPlugin

const SOURCE := "res://probe/editing.gd"
const OUTPUT := "res://build/editor_probe.json"
const CODE_LINE := 5
const COMMENT_LINE := 6
const PRAGMA_LINE := 7
const SETTINGS := "text_editor/theme/highlighting/"

var code: CodeEdit


func _ready() -> void:
	run.call_deferred()


func wait(seconds: float) -> void:
	await get_tree().create_timer(seconds).timeout


func color_at(line: int, column: int) -> String:
	var entries: Dictionary = code.syntax_highlighter.get_line_syntax_highlighting(line)
	var found := -1
	for key: int in entries:
		if key <= column and key > found:
			found = key
	return "" if found < 0 else entries[found]["color"].to_html()


func setting_color(setting: String) -> String:
	return EditorInterface.get_editor_settings().get_setting(SETTINGS + setting).to_html()


func highlighting() -> Dictionary:
	var line := code.get_line(PRAGMA_LINE)
	return {
		"pragma": color_at(PRAGMA_LINE, line.find("#pragma")),
		"directive": color_at(PRAGMA_LINE, line.find("parallel")),
		"clause": color_at(PRAGMA_LINE, line.find("reduction")),
		"second_clause": color_at(PRAGMA_LINE, line.find("schedule")),
		"inside": color_at(PRAGMA_LINE, line.find("total")),
		"trailing_comment": color_at(PRAGMA_LINE, line.find("# note")),
		"plain_comment": color_at(COMMENT_LINE, 2),
		"code_keyword": color_at(CODE_LINE, 1),
		"code_name": color_at(CODE_LINE, 5),
		"keyword": setting_color("keyword_color"),
		"clause_color": setting_color("function_color"),
		"text": setting_color("text_color"),
		"comment": setting_color("comment_color"),
	}


func colors_with_edits() -> Dictionary:
	var pragma := code.get_line(PRAGMA_LINE)
	code.set_line(PRAGMA_LINE, "\t#pragma parallel for reduction(+:total)")
	var seen := {
		"short_directive": color_at(PRAGMA_LINE, 10),
		"short_clause": color_at(PRAGMA_LINE, 22),
	}
	code.set_line(PRAGMA_LINE, "\t#pragma end parallel")
	seen["short_end"] = color_at(PRAGMA_LINE, 10)
	code.set_line(PRAGMA_LINE, "\t#pragma hello there")
	seen["not_a_directive"] = color_at(PRAGMA_LINE, 10)
	code.set_line(PRAGMA_LINE, "\t#pragma native")
	seen["native_too_late"] = color_at(PRAGMA_LINE, 10)
	code.set_line(PRAGMA_LINE, pragma)
	return seen


func suggest(text: String) -> Array:
	code.set_line(PRAGMA_LINE, text)
	code.set_caret_line(PRAGMA_LINE)
	code.set_caret_column(text.length())
	code.request_code_completion(true)
	await wait(0.1)
	var names := []
	for option: Dictionary in code.get_code_completion_options():
		names.append(option["display_text"])
	code.cancel_code_completion()
	return names


func style_of(text: String, display: String) -> Dictionary:
	code.set_line(PRAGMA_LINE, text)
	code.set_caret_line(PRAGMA_LINE)
	code.set_caret_column(text.length())
	code.request_code_completion(true)
	await wait(0.1)
	var style := {}
	for option: Dictionary in code.get_code_completion_options():
		if option["display_text"] == display:
			style = {"kind": option["kind"], "icon": option["icon"] != null, "color": option["font_color"].to_html()}
	code.cancel_code_completion()
	return style


func styles() -> Dictionary:
	return {
		"keyword": await style_of("\t#pragma omp ", "parallel"),
		"clause": await style_of("\t#pragma omp parallel for ", "schedule(…)"),
		"constant": await style_of("\t#pragma omp parallel for schedule(", "dynamic"),
		"operator": await style_of("\t#pragma omp parallel for reduction(", "min"),
		"local": await style_of("\t#pragma omp parallel for private(", "total"),
		"expected_color": setting_color("completion_font_color"),
		"function_kind": CodeEdit.KIND_FUNCTION,
		"variable_kind": CodeEdit.KIND_VARIABLE,
		"constant_kind": CodeEdit.KIND_CONSTANT,
	}


func suggestions() -> Dictionary:
	var result := {}
	for text: String in ["\t#pragma ", "\t#pragma end ", "\t#pragma par", "\t#pragma parallel for ", "\t#pragma hello ", "\t#pragma omp ", "\t#pragma omp par", "\t#pragma omp parallel ", "\t#pragma omp parallel for ", "\t#pragma omp for ", "\t#pragma omp critical ", "\t#pragma omp parallel for schedule(", "\t#pragma omp parallel for reduction(", "\t#pragma omp parallel for reduction(+:to", "\t#pragma omp parallel for private(", "\tvar plain := "]:
		result[text.strip_edges()] = await suggest(text)
	return result


func error_after(text: String) -> Dictionary:
	code.text = text
	await wait(0.7)
	var overlay: Object = code.get_meta("godot_native_overlay")
	return {"line": overlay.get_error_line(), "message": overlay.get_error_message()}


func errors(good: String) -> Dictionary:
	var bad_clause := good.replace("schedule(dynamic)", "shedule(dynamic)")
	return {
		"pragma": await error_after(bad_clause),
		"code": await error_after(good.replace("return total", "return")),
		"fixed": await error_after(good),
		"plain_script": await error_after(bad_clause.replace("#pragma native\n", "")),
	}


func run() -> void:
	await wait(1.0)
	EditorInterface.edit_script(load(SOURCE))
	await wait(0.5)
	code = EditorInterface.get_script_editor().get_current_editor().get_base_editor()
	var good := code.text
	var result := {"highlighter": code.syntax_highlighter.get_class(), "colors": highlighting()}
	result["edited_colors"] = colors_with_edits()
	result["suggestions"] = await suggestions()
	result["styles"] = await styles()
	code.text = good
	result["errors"] = await errors(good)
	FileAccess.open(OUTPUT, FileAccess.WRITE).store_string(JSON.stringify(result))
	get_tree().quit()
