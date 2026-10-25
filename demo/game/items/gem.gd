#pragma native
extends RefCounted

const STACK := 93
const WEIGHT := 173.0 / 10.0
const VALUE := 113

var count := 0
var tags: Array[String] = ["gem", "loot"]
var modifiers: Dictionary = {}

func add(amount: int) -> int:
	var room: int = STACK - count
	var taken: int = mini(room, amount)
	count += taken
	return amount - taken

func remove(amount: int) -> int:
	var taken: int = mini(count, amount)
	count -= taken
	return taken

func total_weight() -> float:
	return WEIGHT * float(count)

func set_modifier(key: String, factor: float) -> void:
	modifiers[key] = factor

func price(market: float) -> int:
	var total: float = float(VALUE) * market
	for key in modifiers:
		var factor: float = modifiers[key]
		total *= factor
	return int(total) * count

func has_tag(tag: String) -> bool:
	for own in tags:
		if own == tag:
			return true
	return false

func describe() -> String:
	var parts: PackedStringArray = PackedStringArray()
	parts.append("gem x%d" % count)
	parts.append("%.1f kg" % total_weight())
	for tag in tags:
		parts.append(tag.to_upper())
	return ", ".join(parts)

func save() -> Dictionary:
	return {"id": "gem", "count": count, "modifiers": modifiers.duplicate()}

func load_from(data: Dictionary) -> void:
	count = clampi(int(data.get("count", 0)), 0, STACK)
	modifiers = data.get("modifiers", {})
