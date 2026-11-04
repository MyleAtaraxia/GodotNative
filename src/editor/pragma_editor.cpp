#include "pragma_editor.h"

#include <godot_cpp/classes/code_edit.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/editor_settings.hpp>
#include <godot_cpp/classes/editor_syntax_highlighter.hpp>
#include <godot_cpp/classes/gd_script.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/script_editor.hpp>
#include <godot_cpp/classes/script_editor_base.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <algorithm>
#include <cctype>
#include <set>

using namespace godot;

namespace {

const char *OVERLAY_META = "godot_native_overlay";

std::string utf8(const String &s) {
	CharString chars = s.utf8();
	return std::string(chars.get_data(), chars.length());
}

bool word_char(char c) {
	return isalnum((unsigned char)c) || c == '_';
}

std::string left_trim(const std::string &s) {
	size_t at = s.find_first_not_of(" \t");
	return at == std::string::npos ? "" : s.substr(at);
}

std::string directive_at(const std::string &after) {
	std::string found;
	for (const auto &d : gn::omp_directives()) {
		const std::string &kind = d.first;
		if (kind.size() > found.size() && after.compare(0, kind.size(), kind) == 0 && (after.size() == kind.size() || !word_char(after[kind.size()]))) {
			found = kind;
		}
	}
	return found;
}

} // namespace

namespace godot {

class PragmaHighlighter : public EditorSyntaxHighlighter {
	GDCLASS(PragmaHighlighter, EditorSyntaxHighlighter)

	Ref<SyntaxHighlighter> inner;
	Color keyword_color;
	Color clause_color;
	Color text_color;

	Variant color(const Color &value) const {
		Dictionary entry;
		entry["color"] = value;
		return entry;
	}

protected:
	static void _bind_methods() {}

public:
	void wrap(const Ref<SyntaxHighlighter> &p_inner) { inner = p_inner; }

	String _get_name() const override { return "GDScript"; }

	void _update_cache() override {
		inner->update_cache();
		Ref<EditorSettings> settings = EditorInterface::get_singleton()->get_editor_settings();
		keyword_color = settings->get_setting("text_editor/theme/highlighting/keyword_color");
		clause_color = settings->get_setting("text_editor/theme/highlighting/function_color");
		text_color = settings->get_setting("text_editor/theme/highlighting/text_color");
	}

	void _clear_highlighting_cache() override {
		inner->clear_highlighting_cache();
	}

	Dictionary _get_line_syntax_highlighting(int32_t p_line) const override {
		Dictionary base = inner->get_line_syntax_highlighting(p_line);
		std::string line = utf8(get_text_edit()->get_line(p_line));
		size_t at = line.find_first_not_of(" \t");
		if (at == std::string::npos || (line.compare(at, 11, "#pragma omp") != 0 && line.compare(at, 12, "#pragma end ") != 0)) {
			return base;
		}
		Dictionary map;
		Array keys = base.keys();
		for (int i = 0; i < keys.size(); i++) {
			if (int(keys[i]) < int(at)) {
				map[keys[i]] = base[keys[i]];
			}
		}
		Variant comment = base.get(int(at), Variant());
		size_t end = std::min(line.find('#', at + 1), line.size());
		size_t words = at + 11;
		map[int(at)] = color(keyword_color);
		std::string after = left_trim(line.substr(words, end - words));
		size_t kind_start = end - std::min(end - words, after.size());
		std::string kind = directive_at(after.compare(0, 4, "end ") == 0 ? after.substr(4) : after);
		size_t i = kind_start + (after.compare(0, 4, "end ") == 0 ? 4 : 0) + kind.size();
		int depth = 0;
		while (i < end) {
			if (word_char(line[i]) && depth == 0) {
				map[int(i)] = color(clause_color);
				while (i < end && word_char(line[i])) {
					i++;
				}
				continue;
			}
			if (line[i] == '(') {
				map[int(i)] = color(text_color);
				depth++;
			} else if (line[i] == ')' && --depth == 0 && i + 1 < end) {
				map[int(i + 1)] = color(text_color);
			}
			i++;
		}
		if (end < line.size() && comment.get_type() != Variant::NIL) {
			map[int(end)] = comment;
		}
		return map;
	}
};

class PragmaOverlay : public Control {
	GDCLASS(PragmaOverlay, Control)

	CodeEdit *code = nullptr;
	String path;

protected:
	static void _bind_methods() {}

public:
	void wrap_highlighter() {
		Ref<SyntaxHighlighter> current = code->get_syntax_highlighter();
		if (current.is_null() || !current->is_class("GDScriptSyntaxHighlighter")) {
			return;
		}
		Ref<PragmaHighlighter> wrapper;
		wrapper.instantiate();
		wrapper->wrap(current);
		code->set_syntax_highlighter(wrapper);
	}

	void setup(CodeEdit *p_code, const String &p_path) {
		code = p_code;
		path = p_path;
		code->add_child(this, false, INTERNAL_MODE_BACK);
		code->set_meta(OVERLAY_META, this);
		set_anchors_and_offsets_preset(PRESET_FULL_RECT);
		set_mouse_filter(MOUSE_FILTER_IGNORE);
		wrap_highlighter();
	}
};

class PragmaEditors : public RefCounted {
	GDCLASS(PragmaEditors, RefCounted)

protected:
	static void _bind_methods() {}

public:
	void on_script_changed(const Ref<Script> &script) {
		ScriptEditorBase *editor = EditorInterface::get_singleton()->get_script_editor()->get_current_editor();
		CodeEdit *code = editor ? Object::cast_to<CodeEdit>(editor->get_base_editor()) : nullptr;
		if (!code || !Object::cast_to<GDScript>(script.ptr()) || script->get_path().contains("::")) {
			return;
		}
		if (code->has_meta(OVERLAY_META)) {
			Object::cast_to<PragmaOverlay>(code->get_meta(OVERLAY_META))->wrap_highlighter();
			return;
		}
		memnew(PragmaOverlay)->setup(code, script->get_path());
	}
};

} // namespace godot

static Ref<PragmaEditors> editors;

void gn_register_pragma_editor() {
	GDREGISTER_INTERNAL_CLASS(PragmaHighlighter);
	GDREGISTER_INTERNAL_CLASS(PragmaOverlay);
	GDREGISTER_INTERNAL_CLASS(PragmaEditors);
}

void gn_pragma_editor_start() {
	editors.instantiate();
	EditorInterface::get_singleton()->get_script_editor()->connect("editor_script_changed", callable_mp(editors.ptr(), &PragmaEditors::on_script_changed));
}

void gn_pragma_editor_stop() {
	EditorInterface::get_singleton()->get_script_editor()->disconnect("editor_script_changed", callable_mp(editors.ptr(), &PragmaEditors::on_script_changed));
	editors.unref();
}
