package engine2D

import (
	"image/color"
	"time"

	"linux-wallpaperengine/internal/convert"
	"linux-wallpaperengine/internal/engine2D/particle"
	"linux-wallpaperengine/internal/engine2D/shader"
	"linux-wallpaperengine/internal/wallpaper"

	rl "github.com/gen2brain/raylib-go/raylib"
)

// Renderer handles 2D rendering of objects and effects.
type Renderer struct {
	RenderObjects []RenderObject
	SceneOffsetX  float64
	SceneOffsetY  float64
	SceneWidth    int
	SceneHeight   int
	RenderScale   float64
	BgColor       color.RGBA
	DummyTexture  *rl.Texture2D
	MaskShader    rl.Shader
	MouseX        float64
	MouseY        float64
	StartTime     time.Time
}

type RenderObject struct {
	Object         *wallpaper.Object
	Image          *rl.Texture2D
	RenderTexture  *rl.RenderTexture2D
	ParticleSystem *particle.ParticleSystem
	Offset         wallpaper.Vec2
	Cropoffset     wallpaper.Vec2
	Mesh           *convert.MDLMesh
	Effects        []shader.LoadedEffect
	PingPong       [2]*rl.RenderTexture2D
}
