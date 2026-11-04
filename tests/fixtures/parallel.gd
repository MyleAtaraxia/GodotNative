#pragma native
extends RefCounted


func sum(count: int) -> int:
	var total: int = 5
	#pragma omp parallel for reduction(+:total)
	for i in range(count):
		total += i
	return total


func product(count: int) -> int:
	var total: int = 3
	#pragma omp parallel for reduction(*:total)
	for i in count:
		total *= 2 if i < 4 else 1
	return total


func extremes(values: PackedFloat64Array) -> Array:
	var low: float = values[0]
	var high: float = values[0]
	#pragma omp parallel for reduction(min:low) reduction(max:high)
	for i in range(values.size()):
		low = minf(low, values[i])
		high = maxf(high, values[i])
	return [low, high]


func squares(count: int) -> PackedInt64Array:
	var out := PackedInt64Array()
	out.resize(count)
	#pragma omp parallel for schedule(dynamic, 8)
	for i in range(count):
		out[i] = i * i
	return out


func negative_step() -> PackedInt64Array:
	var out := PackedInt64Array()
	out.resize(5)
	#pragma omp parallel for
	for i in range(10, 0, -2):
		out[i / 2 - 1] = i
	return out


func writes_to_a_copy(source: PackedInt64Array) -> PackedInt64Array:
	var copy := source
	#pragma omp parallel for
	for i in range(copy.size()):
		copy[i] = -1
	return copy


func serial_loops_inside(count: int) -> int:
	var total: int = 0
	#pragma omp parallel for reduction(+:total)
	for i in range(count):
		if i % 2 == 0:
			continue
		for j in range(100):
			if j == 3:
				break
			total += 1
	return total


func nested(count: int) -> int:
	var total: int = 0
	#pragma omp parallel for reduction(+:total)
	for i in range(count):
		#pragma omp parallel for reduction(+:total)
		for j in range(count):
			total += 1
	return total


func thread_count(count: int) -> int:
	var ids := PackedInt64Array()
	ids.resize(count)
	#pragma omp parallel for
	for i in range(count):
		ids[i] = OS.get_thread_caller_id()
	var seen := {}
	for id in ids:
		seen[id] = true
	return seen.size()


func team_sum(count: int) -> int:
	var total: int = 0
	#pragma omp parallel reduction(+:total)
	#pragma omp for schedule(guided)
	for i in range(count):
		total += i
	#pragma end parallel
	return total


func critical_append(count: int) -> Array:
	var seen: Array = []
	#pragma omp parallel for
	for i in range(count):
		#pragma omp critical(seen)
		seen.append(i)
		#pragma omp end critical
	seen.sort()
	return seen


func atomic_count(count: int) -> int:
	var hits: int = 0
	#pragma omp parallel for shared(hits)
	for i in range(count):
		#pragma omp atomic
		hits += 1
	return hits


func atomic_element(count: int) -> PackedFloat64Array:
	var bins := PackedFloat64Array()
	bins.resize(4)
	#pragma omp parallel for
	for i in range(count):
		#pragma omp atomic
		bins[i % 4] += 0.5
	return bins


func single_runs() -> int:
	var runs: int = 0
	#pragma omp parallel
	#pragma omp single
	runs += 1
	#pragma end single
	#pragma end parallel
	return runs


func barrier_errors(count: int) -> int:
	var done := PackedInt64Array()
	done.resize(count)
	var errors: int = 0
	#pragma omp parallel reduction(+:errors)
	#pragma omp for nowait
	for i in range(count):
		done[i] = 1
	#pragma omp barrier
	#pragma omp for
	for i in range(count):
		if done[i] != 1:
			errors += 1
	#pragma end parallel
	return errors


func private_values(count: int) -> int:
	var scratch: int = 7
	var total: int = 0
	#pragma omp parallel for private(scratch) reduction(+:total)
	for i in range(count):
		scratch = i * 2
		total += scratch
	return total


func firstprivate_values(count: int) -> int:
	var base: int = 3
	var total: int = 0
	#pragma omp parallel for firstprivate(base) reduction(+:total)
	for i in range(count):
		total += base + i
	return total


func threads_with_limit(count: int, limit: int) -> int:
	var ids := PackedInt64Array()
	ids.resize(count)
	#pragma omp parallel for num_threads(limit * 1)
	for i in range(count):
		ids[i] = OS.get_thread_caller_id()
	var seen := {}
	for id in ids:
		seen[id] = true
	return seen.size()


func threads_when(count: int, enabled: bool) -> int:
	var ids := PackedInt64Array()
	ids.resize(count)
	#pragma omp parallel for if(enabled and count > 0)
	for i in range(count):
		ids[i] = OS.get_thread_caller_id()
	var seen := {}
	for id in ids:
		seen[id] = true
	return seen.size()


func simd_dot(a: PackedFloat64Array, b: PackedFloat64Array) -> float:
	var total: float = 0.0
	#pragma omp simd reduction(+:total)
	for i in range(a.size()):
		total += a[i] * b[i]
	return total


func simd_scale(a: PackedFloat64Array, factor: float) -> PackedFloat64Array:
	var out := PackedFloat64Array()
	out.resize(a.size())
	#pragma omp simd
	for i in range(a.size()):
		out[i] = a[i] * factor
	return out
