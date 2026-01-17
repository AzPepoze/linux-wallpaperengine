package types

import (
	"linux-wallpaperengine/src/convert"
	"linux-wallpaperengine/src/wallpaper"
	"linux-wallpaperengine/src/wallpaper/feature"

	rl "github.com/gen2brain/raylib-go/raylib"
)

type RenderObject struct {
	Object         *wallpaper.Object
	Image          *rl.Texture2D
	RenderTexture  *rl.RenderTexture2D
	ParticleSystem *feature.ParticleSystem
	Offset         wallpaper.Vec2
	Cropoffset     wallpaper.Vec2
	Mesh           *convert.MDLMesh
	Effects        []wallpaper.LoadedEffect
	PingPong       [2]*rl.RenderTexture2D
}
