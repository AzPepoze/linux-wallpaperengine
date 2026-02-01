package particle

import (
	"image/color"
	"math"

	"linux-wallpaperengine/internal/wallpaper"

	rl "github.com/gen2brain/raylib-go/raylib"
)

var fallbackTexture *rl.Texture2D

// Draw renders all particles in the system.
func (ps *ParticleSystem) Draw(originX, originY float64, objScale wallpaper.Vec3) {
	img := ps.Texture
	if img == nil {
		if fallbackTexture == nil {
			i := rl.GenImageColor(2, 2, rl.White)
			t := rl.LoadTextureFromImage(i)
			rl.UnloadImage(i)
			fallbackTexture = &t
		}
		img = fallbackTexture
	}

	rendererType := ""
	if len(ps.Config.Renderer) > 0 {
		rendererType = ps.Config.Renderer[0].Name
	}

	sequenceMultiplier := ps.Config.SequenceMultiplier
	useSpriteSheet := sequenceMultiplier > 1

	gridSize := int(sequenceMultiplier * 4)
	if gridSize <= 1 {
		gridSize = 1
	}

	texGridX := 0
	texGridY := 0

	if ps.TexInfo != nil && len(ps.TexInfo.SpriteSheetSequences) > 0 {
		seq := ps.TexInfo.SpriteSheetSequences[0]
		if seq.Width > 0 && seq.Height > 0 && ps.Texture != nil {
			texGridX = int(ps.Texture.Width) / seq.Width
			texGridY = int(ps.Texture.Height) / seq.Height
			useSpriteSheet = true
		}
	}

	hasGridParticles := false
	if len(ps.Particles) > 0 {
		for _, p := range ps.Particles {
			if p.GridX > 0 && p.GridY > 0 {
				hasGridParticles = true
				break
			}
		}
	}

	if (hasGridParticles || useSpriteSheet) && rendererType == "" {
		rendererType = "sprite"
	}

	width := float32(img.Width)
	height := float32(img.Height)

	rl.BeginBlendMode(ps.BlendMode)

	for _, particle := range ps.Particles {
		isSprite := (rendererType == "sprite" && useSpriteSheet) || (particle.GridX > 0 && particle.GridY > 0)
		if isSprite {
			gridSizeX := gridSize
			gridSizeY := gridSize

			if texGridX > 0 && texGridY > 0 {
				gridSizeX = texGridX
				gridSizeY = texGridY
			}

			if particle.GridX > 0 {
				gridSizeX = particle.GridX
			}
			if particle.GridY > 0 {
				gridSizeY = particle.GridY
			}

			totalFrames := gridSizeX * gridSizeY

			frameIndex := particle.SpriteFrame
			if frameIndex < 0 {
				ageRatio := (particle.MaxLife - particle.Life) / particle.MaxLife
				frameIndex = int(ageRatio * float64(totalFrames))
			}

			if frameIndex >= totalFrames {
				frameIndex = totalFrames - 1
			}
			if frameIndex < 0 {
				frameIndex = 0
			}

			spriteWidth := width / float32(gridSizeX)
			spriteHeight := height / float32(gridSizeY)
			srcX := float32(frameIndex%gridSizeX) * spriteWidth
			srcY := float32(frameIndex/gridSizeX) * spriteHeight

			sourceRec := rl.NewRectangle(srcX, srcY, spriteWidth, spriteHeight)

			scale := float32(particle.Size / 1000.0)
			finalScaleX := scale * float32(objScale.X)
			finalScaleY := scale * float32(objScale.Y)

			destWidth := spriteWidth * finalScaleX
			destHeight := spriteHeight * finalScaleY

			destX := float32(originX + particle.Position.X*objScale.X)
			destY := float32(originY - particle.Position.Y*objScale.Y)

			destRec := rl.NewRectangle(destX, destY, destWidth, destHeight)
			origin := rl.NewVector2(destWidth/2, destHeight/2)

			rotation := float32(particle.Rotation * 180.0 / math.Pi)

			color := rl.NewColor(
				uint8(particle.Color.X*255),
				uint8(particle.Color.Y*255),
				uint8(particle.Color.Z*255),
				uint8(particle.Alpha*255),
			)

			rl.DrawTexturePro(*img, sourceRec, destRec, origin, rotation, color)

		} else {
			sourceRec := rl.NewRectangle(0, 0, width, height)

			scale := float32(particle.Size / 100.0)
			finalScaleX := scale * float32(objScale.X)
			finalScaleY := scale * float32(objScale.Y)

			destWidth := width * finalScaleX
			destHeight := height * finalScaleY

			destX := float32(originX + particle.Position.X*objScale.X)
			destY := float32(originY + particle.Position.Y*objScale.Y)

			destRec := rl.NewRectangle(destX, destY, destWidth, destHeight)
			origin := rl.NewVector2(destWidth/2, destHeight/2)
			rotation := float32(particle.Rotation * 180.0 / math.Pi)

			color := rl.NewColor(
				uint8(particle.Color.X*255),
				uint8(particle.Color.Y*255),
				uint8(particle.Color.Z*255),
				uint8(particle.Alpha*255),
			)

			rl.DrawTexturePro(*img, sourceRec, destRec, origin, rotation, color)
		}
	}
	rl.EndBlendMode()
}

// ApplyEffects applies effect properties to objects (opacity, tint, etc).
func ApplyEffects(obj *wallpaper.Object, alpha *float64, tint *color.RGBA) {
	for _, effect := range obj.Effects {
		if !effect.Visible.GetBool() {
			continue
		}

		if effect.Name == "opacity" {
			*alpha *= effect.Alpha.GetFloat()
		}
		if effect.Name == "tint" {
			if len(effect.Passes) > 0 {
				constantColor := effect.Passes[0].ConstantColor
				tint.R = uint8(float64(tint.R) * constantColor.X)
				tint.G = uint8(float64(tint.G) * constantColor.Y)
				tint.B = uint8(float64(tint.B) * constantColor.Z)
			}
		}
	}
}
