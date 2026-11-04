extends GutTest

var loaded: Script
var parallel: RefCounted


func before_each() -> void:
	loaded = load("res://fixtures/parallel.gd")
	parallel = loaded.new()


func test_sum_keeps_the_starting_value() -> void:
	assert_eq(parallel.sum(100000), 5 + 4999950000)


func test_product() -> void:
	assert_eq(parallel.product(1000), 48)


func test_min_and_max() -> void:
	assert_eq(parallel.extremes(PackedFloat64Array([3.0, -2.5, 9.0, 1.0])), [-2.5, 9.0])


func test_writes_by_index() -> void:
	var out: PackedInt64Array = parallel.squares(10000)
	assert_eq(out.size(), 10000)
	assert_eq(out[9999], 9999 * 9999)
	assert_eq(Array(out).reduce(func(a, b): return a + b), 333283335000)


func test_negative_step() -> void:
	assert_eq(parallel.negative_step(), PackedInt64Array([2, 4, 6, 8, 10]))


func test_empty_range() -> void:
	assert_eq(parallel.sum(0), 5)


func test_writing_an_array_with_a_second_reference() -> void:
	var source := PackedInt64Array([1, 2, 3, 4])
	assert_eq(parallel.writes_to_a_copy(source), PackedInt64Array([-1, -1, -1, -1]))


func test_serial_loops_keep_break_and_continue() -> void:
	assert_eq(parallel.serial_loops_inside(100), 150)


func test_nested_regions() -> void:
	assert_eq(parallel.nested(40), 1600)


func threaded() -> bool:
	return loaded.get_class() == "NativeScript" and OS.get_processor_count() > 1


func test_uses_threads_only_when_native() -> void:
	var threads: int = parallel.thread_count(100000)
	if threaded():
		assert_gt(threads, 1)
	else:
		assert_eq(threads, 1)


func test_for_inside_a_parallel_block() -> void:
	assert_eq(parallel.team_sum(100000), 4999950000)


func test_critical() -> void:
	var seen: Array = parallel.critical_append(2000)
	assert_eq(seen.size(), 2000)
	assert_eq(seen[1999], 1999)


func test_atomic_on_a_local() -> void:
	assert_eq(parallel.atomic_count(100000), 100000)


func test_atomic_on_a_packed_element() -> void:
	assert_eq(parallel.atomic_element(4000), PackedFloat64Array([500.0, 500.0, 500.0, 500.0]))


func test_single_runs_once() -> void:
	assert_eq(parallel.single_runs(), 1)


func test_barrier_and_nowait() -> void:
	assert_eq(parallel.barrier_errors(50000), 0)


func test_private() -> void:
	assert_eq(parallel.private_values(1000), 999000)


func test_firstprivate() -> void:
	assert_eq(parallel.firstprivate_values(1000), 3000 + 499500)


func test_num_threads() -> void:
	assert_lte(parallel.threads_with_limit(100000, 2), 2)


func test_if_clause() -> void:
	assert_eq(parallel.threads_when(100000, false), 1)
	if threaded():
		assert_gt(parallel.threads_when(100000, true), 1)


func test_simd_reduction() -> void:
	var a := PackedFloat64Array([1.0, 2.0, 3.0, 4.0, 5.0])
	assert_eq(parallel.simd_dot(a, a), 55.0)


func test_simd_writes() -> void:
	var a := PackedFloat64Array([1.0, 2.0, 3.0])
	assert_eq(parallel.simd_scale(a, 2.0), PackedFloat64Array([2.0, 4.0, 6.0]))
