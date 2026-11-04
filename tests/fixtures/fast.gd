#pragma native unsafe
class_name Fast
extends RefCounted


func sum_array(values: Array[int]) -> int:
	var sum: int = 0
	for value: int in values:
		sum += value
	return sum
