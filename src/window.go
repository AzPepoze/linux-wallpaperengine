package main

import (
	"image/color"
	"math"
	"time"

	"linux-wallpaperengine/src/convert"
	"linux-wallpaperengine/src/utils"
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

type Window struct {
	scene          wallpaper.Scene
	bgColor        color.RGBA
	renderObjects  []RenderObject
	audioManager   *wallpaper.AudioManager
	mouseX, mouseY float64
	startTime      time.Time
	lastFrameTime  time.Time
	renderScale    float64
	sceneWidth     int
	sceneHeight    int
	scaleLogged    bool

	updateObjects []wallpaper.Object
	updateOffsets []wallpaper.Vec2
	debugOverlay  *DebugOverlay
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
		renderScale:   1.0,
		renderObjects: make([]RenderObject, 0, len(scene.Objects)),
		updateObjects: make([]wallpaper.Object, len(scene.Objects)),
		updateOffsets: make([]wallpaper.Vec2, len(scene.Objects)),
		debugOverlay:  NewDebugOverlay(),
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
				utils.Error("Failed to load texture for object %s from %s: %v", object.Name, texturePath, err)
			}
		} else if object.Image != "" {
			utils.Error("Could not resolve texture path for object %s (Image: %s)", object.Name, object.Image)
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

	if utils.DebugMode {
		window.debugOverlay.Update()
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

					finalScaleX := (targetWidth / float64(imageWidth)) * renderObject.Scale.X * window.renderScale
					finalScaleY := (targetHeight / float64(imageHeight)) * renderObject.Scale.Y * window.renderScale
					options.GeoM.Scale(finalScaleX, finalScaleY)

					if renderObject.Angles.Z != 0 {
						radians := renderObject.Angles.Z * (math.Pi / 180.0)
						options.GeoM.Rotate(radians)
					}

					scaledOriginX := (renderObject.Origin.X + renderObject.Offset.X) * window.renderScale
					scaledOriginY := (renderObject.Origin.Y + renderObject.Offset.Y) * window.renderScale
					options.GeoM.Translate(scaledOriginX, scaledOriginY)
					screen.DrawImage(renderObject.Image, options)
				}
			}
		}

		if renderObject.ParticleSystem != nil {
			scaledX := (renderObject.Origin.X + renderObject.Offset.X) * window.renderScale
			scaledY := (renderObject.Origin.Y + renderObject.Offset.Y) * window.renderScale
			scaledScale := wallpaper.Vec3{
				X: renderObject.Scale.X * window.renderScale,
				Y: renderObject.Scale.Y * window.renderScale,
				Z: renderObject.Scale.Z * window.renderScale,
			}
			renderObject.ParticleSystem.Draw(screen, scaledX, scaledY, scaledScale)
		}
	}

	if utils.DebugMode {
		window.debugOverlay.Draw(screen, window.renderObjects, window.sceneWidth, window.sceneHeight, window.renderScale)
	}
}

func (window *Window) Layout(outsideWidth, outsideHeight int) (screenWidth, screenHeight int) {
	// Get the scene's target resolution
	width := window.scene.General.OrthogonalProjection.Width
	height := window.scene.General.OrthogonalProjection.Height
	if width <= 0 || height <= 0 {
		width, height = 1920, 1080
	}

	// Store original scene resolution
	if window.sceneWidth == 0 {
		window.sceneWidth = width
		window.sceneHeight = height
	}

	// Get monitor size
	monitor := ebiten.Monitor()
	monitorW, monitorH := monitor.Size()

	// Calculate scale factor
	window.renderScale = 1.0

	// If scene resolution is higher than monitor, scale down to monitor resolution
	// This prevents rendering at 4K when monitor is 1080p
	if width > monitorW || height > monitorH {
		scaleW := float64(monitorW) / float64(width)
		scaleH := float64(monitorH) / float64(height)
		window.renderScale = math.Min(scaleW, scaleH)

		scaledWidth := int(float64(width) * window.renderScale)
		scaledHeight := int(float64(height) * window.renderScale)

		if !window.scaleLogged {
			utils.Info("Scene resolution: %dx%d, rendering at: %dx%d (monitor: %dx%d, scale: %.2f)",
				width, height, scaledWidth, scaledHeight, monitorW, monitorH, window.renderScale)
			window.scaleLogged = true
		}

		width = scaledWidth
		height = scaledHeight
	}

	return width, height
}
