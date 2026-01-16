package types

import (
	"linux-wallpaperengine/src/wallpaper"
	"linux-wallpaperengine/src/wallpaper/feature"

	rl "github.com/gen2brain/raylib-go/raylib"
)

type RenderObject struct {

	Object         *wallpaper.Object

	Image          *rl.Texture2D

	RenderTexture  *rl.RenderTexture2D

	Offset         wallpaper.Vec2

	ParticleSystem *feature.ParticleSystem

}
