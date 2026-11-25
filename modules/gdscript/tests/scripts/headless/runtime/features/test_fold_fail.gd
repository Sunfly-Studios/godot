extends SceneTree

const BAD_RANDOM = [1, 2, 3].pick_random()

func _init():
	print(BAD_RANDOM)
	quit()