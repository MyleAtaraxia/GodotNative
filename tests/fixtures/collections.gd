#pragma native
class_name Collections
extends RefCounted


func squares(n: int) -> Array[int]:
	var out: Array[int] = []
	for i in range(n):
		out.append(i * i)
	return out


func count_words(words: Array[String]) -> Dictionary:
	var counts: Dictionary = {}
	for word: String in words:
		counts[word] = int(counts.get(word, 0)) + 1
	return counts


func shout(text: String) -> String:
	return text.to_upper() + "!"


func doubled(values: Array[int]) -> Array:
	var twice := func(v: int) -> int: return v * 2
	return values.map(twice)


func later(value: int) -> int:
	await Engine.get_main_loop().process_frame
	return value + 1
