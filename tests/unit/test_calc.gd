extends GutTest

var calc


func before_each() -> void:
	calc = load("res://fixtures/calc.gd").new()


func test_add() -> void:
	assert_eq(calc.add(2, 40), 42)


func test_recursion() -> void:
	assert_eq(calc.fib(15), 610)


func test_loop() -> void:
	assert_eq(calc.sum_to(100), 5050)


func test_integer_division_truncates_toward_zero() -> void:
	assert_eq(calc.divide(-7, 2), -3)


func test_int_times_float() -> void:
	assert_almost_eq(calc.mix(3, 1.5), 4.5, 0.0001)


func test_enum_constant() -> void:
	assert_eq(calc.apply(calc.Mode.MUL, 6, 7), 42)


func test_constant() -> void:
	assert_eq(calc.LIMIT, 10)


func test_engine_type_method() -> void:
	assert_almost_eq(calc.length(Vector2(3, 4)), 5.0, 0.0001)


func test_member_state() -> void:
	calc.bump()
	assert_eq(calc.bump(), 2)
	assert_eq(calc.total, 2)


func test_match() -> void:
	assert_eq(calc.grade(100), "perfect")
	assert_eq(calc.grade(0), "none")
	assert_eq(calc.grade(50), "some")
