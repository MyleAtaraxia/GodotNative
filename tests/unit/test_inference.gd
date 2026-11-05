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


func test_parameters_seen_at_call_sites() -> void:
	assert_eq(source.uses_plus_one(), 2)
	assert_eq(source.plus_one(1), 2)
	assert_eq(source.uses_fib(), 55)
	assert_eq(source.fib(10), 55)
	assert_eq(source.uses_static(), 2)
	assert_eq(source.uses_packed_size(), 2)
	assert_eq(source.uses_pushed_to(), [1])


func test_parameters_called_with_another_type() -> void:
	assert_eq(source.plus_one(1.5), 2.5)
	assert_eq(source.plus_one(2.0), 3.0)
	assert_eq(source.fib(2.0), 1.0)
	assert_eq(source.static_plus_one(1.5), 2.5)
	assert_eq(source.packed_size([1, 2, 3]), 3)
	assert_eq(source.packed_size("abcd".to_utf8_buffer()), 4)


func test_parameters_written_in_the_body() -> void:
	assert_eq(source.uses_reassigned(), ["text", 0])
	assert_eq(source.reassigned(1), "text")
	assert_eq(source.counted_down(2.5), -0.5)


func test_parameter_defaults() -> void:
	assert_eq(source.with_default(), 4)
	assert_eq(source.with_default(3), 6)
	assert_eq(source.with_default(1.5), 3.0)


func test_parameters_with_two_types() -> void:
	assert_eq(source.uses_two_kinds(), [1, "a"])


func test_guess_that_cannot_compile_is_dropped() -> void:
	var node := Node.new()
	node.add_child(Node.new())
	assert_eq(source.uses_only_nodes(), 7)
	assert_eq(source.only_nodes(node), 1)
	node.free()


func test_parameters_reach_overrides() -> void:
	assert_eq(source.uses_scaled_by(), [2, 4])
