extends GutTest

var collections


func before_each() -> void:
	collections = load("res://fixtures/collections.gd").new()


func test_typed_array() -> void:
	var squares: Array[int] = collections.squares(4)
	assert_eq(squares, [0, 1, 4, 9] as Array[int])
	assert_eq(squares.get_typed_builtin(), TYPE_INT)


func test_dictionary() -> void:
	var words: Array[String] = ["a", "b", "a"]
	assert_eq(collections.count_words(words), {"a": 2, "b": 1})


func test_string() -> void:
	assert_eq(collections.shout("hi"), "HI!")


func test_lambda() -> void:
	var values: Array[int] = [1, 2]
	assert_eq(collections.doubled(values), [2, 4])


func test_await() -> void:
	assert_eq(await collections.later(1), 2)
