extends SceneTree


func _init() -> void:
	load("res://fixtures/parallel.gd").new().sum(20000)
	quit()
