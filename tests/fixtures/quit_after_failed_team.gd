extends SceneTree


func _init() -> void:
	var team: Object = load("res://fixtures/failed_team.gd").new()
	team.fails_before_barrier(PackedInt64Array())
	team.fails_before_worksharing(PackedInt64Array())
	print("TEAM FINISHED")
	quit()
