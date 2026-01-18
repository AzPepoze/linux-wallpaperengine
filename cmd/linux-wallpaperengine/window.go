package main

import (
	"image/color"
	"math"
	"path/filepath"
	"strings"
	"time"

	"linux-wallpaperengine/internal/convert"
	"linux-wallpaperengine/internal/debug"
	"linux-wallpaperengine/internal/engine2D"
	"linux-wallpaperengine/internal/utils"
	"linux-wallpaperengine/internal/wallpaper"

	rl "github.com/gen2brain/raylib-go/raylib"
)

type Window struct {
	scene          wallpaper.Scene
	bgColor        color.RGBA
	renderObjects  []engine2D.RenderObject
	audioManager   *wallpaper.AudioManager
	mouseX, mouseY float64
	startTime      time.Time
	lastFrameTime  time.Time
	renderScale    float64
	sceneWidth     int
	sceneHeight    int
	sceneOffsetX   float64
	sceneOffsetY   float64
	scalingMode    string

	updateObjects []wallpaper.Object
	updateOffsets []wallpaper.Vec2
	debugOverlay  *debug.DebugOverlay
	dummyTexture  rl.Texture2D
	maskShader    rl.Shader
}

func NewWindow(scene wallpaper.Scene, scalingMode string) *Window {
	engine2D.InitDefaults()

	red, green, blue := wallpaper.ParseColor(scene.General.ClearColor)
	audioManager := wallpaper.NewAudioManager()

	// Create a 1x1 White dummy texture for default masks
	img := rl.GenImageColor(1, 1, rl.White)
	dummyTex := rl.LoadTextureFromImage(img)
	rl.UnloadImage(img)

	// Create a simple mask preview shader
	maskSh := rl.LoadShaderFromMemory("", `
#version 120
uniform sampler2D texture0;
varying vec2 fragTexCoord;
void main() {
    gl_FragColor = texture2D(texture0, fragTexCoord);
}
`)

	// Parse scene resolution
	width := scene.General.OrthogonalProjection.Width
	height := scene.General.OrthogonalProjection.Height
	if width <= 0 || height <= 0 {
		width, height = 1280, 720
	}

	window := &Window{
		scene:         scene,
		bgColor:       color.RGBA{uint8(red * 255), uint8(green * 255), uint8(blue * 255), 255},
		audioManager:  audioManager,
		startTime:     time.Now(),
		lastFrameTime: time.Now(),
		renderScale:   1.0,
		sceneWidth:    width,
		sceneHeight:   height,
		scalingMode:   scalingMode,
		renderObjects: make([]engine2D.RenderObject, 0, len(scene.Objects)),
		updateObjects: make([]wallpaper.Object, len(scene.Objects)),
		updateOffsets: make([]wallpaper.Vec2, len(scene.Objects)),
		debugOverlay:  debug.NewDebugOverlay(),
		dummyTexture:  dummyTex,
		maskShader:    maskSh,
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

		var ps *engine2D.ParticleSystem
		if object.Particle != "" {
			ps = loadParticleSystem(object.Name, object.Particle, object.InstanceOverride)
		}

		var loadedEffects []engine2D.LoadedEffect
		for j := range object.Effects {
			loadedEffects = append(loadedEffects, engine2D.LoadEffect(&object.Effects[j]))
		}

		window.renderObjects = append(window.renderObjects, engine2D.RenderObject{
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
				window.renderObjects[len(window.renderObjects)-1].Mesh = mesh
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

	// Calculate render scale based on window size
	screenWidth := rl.GetScreenWidth()
	screenHeight := rl.GetScreenHeight()

	scaleW := float64(screenWidth) / float64(window.sceneWidth)
	scaleH := float64(screenHeight) / float64(window.sceneHeight)

	if window.scalingMode == "fit" {
		window.renderScale = math.Min(scaleW, scaleH)
	} else {
		window.renderScale = math.Max(scaleW, scaleH)
	}

	window.sceneOffsetX = (float64(screenWidth) - float64(window.sceneWidth)*window.renderScale) / 2
	window.sceneOffsetY = (float64(screenHeight) - float64(window.sceneHeight)*window.renderScale) / 2

	// Update Mouse using Raylib's built-in function
	mPos := rl.GetMousePosition()
	mouseX, mouseY := float64(mPos.X), float64(mPos.Y)

	// Normalized mouse coordinates (-1 to 1) relative to scene
	relMouseX := (mouseX - window.sceneOffsetX) / window.renderScale
	relMouseY := (mouseY - window.sceneOffsetY) / window.renderScale

	window.mouseX = (relMouseX / float64(window.sceneWidth) * 2) - 1.0
	window.mouseY = (relMouseY / float64(window.sceneHeight) * 2) - 1.0

	for i, renderObject := range window.renderObjects {
		window.updateObjects[i] = *renderObject.Object
		window.updateOffsets[i] = wallpaper.Vec2{X: 0, Y: 0}

		if !renderObject.Object.Visible.GetBool() {
			continue
		}
		if renderObject.ParticleSystem != nil {
			renderObject.ParticleSystem.Update(deltaTime)
		}
	}

	totalTime := time.Since(window.startTime).Seconds()

	if window.scene.General.CameraParallax {
		engine2D.UpdateParallax(window.updateObjects, window.updateOffsets, window.mouseX, window.mouseY, window.scene.General.CameraParallaxAmount)
	}
	engine2D.UpdateClock(window.updateObjects, window.updateOffsets)
	engine2D.UpdateShake(window.updateObjects, window.updateOffsets, totalTime)

	window.audioManager.Update()

	if rl.IsKeyPressed(rl.KeyF8) {
		utils.ShowDebugUI = !utils.ShowDebugUI
	}

	for i := range window.renderObjects {
		*window.renderObjects[i].Object = window.updateObjects[i]
		window.renderObjects[i].Offset = window.updateOffsets[i]

		if window.renderObjects[i].RenderTexture != nil && window.renderObjects[i].Object.GetText() != "" {
			engine2D.RenderText(window.renderObjects[i].Object, window.renderObjects[i].RenderTexture)
		}
	}

	if utils.ShowDebugUI {
		window.debugOverlay.Update()
	}
}

func mapCoord(nx, ny float32, destRec rl.Rectangle, rotation float32) rl.Vector2 {
	scaleX := destRec.Width / 8.5
	scaleY := destRec.Height / 8.5

	lx := nx * float32(scaleX)
	ly := ny * float32(scaleY)

	// Apply rotation
	rad := rotation * math.Pi / 180.0
	c := float32(math.Cos(float64(rad)))
	s := float32(math.Sin(float64(rad)))

	rx := lx*c - ly*s
	ry := lx*s + ly*c

	return rl.NewVector2(destRec.X+rx, destRec.Y+ry)
}

func (window *Window) Draw() {
	rl.ClearBackground(rl.Black)

	totalTime := time.Since(window.startTime).Seconds()

	// Clip rendering to scene area to prevent overdraw in 'fit' mode or when objects move out of bounds
	sceneRectX := int32(window.sceneOffsetX)
	sceneRectY := int32(window.sceneOffsetY)
	sceneRectW := int32(float64(window.sceneWidth) * window.renderScale)
	sceneRectH := int32(float64(window.sceneHeight) * window.renderScale)

	rl.BeginScissorMode(sceneRectX, sceneRectY, sceneRectW, sceneRectH)
	rl.ClearBackground(rl.NewColor(window.bgColor.R, window.bgColor.G, window.bgColor.B, 255))

	for i := range window.renderObjects {
		renderObject := &window.renderObjects[i]
		if !renderObject.Object.Visible.GetBool() {
			continue
		}

		var texture *rl.Texture2D
		isRenderTexture := false

		if renderObject.Image != nil {
			texture = renderObject.Image
		} else if renderObject.RenderTexture != nil {
			texture = &renderObject.RenderTexture.Texture
			isRenderTexture = true
		}

		if texture != nil {
			alpha := renderObject.Object.Alpha.GetFloat()
			tintColor := color.RGBA{255, 255, 255, 255}
			engine2D.ApplyEffects(renderObject.Object, &alpha, &tintColor)

			if alpha > 0 {
				targetWidth, targetHeight := renderObject.Object.Size.X, renderObject.Object.Size.Y
				if targetWidth > 0 && targetHeight > 0 {

					imageWidth := float32(texture.Width)
					imageHeight := float32(texture.Height)

					// Source rectangle
					sourceRec := rl.NewRectangle(0, 0, imageWidth, imageHeight)
					if isRenderTexture {
						sourceRec.Height = -imageHeight // Flip vertically for RenderTexture
					}

					// Calculate scale
					finalScaleX := (targetWidth / float64(imageWidth)) * renderObject.Object.Scale.X * window.renderScale
					finalScaleY := (targetHeight / float64(imageHeight)) * renderObject.Object.Scale.Y * window.renderScale

					// Destination rectangle (centered at Origin)
					scaledOriginX := window.sceneOffsetX + (renderObject.Object.Origin.X+renderObject.Offset.X-renderObject.Cropoffset.X)*window.renderScale
					scaledOriginY := window.sceneOffsetY + (renderObject.Object.Origin.Y+renderObject.Offset.Y-renderObject.Cropoffset.Y)*window.renderScale

					destWidth := float64(imageWidth) * finalScaleX
					destHeight := float64(imageHeight) * finalScaleY

					// Culling: check if object is outside screen
					halfWidth := math.Abs(destWidth) / 2
					halfHeight := math.Abs(destHeight) / 2
					// Using a bounding radius to account for rotation
					radius := math.Sqrt(halfWidth*halfWidth + halfHeight*halfHeight)

					if scaledOriginX+radius < 0 || scaledOriginX-radius > float64(rl.GetScreenWidth()) ||
						scaledOriginY+radius < 0 || scaledOriginY-radius > float64(rl.GetScreenHeight()) {
						continue
					}

					destRec := rl.NewRectangle(
						float32(scaledOriginX),
						float32(scaledOriginY),
						float32(math.Abs(destWidth)),
						float32(math.Abs(destHeight)),
					)

					// Origin (Pivot point relative to destination rectangle top-left)
					origin := rl.NewVector2(float32(math.Abs(destWidth))/2, float32(math.Abs(destHeight))/2)

					rotation := float32(renderObject.Object.Angles.Z) // Degrees

					rlTint := rl.NewColor(
						tintColor.R,
						tintColor.G,
						tintColor.B,
						uint8(float32(255)*float32(alpha)),
					)

					// Shader application
					// activeShader := rl.Shader{}

					// 1. Setup PingPong buffers if needed
					hasEffects := false
					for _, le := range renderObject.Effects {
						if le.Config.Visible.GetBool() && len(le.Passes) > 0 {
							hasEffects = true
							break
						}
					}

					currentTexture := texture
					isCurrentRenderTexture := isRenderTexture

					if hasEffects {
						// Init buffers if nil or size mismatch
						w := int32(texture.Width)
						h := int32(texture.Height)

						if renderObject.PingPong[0] == nil ||
							renderObject.PingPong[0].Texture.Width != w ||
							renderObject.PingPong[0].Texture.Height != h {

							// Cleanup old if exists (Raylib Go wrapper might rely on GC or explicit Unload? Explicit is safer but we'll load new for now)
							if renderObject.PingPong[0] != nil {
								rl.UnloadRenderTexture(*renderObject.PingPong[0])
								rl.UnloadRenderTexture(*renderObject.PingPong[1])
							}

							rt1 := rl.LoadRenderTexture(w, h)
							rt2 := rl.LoadRenderTexture(w, h)
							// Set Wrap to Repeat for these intermediate buffers too!
							rl.SetTextureWrap(rt1.Texture, rl.TextureWrapRepeat)
							rl.SetTextureWrap(rt2.Texture, rl.TextureWrapRepeat)

							renderObject.PingPong[0] = &rt1
							renderObject.PingPong[1] = &rt2
						}

						// 2. Ping-Pong Rendering
						pingPongIdx := 0

						for _, le := range renderObject.Effects {
							if !le.Config.Visible.GetBool() || len(le.Passes) == 0 {
								continue
							}

							activePass := &le.Passes[0]
							targetRT := renderObject.PingPong[pingPongIdx]
							activeShader := activePass.Shader

							if activeShader.ID == 0 {
								// utils.Error("Effect '%s' has invalid shader in pass, skipping effect.", le.Config.Name)
								continue
							}

							activeTextures := activePass.Textures
							activeMainTex := currentTexture

							// If the pass overrides the main texture (Texture 0), use it
							if len(activeTextures) > 0 && activeTextures[0] != nil {
								activeMainTex = activeTextures[0]
							}

							// Per-Effect Mask Visualization
							if le.ShowMask {
								activeShader = window.maskShader

								if len(activeTextures) > 1 && activeTextures[1] != nil {
									activeMainTex = activeTextures[1]
								} else if len(activeTextures) > 0 && activeTextures[0] != nil {
									activeMainTex = activeTextures[0] // Fallback
								} else {
									activeMainTex = &window.dummyTexture
								}
							}

							// Disable Scissor during off-screen rendering to avoid clipping to screen rect
							rl.EndScissorMode()

							rl.BeginTextureMode(*targetRT)
							rl.ClearBackground(rl.Blank)
							rl.BeginShaderMode(activeShader)

							if le.ShowMask {
								texture0Loc := rl.GetShaderLocation(activeShader, "texture0")
								if texture0Loc != -1 {
									rl.SetShaderValueTexture(activeShader, texture0Loc, *activeMainTex)
								}
							} else {
								globalState := engine2D.GlobalState{
									Time:      totalTime,
									MouseX:    window.mouseX,
									MouseY:    window.mouseY,
									ParallaxX: window.mouseX,
									ParallaxY: window.mouseY,
								}

								engine2D.ApplyPass(activePass, globalState, activeMainTex)
							}

							// Draw activeMainTex 1:1 onto targetRT
							srcRec := rl.NewRectangle(0, 0, float32(activeMainTex.Width), float32(activeMainTex.Height))
							if activeMainTex == currentTexture && isCurrentRenderTexture {
								srcRec.Height = -srcRec.Height
							}

							dstRec := rl.NewRectangle(0, 0, float32(targetRT.Texture.Width), float32(targetRT.Texture.Height))

							rl.DrawTexturePro(*activeMainTex, srcRec, dstRec, rl.NewVector2(0, 0), 0, rl.White)

							rl.EndShaderMode()
							rl.EndTextureMode()

							// Restore Scissor for main screen rendering
							rl.BeginScissorMode(sceneRectX, sceneRectY, sceneRectW, sceneRectH)

							// Update for next pass
							currentTexture = &targetRT.Texture
							isCurrentRenderTexture = true // Result of RT is always an RT texture
							pingPongIdx = 1 - pingPongIdx
						}
					}

					// Use currentTexture (processed) for final draw
					imageWidth = float32(currentTexture.Width)
					imageHeight = float32(currentTexture.Height)

					// Source rectangle
					sourceRec = rl.NewRectangle(0, 0, imageWidth, imageHeight)
					if isCurrentRenderTexture {
						sourceRec.Height = -imageHeight // Flip vertically for RenderTexture
					}

					// Calculate scale
					finalScaleX = (targetWidth / float64(imageWidth)) * renderObject.Object.Scale.X * window.renderScale
					finalScaleY = (targetHeight / float64(imageHeight)) * renderObject.Object.Scale.Y * window.renderScale

					// Destination rectangle (centered at Origin)
					scaledOriginX = window.sceneOffsetX + (renderObject.Object.Origin.X+renderObject.Offset.X-renderObject.Cropoffset.X)*window.renderScale
					scaledOriginY = window.sceneOffsetY + (renderObject.Object.Origin.Y+renderObject.Offset.Y-renderObject.Cropoffset.Y)*window.renderScale

					destWidth = float64(imageWidth) * finalScaleX
					destHeight = float64(imageHeight) * finalScaleY

					// Re-calculate destRec and Origin
					destRec = rl.NewRectangle(
						float32(scaledOriginX),
						float32(scaledOriginY),
						float32(math.Abs(destWidth)),
						float32(math.Abs(destHeight)),
					)

					origin = rl.NewVector2(float32(math.Abs(destWidth))/2, float32(math.Abs(destHeight))/2)

					rl.DrawTexturePro(*currentTexture, sourceRec, destRec, origin, rotation, rlTint)

					// Debug: Draw Mesh Wireframe
					if utils.DebugMode && renderObject.Mesh != nil {
						rl.DrawRectangleLinesEx(destRec, 2, rl.Red)

						// Draw mesh triangles
						for i := 0; i < len(renderObject.Mesh.Indices)-2; i += 3 {
							idx1 := renderObject.Mesh.Indices[i]
							idx2 := renderObject.Mesh.Indices[i+1]
							idx3 := renderObject.Mesh.Indices[i+2]

							if int(idx1) < len(renderObject.Mesh.Vertices) &&
								int(idx2) < len(renderObject.Mesh.Vertices) &&
								int(idx3) < len(renderObject.Mesh.Vertices) {
								v1 := renderObject.Mesh.Vertices[idx1]
								v2 := renderObject.Mesh.Vertices[idx2]
								v3 := renderObject.Mesh.Vertices[idx3]

								// Map normalized coords to screen
								p1 := mapCoord(v1.PosX, v1.PosY, destRec, rotation)
								p2 := mapCoord(v2.PosX, v2.PosY, destRec, rotation)
								p3 := mapCoord(v3.PosX, v3.PosY, destRec, rotation)

								rl.DrawLineV(p1, p2, rl.Green)
								rl.DrawLineV(p2, p3, rl.Green)
								rl.DrawLineV(p3, p1, rl.Green)
							}
						}
					}
				}
			}
		}

		if renderObject.ParticleSystem != nil {
			scaledX := window.sceneOffsetX + (renderObject.Object.Origin.X+renderObject.Offset.X-renderObject.Cropoffset.X)*window.renderScale
			scaledY := window.sceneOffsetY + (renderObject.Object.Origin.Y+renderObject.Offset.Y-renderObject.Cropoffset.Y)*window.renderScale

			// scaledY -= float64(window.sceneHeight) / 2 // Move center to (0,0)
			// scaledY *= -1                              // Flip Y axis

			// Conservative culling for particles: if origin is way off screen, skip
			margin := 2000.0 * window.renderScale
			if scaledX+margin >= 0 && scaledX-margin <= float64(rl.GetScreenWidth()) &&
				scaledY+margin >= 0 && scaledY-margin <= float64(rl.GetScreenHeight()) {
				scaledScale := wallpaper.Vec3{
					X: renderObject.Object.Scale.X * window.renderScale,
					Y: renderObject.Object.Scale.Y * window.renderScale,
					Z: renderObject.Object.Scale.Z * window.renderScale,
				}
				renderObject.ParticleSystem.Draw(scaledX, scaledY, scaledScale)
			}
		}
	}

	rl.EndScissorMode()

	if utils.ShowDebugUI {
		window.debugOverlay.Draw(window.renderObjects, window.sceneWidth, window.sceneHeight, window.renderScale, window.sceneOffsetX, window.sceneOffsetY, window.scalingMode)
	}
}
