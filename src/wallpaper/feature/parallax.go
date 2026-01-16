package feature

import (
	"linux-wallpaperengine/src/wallpaper"
)

func UpdateParallax(objects []wallpaper.Object, offsets []wallpaper.Vec2, mouseX, mouseY float64, amount float64) {
	for i := range objects {
		object := &objects[i]
		dx := mouseX * amount * 100 * object.ParallaxDepth.X
		dy := mouseY * amount * 100 * object.ParallaxDepth.Y

		offsets[i].X += dx
		offsets[i].Y += dy
	}
}
