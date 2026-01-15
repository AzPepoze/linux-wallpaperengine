package main

import (
	"fmt"
	"image/color"
	"math"
	"time"

	"linux-wallpaperengine/src/convert"
	"linux-wallpaperengine/src/utils"
	"linux-wallpaperengine/src/wallpaper"
	"linux-wallpaperengine/src/wallpaper/feature"

	"github.com/hajimehoshi/ebiten/v2"
	"github.com/hajimehoshi/ebiten/v2/ebitenutil"
)

type RenderObject struct {
	*wallpaper.Object
	Image          *ebiten.Image
	Offset         wallpaper.Vec2
	ParticleSystem *feature.ParticleSystem
}

type Window struct {
	scene          wallpaper.Scene
	bgColor        color.RGBA
	renderObjects  []RenderObject
	audioManager   *wallpaper.AudioManager
	mouseX, mouseY float64
	startTime      time.Time
	lastFrameTime  time.Time

	updateObjects []wallpaper.Object
	updateOffsets []wallpaper.Vec2
	debugBuffer   *ebiten.Image
}

func NewWindow(scene wallpaper.Scene) *Window {
	red, green, blue := wallpaper.ParseColor(scene.General.ClearColor)
	audioManager := wallpaper.NewAudioManager()

	window := &Window{
		scene:         scene,
		bgColor:       color.RGBA{uint8(red * 255), uint8(green * 255), uint8(blue * 255), 255},
		audioManager:  audioManager,
		startTime:     time.Now(),
		lastFrameTime: time.Now(),
		renderObjects: make([]RenderObject, 0, len(scene.Objects)),
		updateObjects: make([]wallpaper.Object, len(scene.Objects)),
		updateOffsets: make([]wallpaper.Vec2, len(scene.Objects)),
		debugBuffer:   ebiten.NewImage(400, 100),
	}

	for i := range scene.Objects {
		object := &scene.Objects[i]

		if len(object.Sound.Value) > 0 {
			window.audioManager.Play(object)
		}

		utils.Debug("Adding object: %s", object.Name)
		texturePath := resolveTexturePath(object)
		var image *ebiten.Image
		if texturePath != "" {
			var err error
			image, err = convert.LoadTexture(texturePath)
			if err != nil {
				utils.Warn("Failed to load texture for object %s from %s: %v", object.Name, texturePath, err)
			}
		} else if object.Image != "" {
			utils.Warn("Could not resolve texture path for object %s (Image: %s)", object.Name, object.Image)
		}

		if image == nil && object.GetText() != "" {
			image = ebiten.NewImage(int(object.Size.X), int(object.Size.Y))
		}

		var ps *feature.ParticleSystem
		if object.Particle != "" {
			ps = loadParticleSystem(object.Name, object.Particle, object.InstanceOverride)
		}

		window.renderObjects = append(window.renderObjects, RenderObject{
			Object:         object,
			Image:          image,
			ParticleSystem: ps,
		})
	}

	return window
}

func (window *Window) Update() error {
	currentTime := time.Now()
	deltaTime := currentTime.Sub(window.lastFrameTime).Seconds()
	window.lastFrameTime = currentTime

	globalMouseX, globalMouseY, err := utils.GetGlobalMousePosition()
	if err != nil {
		globalMouseX, globalMouseY = ebiten.CursorPosition()
	}

	window.mouseX = (float64(globalMouseX) / 1920.0 * 2) - 1.0
	window.mouseY = (float64(globalMouseY) / 1080.0 * 2) - 1.0

	for i, renderObject := range window.renderObjects {
		window.updateObjects[i] = *renderObject.Object
		window.updateOffsets[i] = wallpaper.Vec2{X: 0, Y: 0}
		if renderObject.ParticleSystem != nil {
			renderObject.ParticleSystem.Update(deltaTime)
		}
	}

	totalTime := time.Since(window.startTime).Seconds()

	if window.scene.General.CameraParallax {
		feature.UpdateParallax(window.updateObjects, window.updateOffsets, window.mouseX, window.mouseY, window.scene.General.CameraParallaxAmount)
	}
	feature.UpdateClock(window.updateObjects, window.updateOffsets)
	feature.UpdateShake(window.updateObjects, window.updateOffsets, totalTime)

	for i := range window.renderObjects {
		*window.renderObjects[i].Object = window.updateObjects[i]
		window.renderObjects[i].Offset = window.updateOffsets[i]
		if window.renderObjects[i].Image != nil && window.renderObjects[i].GetText() != "" {
			window.renderObjects[i].Image.Fill(color.Transparent)
			feature.RenderText(window.renderObjects[i].Object, window.renderObjects[i].Image)
		}
	}

	return nil
}

func (window *Window) Draw(screen *ebiten.Image) {
	screen.Fill(window.bgColor)

	for _, renderObject := range window.renderObjects {
		if !renderObject.Visible.Value {
			continue
		}

		if renderObject.Image != nil {
			alpha := renderObject.Alpha.Value
			tintColor := color.RGBA{255, 255, 255, 255}
			feature.ApplyEffects(renderObject.Object, &alpha, &tintColor)

			if alpha > 0 {
				targetWidth, targetHeight := renderObject.Size.X, renderObject.Size.Y
				if targetWidth > 0 && targetHeight > 0 {
					options := &ebiten.DrawImageOptions{}
					options.ColorScale.ScaleAlpha(float32(alpha))
					options.ColorScale.Scale(
						float32(tintColor.R)/255.0,
						float32(tintColor.G)/255.0,
						float32(tintColor.B)/255.0,
						1.0,
					)
					options.Filter = ebiten.FilterLinear

					imageWidth, imageHeight := renderObject.Image.Bounds().Dx(), renderObject.Image.Bounds().Dy()
					options.GeoM.Translate(-float64(imageWidth)/2, -float64(imageHeight)/2)

					finalScaleX := (targetWidth / float64(imageWidth)) * renderObject.Scale.X
					finalScaleY := (targetHeight / float64(imageHeight)) * renderObject.Scale.Y
					options.GeoM.Scale(finalScaleX, finalScaleY)

					if renderObject.Angles.Z != 0 {
						radians := renderObject.Angles.Z * (math.Pi / 180.0)
						options.GeoM.Rotate(radians)
					}

					options.GeoM.Translate(renderObject.Origin.X+renderObject.Offset.X, renderObject.Origin.Y+renderObject.Offset.Y)
					screen.DrawImage(renderObject.Image, options)
				}
			}
		}

		if renderObject.ParticleSystem != nil {
			renderObject.ParticleSystem.Draw(screen, renderObject.Origin.X+renderObject.Offset.X, renderObject.Origin.Y+renderObject.Offset.Y, renderObject.Scale)
		}
	}

	window.drawDebugInfo(screen)
}

func (window *Window) drawDebugInfo(screen *ebiten.Image) {
	debugText := fmt.Sprintf("FPS: %.1f\nObjects: %d", ebiten.ActualFPS(), len(window.scene.Objects))

	// Add particle debug info
	for _, renderObject := range window.renderObjects {
		if renderObject.ParticleSystem != nil && len(renderObject.ParticleSystem.Particles) > 0 {
			ps := renderObject.ParticleSystem
			p := ps.Particles[0]
			debugText += fmt.Sprintf("\n\nParticle System: %s\n  Count: %d\n  First Alpha: %.3f\n  Life: %.2f/%.2f\n  Pos: (%.0f, %.0f)\n  Color: (%.2f, %.2f, %.2f)",
				ps.Name,
				len(ps.Particles),
				p.Alpha,
				p.Life,
				p.MaxLife,
				p.Position.X,
				p.Position.Y,
				p.Color.X,
				p.Color.Y,
				p.Color.Z,
			)
			break // Only show first particle system
		}
	}

	window.debugBuffer.Fill(color.Transparent)
	ebitenutil.DebugPrint(window.debugBuffer, debugText)

	screenHeight := screen.Bounds().Dy()
	debugScale := math.Max(1.0, float64(screenHeight)/540.0)

	options := &ebiten.DrawImageOptions{}
	options.GeoM.Scale(debugScale, debugScale)
	options.GeoM.Translate(20, 20)

	screen.DrawImage(window.debugBuffer, options)
}

func (window *Window) Layout(outsideWidth, outsideHeight int) (screenWidth, screenHeight int) {
	width := window.scene.General.OrthogonalProjection.Width
	height := window.scene.General.OrthogonalProjection.Height
	if width <= 0 || height <= 0 {
		return 1920, 1080
	}
	return width, height
}
