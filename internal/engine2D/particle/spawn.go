package particle

import (
	"fmt"
	"math"
	"math/rand"
	"path/filepath"
	"strings"

	"linux-wallpaperengine/internal/wallpaper"
)

// spawnParticle creates and initializes a new particle from an emitter.
func (ps *ParticleSystem) spawnParticle(emitter wallpaper.ParticleEmitter) {
	particle := &Particle{
		Position:     wallpaper.Vec3{X: 0, Y: 0, Z: 0},
		Velocity:     wallpaper.Vec3{X: 0, Y: 0, Z: 0},
		Color:        wallpaper.Vec3{X: 1, Y: 1, Z: 1},
		Life:         1.0,
		MaxLife:      1.0,
		Alpha:        1.0,
		InitialAlpha: 1.0,
		Rotation:     0,
		AngularVel:   0,
		Size:         1.0,
		InitialSize:  1.0,
		SpawnTime:    ps.GlobalTime,
		SpriteFrame:  -1,
		RandomValue:  rand.Float64() * math.Pi * 2.0,
	}

	particle.Position = GetVec3OrFloat(emitter.Origin)

	if ps.Config.AnimationMode == "randomframe" {
		var distMax wallpaper.Vec3
		gridSetSuccess := false

		if ps.TexInfo != nil && len(ps.TexInfo.SpriteSheetSequences) > 0 {
			seq := ps.TexInfo.SpriteSheetSequences[0]
			if seq.Width > 0 && seq.Height > 0 && ps.Texture != nil {
				particle.GridX = int(ps.Texture.Width) / seq.Width
				particle.GridY = int(ps.Texture.Height) / seq.Height
				if seq.Frames > 0 {
					particle.SpriteFrame = rand.Intn(seq.Frames)
				} else {
					particle.SpriteFrame = rand.Intn(particle.GridX * particle.GridY)
				}
				gridSetSuccess = true
				goto gridSet
			}
		}

		distMax = GetVec3OrFloat(emitter.DistanceMax)
		particle.GridX = int(distMax.X)
		particle.GridY = int(distMax.Y)

		if ps.Texture != nil && ps.TextureName != "" {
			parts := strings.Split(filepath.Base(ps.TextureName), "_")
			for _, part := range parts {
				if strings.Contains(part, "x") {
					var fw, fh int
					n, _ := fmt.Sscanf(part, "%dx%d", &fw, &fh)
					if n == 2 && fw > 0 && fh > 0 {
						particle.GridX = int(ps.Texture.Width) / fw
						particle.GridY = int(ps.Texture.Height) / fh

						for _, nextPart := range parts {
							var count int
							if _, err := fmt.Sscanf(nextPart, "%d", &count); err == nil && count > 0 && count <= particle.GridX*particle.GridY {
								particle.SpriteFrame = rand.Intn(count)
								gridSetSuccess = true
								goto gridSet
							}
						}
						gridSetSuccess = true
						goto gridSet
					}
				}
			}
		}

	gridSet:
		if !gridSetSuccess || particle.GridX <= 0 || particle.GridY <= 0 {
			particle.GridX = 0
			particle.GridY = 0
			particle.SpriteFrame = -1
		} else {
			if particle.SpriteFrame < 0 {
				totalSlots := particle.GridX * particle.GridY
				frameCount := totalSlots
				if ps.Config.MaxCount > 0 && ps.Config.MaxCount < totalSlots {
					frameCount = ps.Config.MaxCount
				}
				particle.SpriteFrame = rand.Intn(frameCount)
			}
		}
	}

	switch emitter.Name {
	case "boxrandom":
		distMax := GetVec3OrFloat(emitter.DistanceMax)
		distMin := GetVec3OrFloat(emitter.DistanceMin)

		particle.Position.X += (rand.Float64()*2-1)*(distMax.X-distMin.X)/2 + distMin.X
		particle.Position.Y += (rand.Float64()*2-1)*(distMax.Y-distMin.Y)/2 + distMin.Y
		particle.Position.Z += (rand.Float64()*2-1)*(distMax.Z-distMin.Z)/2 + distMin.Z

	case "sphererandom":
		distMax := GetFloatFromInterface(emitter.DistanceMax)
		distMin := GetFloatFromInterface(emitter.DistanceMin)

		if distMax == 0 && distMin == 0 {
			distMax = 1.0
		} else if distMax == 0 {
			distMax = 100
		}

		angle := rand.Float64() * math.Pi * 2
		elevation := rand.Float64()*math.Pi - math.Pi/2
		radius := distMin + rand.Float64()*(distMax-distMin)

		particle.Position.X += math.Cos(elevation) * math.Cos(angle) * radius
		particle.Position.Y += math.Cos(elevation) * math.Sin(angle) * radius
		particle.Position.Z += math.Sin(elevation) * radius
	}

	for _, init := range ps.Config.Initializer {
		ps.applyInitializer(particle, init)
	}

	if ps.Override != nil {
		if ps.Override.Lifetime.GetFloat() != 0 {
			particle.MaxLife *= ps.Override.Lifetime.GetFloat()
			particle.Life = particle.MaxLife
		}
		if ps.Override.Alpha.GetFloat() != 0 {
			particle.Alpha = ps.Override.Alpha.GetFloat()
			particle.InitialAlpha = ps.Override.Alpha.GetFloat()
		}
		if ps.Override.Size.GetFloat() != 0 {
			particle.Size *= ps.Override.Size.GetFloat()
			particle.InitialSize = particle.Size
		}
		if ps.Override.Speed.GetFloat() != 0 {
			particle.Velocity.X *= ps.Override.Speed.GetFloat()
			particle.Velocity.Y *= ps.Override.Speed.GetFloat()
			particle.Velocity.Z *= ps.Override.Speed.GetFloat()
		}
		if ps.Override.ColorN != "" {
			r, g, b := wallpaper.ParseColor(ps.Override.ColorN)
			particle.Color = wallpaper.Vec3{X: r, Y: g, Z: b}
		}
	}

	ps.Particles = append(ps.Particles, particle)
}
