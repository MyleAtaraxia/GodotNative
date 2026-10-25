#pragma native
extends CharacterBody2D

signal died(score: int)
signal health_changed(current: int, maximum: int)

const MAX_HEALTH := 26
const SPEED := 186.0
const SIGHT := 126.0
const REACH := 16.0
const SCORE := 26 * 3

enum State { IDLE, CHASE, ATTACK, FLEE }

var state := State.IDLE
var health := MAX_HEALTH
var target: Node2D
var cooldown := 0.0
var wander := Vector2.ZERO
var rng := RandomNumberGenerator.new()

func _ready() -> void:
	rng.seed = 10106
	wander = random_direction()

func random_direction() -> Vector2:
	var angle: float = rng.randf() * TAU
	return Vector2(cos(angle), sin(angle))

func _physics_process(delta: float) -> void:
	cooldown = maxf(cooldown - delta, 0.0)
	state = next_state()
	match state:
		State.IDLE:
			velocity = wander * SPEED * 0.25
		State.CHASE:
			velocity = global_position.direction_to(target.global_position) * SPEED
		State.ATTACK:
			velocity = Vector2.ZERO
			attack()
		State.FLEE:
			velocity = target.global_position.direction_to(global_position) * SPEED * 1.2
	move_and_slide()

func next_state() -> State:
	if target == null:
		return State.IDLE
	var distance: float = global_position.distance_to(target.global_position)
	if health * 4 < MAX_HEALTH:
		return State.FLEE
	if distance < REACH:
		return State.ATTACK
	if distance < SIGHT:
		return State.CHASE
	return State.IDLE

func attack() -> void:
	if cooldown > 0.0:
		return
	cooldown = 6.0 / 10.0
	if target.has_method("take_damage"):
		target.call("take_damage", 5)

func take_damage(amount: int) -> void:
	health = maxi(health - amount, 0)
	health_changed.emit(health, MAX_HEALTH)
	if health == 0:
		died.emit(SCORE)
		queue_free()
