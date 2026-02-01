package particle

import (
	"linux-wallpaperengine/internal/wallpaper"

	rl "github.com/gen2brain/raylib-go/raylib"
)

type Particle struct {
	Position     wallpaper.Vec3
	Velocity     wallpaper.Vec3
	Color        wallpaper.Vec3
	Life         float64
	MaxLife      float64
	Alpha        float64
	InitialAlpha float64
	Rotation     float64
	AngularVel   float64
	Size         float64
	InitialSize  float64
	SpawnTime    float64
	SpriteFrame  int
	GridX        int
	GridY        int
	RandomValue  float64
}

type ParticleSystem struct {
	Name          string
	Config        wallpaper.ParticleJSON
	Texture       *rl.Texture2D
	ExtraTextures []*rl.Texture2D
	TextureName   string
	Particles     []*Particle
	Timer         float64
	GlobalTime    float64
	Override      *wallpaper.InstanceOverride
	ControlPts    []wallpaper.Vec3
	MousePos      wallpaper.Vec3
	BlendMode     rl.BlendMode
	TexInfo       *wallpaper.TexJSON
}

type ParticleSystemOptions struct {
	Name          string
	Config        wallpaper.ParticleJSON
	Texture       *rl.Texture2D
	ExtraTextures []*rl.Texture2D
	TextureName   string
	Override      *wallpaper.InstanceOverride
	BlendMode     rl.BlendMode
	TexInfo       *wallpaper.TexJSON
}
