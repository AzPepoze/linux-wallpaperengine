package main

import (
	"image/color"
	"math"
	"path/filepath"
	"strings"
	"time"

	"linux-wallpaperengine/src/convert"
	"linux-wallpaperengine/src/debug"
	"linux-wallpaperengine/src/types"
	"linux-wallpaperengine/src/utils"
	"linux-wallpaperengine/src/wallpaper"
	"linux-wallpaperengine/src/wallpaper/feature"

	rl "github.com/gen2brain/raylib-go/raylib"
)

type Window struct {
	scene          wallpaper.Scene
	bgColor        color.RGBA
	renderObjects  []types.RenderObject
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
}

func NewWindow(scene wallpaper.Scene, scalingMode string) *Window {
	red, green, blue := wallpaper.ParseColor(scene.General.ClearColor)
	audioManager := wallpaper.NewAudioManager()

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
		renderObjects: make([]types.RenderObject, 0, len(scene.Objects)),
		updateObjects: make([]wallpaper.Object, len(scene.Objects)),
		updateOffsets: make([]wallpaper.Vec2, len(scene.Objects)),
		debugOverlay:  debug.NewDebugOverlay(),
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

		var ps *feature.ParticleSystem
		if object.Particle != "" {
			ps = loadParticleSystem(object.Name, object.Particle, object.InstanceOverride)
		}

		window.renderObjects = append(window.renderObjects, types.RenderObject{
			Object:         object,
			Image:          image,
			RenderTexture:  renderTexture,
			ParticleSystem: ps,
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
		feature.UpdateParallax(window.updateObjects, window.updateOffsets, window.mouseX, window.mouseY, window.scene.General.CameraParallaxAmount)
	}
	feature.UpdateClock(window.updateObjects, window.updateOffsets)
	feature.UpdateShake(window.updateObjects, window.updateOffsets, totalTime)

	window.audioManager.Update()

	if rl.IsKeyPressed(rl.KeyF8) {
		utils.ShowDebugUI = !utils.ShowDebugUI
	}

	for i := range window.renderObjects {
		*window.renderObjects[i].Object = window.updateObjects[i]
		window.renderObjects[i].Offset = window.updateOffsets[i]

		if window.renderObjects[i].RenderTexture != nil && window.renderObjects[i].Object.GetText() != "" {
			feature.RenderText(window.renderObjects[i].Object, window.renderObjects[i].RenderTexture)
		}
	}

	if utils.ShowDebugUI {
		window.debugOverlay.Update()
	}
}

func mapCoord(nx, ny float32, destRec rl.Rectangle, origin rl.Vector2, rotation float32) rl.Vector2 {
	// nx, ny are likely in range [-4.25, 4.25]
	// Assume (0,0) is center of object
	
	// Map to screen
	// We guess the scale factor. 
	// If the object range is ~8 units wide, and we want it to fit destRec.Width...
	scaleX := destRec.Width / 8.5
	scaleY := destRec.Height / 8.5
	
	lx := nx * float32(scaleX)
	ly := ny * float32(scaleY)
	
	// Rotate around Pivot (which we assume is at 0,0 of the mesh space)
	// But `origin` parameter is the pivot offset relative to top-left of destRec.
	// Actually, `destRec` is centered at `scaledOrigin` in `Draw`?
	// No:
	// destRec := rl.NewRectangle(scaledOriginX, scaledOriginY, ...)
	// origin := rl.NewVector2(width/2, height/2)
	// DrawTexturePro rotates around `origin`.
	
	// So (0,0) in mesh space corresponds to `destRec.X + origin.X` ?
	// Yes, if the mesh is centered.
	
	rad := rotation * math.Pi / 180.0
	c := float32(math.Cos(float64(rad)))
	s := float32(math.Sin(float64(rad)))
	
	rx := lx*c - ly*s
	ry := lx*s + ly*c
	
	// My `Draw` code:
	// destRec X/Y is `scaledOriginX/Y`.
	// origin is `Width/2, Height/2`.
	// So `scaledOriginX/Y` is the center of the image on screen?
	// Let's check `Draw`:
	// scaledOriginX = sceneOffsetX + (Obj.Origin.X + Offset.X) * scale
	// This looks like the position of the object's origin.
	// And we set destRec.X = scaledOriginX.
	// And we set origin = Width/2, Height/2.
	// So we are saying: "Draw the texture so that its center (Width/2, Height/2) aligns with scaledOriginX".
	
	// So `destRec.X` IS the screen coordinate of the object's center.
	
	return rl.NewVector2(destRec.X + rx, destRec.Y + ry)
}

func (window *Window) Draw() {
	rl.ClearBackground(rl.Black)

	// Clip rendering to scene area to prevent overdraw in 'fit' mode or when objects move out of bounds
	sceneRectX := int32(window.sceneOffsetX)
	sceneRectY := int32(window.sceneOffsetY)
	sceneRectW := int32(float64(window.sceneWidth) * window.renderScale)
	sceneRectH := int32(float64(window.sceneHeight) * window.renderScale)

	rl.BeginScissorMode(sceneRectX, sceneRectY, sceneRectW, sceneRectH)
	rl.ClearBackground(rl.NewColor(window.bgColor.R, window.bgColor.G, window.bgColor.B, 255))

	for _, renderObject := range window.renderObjects {
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
			feature.ApplyEffects(renderObject.Object, &alpha, &tintColor)

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
					scaledOriginX := window.sceneOffsetX + (renderObject.Object.Origin.X+renderObject.Offset.X)*window.renderScale
					scaledOriginY := window.sceneOffsetY + (renderObject.Object.Origin.Y+renderObject.Offset.Y)*window.renderScale

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

					rl.DrawTexturePro(*texture, sourceRec, destRec, origin, rotation, rlTint)

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
							   	p1 := mapCoord(v1.PosX, v1.PosY, destRec, origin, rotation)
							   	p2 := mapCoord(v2.PosX, v2.PosY, destRec, origin, rotation)
							   	p3 := mapCoord(v3.PosX, v3.PosY, destRec, origin, rotation)
							   	
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
			scaledX := window.sceneOffsetX + (renderObject.Object.Origin.X+renderObject.Offset.X)*window.renderScale
			scaledY := window.sceneOffsetY + (renderObject.Object.Origin.Y+renderObject.Offset.Y)*window.renderScale

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
