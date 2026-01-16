package types

import (
	"linux-wallpaperengine/src/wallpaper"
	"linux-wallpaperengine/src/wallpaper/feature"

	"github.com/hajimehoshi/ebiten/v2"
)

type RenderObject struct {
	*wallpaper.Object
	Image          *ebiten.Image
	Offset         wallpaper.Vec2
	ParticleSystem *feature.ParticleSystem
}