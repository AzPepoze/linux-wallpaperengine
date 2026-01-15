package feature

import (
	"linux-wallpaperengine/src/wallpaper"
)

func UpdateParallax(objects []wallpaper.Object, offsets []wallpaper.Vec2, mouseX, mouseY float64, amount float64) {
	for i := range objects {
		object := &objects[i]
		// Parallax formula: mouseDist * amount * depth
		// Base multiplier 100 for visible movement
		offsets[i].X += mouseX * amount * 100 * object.ParallaxDepth.X
		offsets[i].Y += mouseY * amount * 100 * object.ParallaxDepth.Y
	}
}
