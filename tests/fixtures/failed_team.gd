#pragma native
extends RefCounted


func fails_before_barrier(list: PackedInt64Array) -> void:
	var first: int = 0
	#pragma omp parallel
	var mine: int = 0
	#pragma omp critical
	first += 1
	mine = first
	#pragma end critical
	if mine == 1:
		list[100] = 1
	#pragma omp barrier
	#pragma end parallel


func fails_before_worksharing(list: PackedInt64Array) -> void:
	var first: int = 0
	var total: int = 0
	#pragma omp parallel
	var mine: int = 0
	#pragma omp critical
	first += 1
	mine = first
	#pragma end critical
	if mine == 1:
		list[100] = 1
	#pragma omp for reduction(+:total)
	for i in range(1000):
		total += i
	#pragma omp single
	total += 1
	#pragma end single
	#pragma end parallel
