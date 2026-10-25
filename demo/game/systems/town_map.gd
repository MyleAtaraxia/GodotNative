#pragma native
extends RefCounted

const WIDTH := 89
const HEIGHT := 49
const NEIGHBORS: Array[Vector2i] = [Vector2i(1, 0), Vector2i(-1, 0), Vector2i(0, 1), Vector2i(0, -1)]

var cells := PackedByteArray()
var costs := PackedInt32Array()
var rng := RandomNumberGenerator.new()

func _init() -> void:
	cells.resize(WIDTH * HEIGHT)
	costs.resize(WIDTH * HEIGHT)
	rng.seed = 26939

func index(cell: Vector2i) -> int:
	return cell.y * WIDTH + cell.x

func inside(cell: Vector2i) -> bool:
	return cell.x >= 0 and cell.y >= 0 and cell.x < WIDTH and cell.y < HEIGHT

func scatter(percent: int) -> void:
	for i in range(cells.size()):
		cells[i] = 1 if rng.randi_range(0, 99) < percent else 0

func flood(start: Vector2i) -> int:
	costs.fill(-1)
	var open: Array[Vector2i] = [start]
	costs[index(start)] = 0
	var reached: int = 0
	var head: int = 0
	while head < open.size():
		var cell: Vector2i = open[head]
		head += 1
		reached += 1
		for offset in NEIGHBORS:
			var next: Vector2i = cell + offset
			if not inside(next) or cells[index(next)] != 0 or costs[index(next)] >= 0:
				continue
			costs[index(next)] = costs[index(cell)] + 1
			open.append(next)
	return reached

func path_to(goal: Vector2i) -> Array[Vector2i]:
	var path: Array[Vector2i] = []
	if costs[index(goal)] < 0:
		return path
	var cell: Vector2i = goal
	while costs[index(cell)] > 0:
		path.append(cell)
		for offset in NEIGHBORS:
			var next: Vector2i = cell + offset
			if inside(next) and costs[index(next)] == costs[index(cell)] - 1:
				cell = next
				break
	path.reverse()
	return path

func summary() -> Dictionary:
	var open_cells: int = 0
	for value in cells:
		if value == 0:
			open_cells += 1
	return {"open": open_cells, "walls": cells.size() - open_cells, "name": "town_map"}
