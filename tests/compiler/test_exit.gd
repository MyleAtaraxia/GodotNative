extends GutTest


func test_process_exits_cleanly_after_a_parallel_loop() -> void:
	var args := ["--headless", "--path", ProjectSettings.globalize_path("res://"), "-s", "res://fixtures/quit_after_parallel.gd"]
	assert_eq(OS.execute(OS.get_executable_path(), args, [], true), 0)


func test_a_failed_thread_does_not_leave_the_team_waiting() -> void:
	var args := ["--headless", "--path", ProjectSettings.globalize_path("res://"), "-s", "res://fixtures/quit_after_failed_team.gd"]
	var output := []
	assert_eq(OS.execute(OS.get_executable_path(), args, output, true), 0)
	assert_string_contains("".join(output), "TEAM FINISHED")
