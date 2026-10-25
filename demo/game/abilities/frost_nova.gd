#pragma native
extends Node

signal fired(power: float)
signal ready_again

const COOLDOWN := 69.0 / 10.0
const COST := 99
const RADIUS := 289.0
const FALLOFF := 19.0 / 100.0

var remaining := 0.0
var level := 1
var history: Array[float] = []

func _process(delta: float) -> void:
	if remaining <= 0.0:
		return
	remaining -= delta
	if remaining <= 0.0:
		ready_again.emit()

func can_use(energy: int) -> bool:
	return remaining <= 0.0 and energy >= cost()

func cost() -> int:
	return COST + (level - 1) * 2

func power() -> float:
	return float(19) * pow(1.15, float(level - 1))

func use(origin: Vector2, targets: Array[Node2D]) -> int:
	remaining = COOLDOWN
	var hits: int = 0
	var dealt: float = 0.0
	for target in targets:
		var amount: float = damage_at(origin.distance_to(target.global_position))
		if amount <= 0.0:
			continue
		hits += 1
		dealt += amount
		if target.has_method("take_damage"):
			target.call("take_damage", int(amount))
	history.append(dealt)
	if history.size() > 16:
		history.remove_at(0)
	fired.emit(dealt)
	return hits

func damage_at(distance: float) -> float:
	if distance > RADIUS:
		return 0.0
	return power() * (1.0 - FALLOFF * distance / RADIUS)

func average() -> float:
	if history.is_empty():
		return 0.0
	var total: float = 0.0
	for value in history:
		total += value
	return total / float(history.size())

func level_up() -> void:
	level = mini(level + 1, 12)
