package main

import (
	"image/color"
	"path/filepath"
	"strings"
	"time"

	"linux-wallpaperengine/internal/convert"
	"linux-wallpaperengine/internal/debug"
	"linux-wallpaperengine/internal/engine2D"
	"linux-wallpaperengine/internal/engine2D/particle"
	"linux-wallpaperengine/internal/engine2D/shader"
	"linux-wallpaperengine/internal/utils"
	"linux-wallpaperengine/internal/wallpaper"

	rl "github.com/gen2brain/raylib-go/raylib"
)

type Window struct {
	scene         wallpaper.Scene
	audioManager  *wallpaper.AudioManager
	startTime     time.Time
	lastFrameTime time.Time
	scalingMode   string

	debugOverlay *debug.DebugOverlay
	renderer     *engine2D.Renderer
}

func NewWindow(scene wallpaper.Scene, scalingMode string) *Window {
	shader.InitDefaults()

	red, green, blue := wallpaper.ParseColor(scene.General.ClearColor)
	audioManager := wallpaper.NewAudioManager()

	// Create a 1x1 White dummy texture for default masks
	img := rl.GenImageColor(1, 1, rl.White)
	dummyTex := rl.LoadTextureFromImage(img)
	rl.UnloadImage(img)

	// Create a simple mask preview shader
	maskSh := func() rl.Shader {
		defer func() {
			if r := recover(); r != nil {
				utils.Info("Mask shader compilation warning (skipping): %v", r)
			}
		}()
		return rl.LoadShaderFromMemory("", `
#version 120
uniform sampler2D texture0;
varying vec2 fragTexCoord;
void main() {
    gl_FragColor = texture2D(texture0, fragTexCoord);
}
`)
	}()

	// Parse scene resolution
	width := scene.General.OrthogonalProjection.Width
	height := scene.General.OrthogonalProjection.Height
	if width <= 0 || height <= 0 {
		width, height = 1280, 720
	}

	window := &Window{
		scene:         scene,
		audioManager:  audioManager,
		startTime:     time.Now(),
		lastFrameTime: time.Now(),
		scalingMode:   scalingMode,
		debugOverlay:  debug.NewDebugOverlay(),
		renderer: &engine2D.Renderer{
			RenderObjects: make([]engine2D.RenderObject, 0, len(scene.Objects)),
			SceneWidth:    width,
			SceneHeight:   height,
			BgColor:       color.RGBA{uint8(red * 255), uint8(green * 255), uint8(blue * 255), 255},
			DummyTexture:  &dummyTex,
			MaskShader:    maskSh,
			StartTime:     time.Now(),
		},
	}

	for i := range scene.Objects {
		object := &scene.Objects[i]

		if object.Particle != "" {
			object.Origin.Y = float64(height) - object.Origin.Y
		}

		if len(object.Sound) > 0 {
			window.audioManager.Play(object)
		}

		utils.Debug("Adding object: %s", object.Name)

		// Check for Model config (Puppet/Autosize)
		var autoSize bool
		var puppetPath string
		var cropOffset wallpaper.Vec2
		modelPath := object.Model
		if modelPath == "" && strings.HasSuffix(object.Image, ".json") {
			modelPath = object.Image
		}

		if modelPath != "" && strings.HasSuffix(modelPath, ".json") {
			if modelConfig, err := LoadModelConfig(modelPath); err == nil {
				if modelConfig.Puppet != "" {
					utils.Info("Object %s uses Puppet Warp (static rendering only): %s", object.Name, modelConfig.Puppet)
					puppetPath = modelConfig.Puppet
				}
				if modelConfig.Autosize {
					autoSize = true
				}
				cropOffset = modelConfig.Cropoffset
			}
		}

		texturePath := resolveTexturePath(object)
		var image *rl.Texture2D
		var renderTexture *rl.RenderTexture2D

		if texturePath != "" {
			var err error
			image, err = convert.LoadTextureNative(texturePath)
			if err != nil {
				utils.Error("Failed to load texture for object %s from %s: %v", object.Name, texturePath, err)
			} else if autoSize {
				object.Size.X = float64(image.Width)
				object.Size.Y = float64(image.Height)
			}
		} else if object.Image != "" {
			utils.Error("Could not resolve texture path for object %s (Image: %s)", object.Name, object.Image)
		}

		if image == nil && object.GetText() != "" {
			rt := rl.LoadRenderTexture(int32(object.Size.X), int32(object.Size.Y))
			renderTexture = &rt
		}

		var ps *particle.ParticleSystem
		if object.Particle != "" {
			ps = loadParticleSystem(object.Name, object.Particle, object.InstanceOverride)
		}

		var loadedEffects []shader.LoadedEffect
		for j := range object.Effects {
			loadedEffects = append(loadedEffects, shader.LoadEffect(&object.Effects[j]))
		}

		window.renderer.RenderObjects = append(window.renderer.RenderObjects, engine2D.RenderObject{
			Object:         object,
			Image:          image,
			RenderTexture:  renderTexture,
			ParticleSystem: ps,
			Effects:        loadedEffects,
			Cropoffset:     cropOffset,
		})

		// Load MDL mesh if available
		if autoSize && puppetPath != "" {
			mdlFullPath := filepath.Join("tmp", puppetPath)
			utils.Debug("Attempting to load MDL for object '%s' from: %s", object.Name, mdlFullPath)
			if mesh, err := convert.LoadMDL(mdlFullPath); err == nil {
				window.renderer.RenderObjects[len(window.renderer.RenderObjects)-1].Mesh = mesh
				utils.Debug("Successfully attached mesh to object '%s'", object.Name)
			} else {
				utils.Error("Failed to load MDL for object '%s': %v", object.Name, err)
			}
		}
	}

	return window
}

func (window *Window) Run() {
	rl.SetTargetFPS(60)

	for !rl.WindowShouldClose() {
		window.Update()

		rl.BeginDrawing()
		window.Draw()
		rl.EndDrawing()
	}
}

func (window *Window) Update() {
	currentTime := time.Now()
	deltaTime := currentTime.Sub(window.lastFrameTime).Seconds()
	window.lastFrameTime = currentTime

	// Update renderer viewport and mouse
	screenWidth := rl.GetScreenWidth()
	screenHeight := rl.GetScreenHeight()
	window.renderer.UpdateViewport(screenWidth, screenHeight, window.scalingMode)
	mPos := rl.GetMousePosition()
	window.renderer.UpdateMouse(float64(mPos.X), float64(mPos.Y))

	// Update renderer objects and effects
	totalTime := time.Since(window.startTime).Seconds()
	window.renderer.Update(deltaTime, totalTime, &window.scene)

	// Global input handling
	window.audioManager.Update()
	if rl.IsKeyPressed(rl.KeyF8) {
		utils.ShowDebugUI = !utils.ShowDebugUI
	}

	if utils.ShowDebugUI {
		window.debugOverlay.Update()
	}
}

func (window *Window) Draw() {
	window.renderer.Render()

	if utils.ShowDebugUI {
		window.debugOverlay.Draw(window.renderer.RenderObjects, window.renderer.SceneWidth, window.renderer.SceneHeight, window.renderer.RenderScale, window.renderer.SceneOffsetX, window.renderer.SceneOffsetY, window.scalingMode)
	}
}
