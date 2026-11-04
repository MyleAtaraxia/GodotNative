#pragma native
extends Node2D

const SIZE := Vector2(1280, 720)
const MODES := [
	["interpreted", "res://boids_interpreted.gd"],
	["native", "res://boids_native.gd"],
	["native unsafe", "res://boids_native_unsafe.gd"],
	["native parallel", "res://boids_native_parallel.gd"],
]

var mode := 1
var count := 1500
var sim: RefCounted
var flock: MultiMeshInstance2D
var label: Label
var sim_ms := 0.0

func _ready() -> void:
	if "--bench" in OS.get_cmdline_user_args():
		set_process(false)
		bench()
		get_tree().quit()
		return
	flock = MultiMeshInstance2D.new()
	flock.multimesh = MultiMesh.new()
	flock.multimesh.transform_format = MultiMesh.TRANSFORM_2D
	flock.multimesh.mesh = triangle()
	add_child(flock)
	var layer := CanvasLayer.new()
	label = Label.new()
	label.position = Vector2(12, 8)
	layer.add_child(label)
	add_child(layer)
	restart()

func triangle() -> ArrayMesh:
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = PackedVector2Array([Vector2(7, 0), Vector2(-5, 4), Vector2(-5, -4)])
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	return mesh

func make_sim(index: int, boids: int) -> RefCounted:
	var s: RefCounted = load(MODES[index][1]).new()
	s.setup(boids, SIZE.x, SIZE.y)
	return s

func restart() -> void:
	sim = make_sim(mode, count)
	flock.multimesh.instance_count = count

func _unhandled_input(event: InputEvent) -> void:
	if not event.is_pressed():
		return
	if event.is_action("ui_accept"):
		mode = (mode + 1) % MODES.size()
		restart()
	elif event.is_action("ui_up"):
		count += 500
		restart()
	elif event.is_action("ui_down"):
		count = maxi(500, count - 500)
		restart()

func _process(_delta: float) -> void:
	var start := Time.get_ticks_usec()
	sim.step()
	var ms := (Time.get_ticks_usec() - start) / 1000.0
	sim_ms = lerpf(sim_ms, ms, 0.1)
	flock.multimesh.buffer = PackedFloat32Array(Array(sim.transforms()))
	label.text = "%s (running as %s)\n%d boids, %.1f ms per step, %d FPS\nSpace: switch mode    Up/Down: boids" % [
		MODES[mode][0], sim.get_script().get_class(), count, sim_ms, Engine.get_frames_per_second()]

func bench() -> void:
	print("BOIDS main.gd runs as ", get_script().get_class())
	var base := 0.0
	for i in range(MODES.size()):
		var s := make_sim(i, count)
		var best := INF
		for run in range(5):
			var start := Time.get_ticks_usec()
			for n in range(4):
				s.step()
			best = minf(best, (Time.get_ticks_usec() - start) / 1000.0 / 4.0)
		if i == 0:
			base = best
		var check: float = s.checksum()
		print("BOIDS %d %-14s %-13s %8.2f ms/step  %5.1fx  check=%.6f" % [
			count, MODES[i][0], s.get_script().get_class(), best, base / best, check])
