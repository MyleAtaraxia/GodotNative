extends GutTest


func test_process_exits_cleanly_after_a_parallel_loop() -> void:
	var args := ["--headless", "--path", ProjectSettings.globalize_path("res://"), "-s", "res://fixtures/quit_after_parallel.gd"]
	assert_eq(OS.execute(OS.get_executable_path(), args, [], true), 0)
