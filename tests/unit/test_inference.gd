extends GutTest


var source: Node


func before_each() -> void:
	source = load("res://fixtures/inference.gd").new()


func after_each() -> void:
	source.free()


func test_returns() -> void:
	assert_eq(source.answer(), 42)
	assert_eq(source.half(), 21.0)
	assert_eq(source.doubled(4), 8)
	assert_eq(source.countdown(3), 3)
	assert_eq(source.with_lambda(), 5)


func test_returns_of_different_types_stay_dynamic() -> void:
	assert_eq(source.mixed(true), 1)
	assert_eq(source.mixed(false), "text")
	assert_eq(source.int_or_float(true), 1)
	assert_eq(source.int_or_float(false), 1.5)
	assert_eq(source.areas(), [1, "big"])


func test_paths_without_a_value_return_null() -> void:
	assert_eq(source.maybe(true), 1)
	assert_eq(source.maybe(false), null)
	assert_eq(source.bare(true), null)
	assert_eq(source.bare(false), 1)


func test_members() -> void:
	assert_eq(source.bump(), [1, 1.0])
	assert_eq(source.bump(), [2, 2.0])
	assert_eq(source.scaled, 8)
	assert_eq(source.add_word("a"), 1)
	assert_eq(source.words, ["a"])


func test_members_written_with_another_type() -> void:
	assert_eq(source.rewrite(), "text")
	source.from_outside = "outside"
	assert_eq(source.from_outside, "outside")
	source.set("by_name", "named")
	assert_eq(source.by_name, "named")


func test_integer_division_stays_integer() -> void:
	assert_eq(source.integer_division(), 3)


func test_untyped_locals_keep_their_class() -> void:
	assert_eq(source.keeps_class(), 1)
	assert_eq(source.changes_class(), [0, "RefCounted"])
	assert_true(source.becomes_null())


func test_loop_variables() -> void:
	var child := Node.new()
	child.add_child(Node.new())
	source.add_child(child)
	assert_eq(source.child_totals(), 1)


func test_narrowing() -> void:
	var node := Node.new()
	node.add_child(Node.new())
	assert_eq(source.narrowed(node), 1)
	assert_eq(source.narrowed(3), -1)
	assert_eq(source.narrowed_script(3), "none")
	assert_eq(source.narrowed_then_written(node), 5)
	assert_eq(source.not_narrowed(node), 1)
	assert_eq(source.not_narrowed(1), "other")
	node.free()
