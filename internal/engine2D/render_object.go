package engine2D

import (
	"linux-wallpaperengine/internal/convert"
	"linux-wallpaperengine/internal/wallpaper"

	rl "github.com/gen2brain/raylib-go/raylib"
)

type RenderObject struct {
	Object         *wallpaper.Object
	Image          *rl.Texture2D
	RenderTexture  *rl.RenderTexture2D
	ParticleSystem *ParticleSystem
	Offset         wallpaper.Vec2
	Cropoffset     wallpaper.Vec2
	Mesh           *convert.MDLMesh
	Effects        []LoadedEffect
	PingPong       [2]*rl.RenderTexture2D
}
