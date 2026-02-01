package engine2D

import (
	"image/color"
	"math"
	"time"

	"linux-wallpaperengine/internal/engine2D/particle"
	"linux-wallpaperengine/internal/engine2D/shader"
	"linux-wallpaperengine/internal/utils"
	"linux-wallpaperengine/internal/wallpaper"

	rl "github.com/gen2brain/raylib-go/raylib"
)

// EnableCropOffset controls whether to apply cropoffset to object positioning
var EnableCropOffset = false

// UpdateViewport calculates and updates render scale and scene offsets based on window size.
func (r *Renderer) UpdateViewport(screenWidth, screenHeight int, scalingMode string) {
	scaleW := float64(screenWidth) / float64(r.SceneWidth)
	scaleH := float64(screenHeight) / float64(r.SceneHeight)

	if scalingMode == "fit" {
		r.RenderScale = math.Min(scaleW, scaleH)
	} else {
		r.RenderScale = math.Max(scaleW, scaleH)
	}

	r.SceneOffsetX = (float64(screenWidth) - float64(r.SceneWidth)*r.RenderScale) / 2
	r.SceneOffsetY = (float64(screenHeight) - float64(r.SceneHeight)*r.RenderScale) / 2
}

// UpdateMouse updates mouse position in normalized scene coordinates (-1 to 1).
func (r *Renderer) UpdateMouse(screenMouseX, screenMouseY float64) {
	relMouseX := (screenMouseX - r.SceneOffsetX) / r.RenderScale
	relMouseY := (screenMouseY - r.SceneOffsetY) / r.RenderScale

	r.MouseX = (relMouseX / float64(r.SceneWidth) * 2) - 1.0
	r.MouseY = (relMouseY / float64(r.SceneHeight) * 2) - 1.0
}

// Update processes render objects, particle systems, and effects.
func (r *Renderer) Update(deltaTime, totalTime float64, scene *wallpaper.Scene) {
	// Update particle systems for each render object
	for _, renderObject := range r.RenderObjects {
		if !renderObject.Object.Visible.GetBool() {
			continue
		}
		if renderObject.ParticleSystem != nil {
			// Pass mouse position to particle system for control points
			renderObject.ParticleSystem.SetMousePosition(r.MouseX, r.MouseY)
			renderObject.ParticleSystem.Update(deltaTime)
		}
	}

	// Prepare update objects and offsets for effects
	updateObjects := make([]wallpaper.Object, len(r.RenderObjects))
	updateOffsets := make([]wallpaper.Vec2, len(r.RenderObjects))

	for i := range r.RenderObjects {
		updateObjects[i] = *r.RenderObjects[i].Object
		updateOffsets[i] = wallpaper.Vec2{X: 0, Y: 0}
	}

	// Apply scene effects
	if scene.General.CameraParallax {
		UpdateParallax(updateObjects, updateOffsets, r.MouseX, r.MouseY, scene.General.CameraParallaxAmount)
	}
	UpdateClock(updateObjects, updateOffsets)
	UpdateShake(updateObjects, updateOffsets, totalTime)

	// Update render objects with modified data
	for i := range r.RenderObjects {
		*r.RenderObjects[i].Object = updateObjects[i]
		r.RenderObjects[i].Offset = updateOffsets[i]

		if r.RenderObjects[i].RenderTexture != nil && r.RenderObjects[i].Object.GetText() != "" {
			RenderText(r.RenderObjects[i].Object, r.RenderObjects[i].RenderTexture)
		}
	}
}

// Render draws all render objects with effects and shaders.
func (r *Renderer) Render() {
	rl.ClearBackground(rl.Black)

	totalTime := time.Since(r.StartTime).Seconds()

	sceneRectX := int32(r.SceneOffsetX)
	sceneRectY := int32(r.SceneOffsetY)
	sceneRectW := int32(float64(r.SceneWidth) * r.RenderScale)
	sceneRectH := int32(float64(r.SceneHeight) * r.RenderScale)

	rl.BeginScissorMode(sceneRectX, sceneRectY, sceneRectW, sceneRectH)
	rl.ClearBackground(rl.NewColor(r.BgColor.R, r.BgColor.G, r.BgColor.B, 255))

	for i := range r.RenderObjects {
		ro := &r.RenderObjects[i]
		if !ro.Object.Visible.GetBool() {
			continue
		}

		r.renderObject(ro, totalTime, sceneRectX, sceneRectY, sceneRectW, sceneRectH)
	}

	rl.EndScissorMode()
}

func (r *Renderer) renderObject(ro *RenderObject, totalTime float64, sceneRectX, sceneRectY, sceneRectW, sceneRectH int32) {
	var texture *rl.Texture2D
	isRenderTexture := false

	if ro.Image != nil {
		texture = ro.Image
	} else if ro.RenderTexture != nil {
		texture = &ro.RenderTexture.Texture
		isRenderTexture = true
	}

	// Draw particles first (before texture-based rendering)
	if ro.ParticleSystem != nil {
		cropX := 0.0
		cropY := 0.0
		if EnableCropOffset {
			cropX = ro.Cropoffset.X
			cropY = ro.Cropoffset.Y
		}
		scaledX := r.SceneOffsetX + (ro.Object.Origin.X+ro.Offset.X-cropX)*r.RenderScale
		scaledY := r.SceneOffsetY + (ro.Object.Origin.Y+ro.Offset.Y-cropY)*r.RenderScale

		margin := 2000.0 * r.RenderScale
		if scaledX+margin >= 0 && scaledX-margin <= float64(rl.GetScreenWidth()) &&
			scaledY+margin >= 0 && scaledY-margin <= float64(rl.GetScreenHeight()) {
			scaledScale := wallpaper.Vec3{
				X: ro.Object.Scale.X * r.RenderScale,
				Y: ro.Object.Scale.Y * r.RenderScale,
				Z: ro.Object.Scale.Z * r.RenderScale,
			}
			ro.ParticleSystem.Draw(scaledX, scaledY, scaledScale)
		}
	}

	if texture == nil {
		return
	}

	alpha := ro.Object.Alpha.GetFloat()
	tintColor := color.RGBA{255, 255, 255, 255}
	particle.ApplyEffects(ro.Object, &alpha, &tintColor)

	if alpha <= 0 {
		return
	}

	targetWidth, targetHeight := ro.Object.Size.X, ro.Object.Size.Y
	if targetWidth <= 0 || targetHeight <= 0 {
		return
	}

	imageWidth := float32(texture.Width)
	imageHeight := float32(texture.Height)

	sourceRec := rl.NewRectangle(0, 0, imageWidth, imageHeight)
	if isRenderTexture {
		sourceRec.Height = -imageHeight
	}

	finalScaleX := (targetWidth / float64(imageWidth)) * ro.Object.Scale.X * r.RenderScale
	finalScaleY := (targetHeight / float64(imageHeight)) * ro.Object.Scale.Y * r.RenderScale

	cropX := 0.0
	cropY := 0.0
	if EnableCropOffset {
		cropX = ro.Cropoffset.X
		cropY = ro.Cropoffset.Y
	}
	scaledOriginX := r.SceneOffsetX + (ro.Object.Origin.X+ro.Offset.X-cropX)*r.RenderScale
	scaledOriginY := r.SceneOffsetY + (ro.Object.Origin.Y+ro.Offset.Y-cropY)*r.RenderScale

	destWidth := float64(imageWidth) * finalScaleX
	destHeight := float64(imageHeight) * finalScaleY

	halfWidth := math.Abs(destWidth) / 2
	halfHeight := math.Abs(destHeight) / 2
	radius := math.Sqrt(halfWidth*halfWidth + halfHeight*halfHeight)

	if scaledOriginX+radius < 0 || scaledOriginX-radius > float64(rl.GetScreenWidth()) ||
		scaledOriginY+radius < 0 || scaledOriginY-radius > float64(rl.GetScreenHeight()) {
		return
	}

	destRec := rl.NewRectangle(
		float32(scaledOriginX),
		float32(scaledOriginY),
		float32(math.Abs(destWidth)),
		float32(math.Abs(destHeight)),
	)

	origin := rl.NewVector2(float32(math.Abs(destWidth))/2, float32(math.Abs(destHeight))/2)
	rotation := float32(ro.Object.Angles.Z)
	rlTint := rl.NewColor(
		tintColor.R,
		tintColor.G,
		tintColor.B,
		uint8(float32(255)*float32(alpha)),
	)

	hasEffects := false
	for _, le := range ro.Effects {
		if le.Config.Visible.GetBool() && len(le.Passes) > 0 {
			hasEffects = true
			break
		}
	}

	currentTexture := texture
	isCurrentRenderTexture := isRenderTexture

	if hasEffects {
		r.applyEffects(ro, currentTexture, isCurrentRenderTexture, totalTime, sceneRectX, sceneRectY, sceneRectW, sceneRectH)
		currentTexture = &ro.PingPong[0].Texture
		isCurrentRenderTexture = true
	}

	imageWidth = float32(currentTexture.Width)
	imageHeight = float32(currentTexture.Height)

	sourceRec = rl.NewRectangle(0, 0, imageWidth, imageHeight)
	if isCurrentRenderTexture {
		sourceRec.Height = -imageHeight
	}

	finalScaleX = (targetWidth / float64(imageWidth)) * ro.Object.Scale.X * r.RenderScale
	finalScaleY = (targetHeight / float64(imageHeight)) * ro.Object.Scale.Y * r.RenderScale

	cropX = 0.0
	cropY = 0.0
	if EnableCropOffset {
		cropX = ro.Cropoffset.X
		cropY = ro.Cropoffset.Y
	}
	scaledOriginX = r.SceneOffsetX + (ro.Object.Origin.X+ro.Offset.X-cropX)*r.RenderScale
	scaledOriginY = r.SceneOffsetY + (ro.Object.Origin.Y+ro.Offset.Y-cropY)*r.RenderScale

	destWidth = float64(imageWidth) * finalScaleX
	destHeight = float64(imageHeight) * finalScaleY

	destRec = rl.NewRectangle(
		float32(scaledOriginX),
		float32(scaledOriginY),
		float32(math.Abs(destWidth)),
		float32(math.Abs(destHeight)),
	)

	origin = rl.NewVector2(float32(math.Abs(destWidth))/2, float32(math.Abs(destHeight))/2)

	rl.DrawTexturePro(*currentTexture, sourceRec, destRec, origin, rotation, rlTint)

	if utils.DebugMode && ro.Mesh != nil {
		rl.DrawRectangleLinesEx(destRec, 2, rl.Red)
		for i := 0; i < len(ro.Mesh.Indices)-2; i += 3 {
			idx1 := ro.Mesh.Indices[i]
			idx2 := ro.Mesh.Indices[i+1]
			idx3 := ro.Mesh.Indices[i+2]

			if int(idx1) < len(ro.Mesh.Vertices) &&
				int(idx2) < len(ro.Mesh.Vertices) &&
				int(idx3) < len(ro.Mesh.Vertices) {
				v1 := ro.Mesh.Vertices[idx1]
				v2 := ro.Mesh.Vertices[idx2]
				v3 := ro.Mesh.Vertices[idx3]

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

func (r *Renderer) applyEffects(ro *RenderObject, srcTexture *rl.Texture2D, isSrcRenderTexture bool, totalTime float64, sceneRectX, sceneRectY, sceneRectW, sceneRectH int32) {
	w := int32(srcTexture.Width)
	h := int32(srcTexture.Height)

	if ro.PingPong[0] == nil ||
		ro.PingPong[0].Texture.Width != w ||
		ro.PingPong[0].Texture.Height != h {

		if ro.PingPong[0] != nil {
			rl.UnloadRenderTexture(*ro.PingPong[0])
			rl.UnloadRenderTexture(*ro.PingPong[1])
		}

		rt1 := rl.LoadRenderTexture(w, h)
		rt2 := rl.LoadRenderTexture(w, h)
		rl.SetTextureWrap(rt1.Texture, rl.TextureWrapRepeat)
		rl.SetTextureWrap(rt2.Texture, rl.TextureWrapRepeat)

		ro.PingPong[0] = &rt1
		ro.PingPong[1] = &rt2
	}

	currentTexture := srcTexture
	isCurrentRenderTexture := isSrcRenderTexture
	pingPongIdx := 0

	for _, le := range ro.Effects {
		if !le.Config.Visible.GetBool() || len(le.Passes) == 0 {
			continue
		}

		for passIdx := range le.Passes {
			activePass := &le.Passes[passIdx]
			targetRT := ro.PingPong[pingPongIdx]
			activeShader := activePass.Shader

			if activeShader.ID == 0 {
				continue
			}

			activeTextures := activePass.Textures
			activeMainTex := currentTexture
			shouldFlip := isCurrentRenderTexture

			if len(activeTextures) > 0 && activeTextures[0] != nil {
				activeMainTex = activeTextures[0]
				shouldFlip = false
			}

			if le.ShowMask {
				activeShader = r.MaskShader

				if len(activeTextures) > 1 && activeTextures[1] != nil {
					activeMainTex = activeTextures[1]
					shouldFlip = false
				} else if len(activeTextures) > 0 && activeTextures[0] != nil {
					activeMainTex = activeTextures[0]
					shouldFlip = false
				} else {
					activeMainTex = r.DummyTexture
					shouldFlip = false
				}
			}

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
				globalState := shader.GlobalState{
					Time:      totalTime,
					MouseX:    r.MouseX,
					MouseY:    r.MouseY,
					ParallaxX: r.MouseX,
					ParallaxY: r.MouseY,
				}

				shader.ApplyPass(activePass, globalState, activeMainTex)
			}

			srcRec := rl.NewRectangle(0, 0, float32(activeMainTex.Width), float32(activeMainTex.Height))
			dstRec := rl.NewRectangle(0, 0, float32(targetRT.Texture.Width), float32(targetRT.Texture.Height))

			if shouldFlip {
				srcRec.Height = -srcRec.Height
			}

			rl.DrawTexturePro(*activeMainTex, srcRec, dstRec, rl.NewVector2(0, 0), 0, rl.White)

			rl.EndShaderMode()
			rl.EndTextureMode()

			rl.BeginScissorMode(sceneRectX, sceneRectY, sceneRectW, sceneRectH)

			currentTexture = &targetRT.Texture
			isCurrentRenderTexture = true
			pingPongIdx = 1 - pingPongIdx
		}
	}
}

func mapCoord(nx, ny float32, destRec rl.Rectangle, rotation float32) rl.Vector2 {
	scaleX := destRec.Width / 8.5
	scaleY := destRec.Height / 8.5

	lx := nx * float32(scaleX)
	ly := ny * float32(scaleY)

	rad := rotation * math.Pi / 180.0
	c := float32(math.Cos(float64(rad)))
	s := float32(math.Sin(float64(rad)))

	rx := lx*c - ly*s
	ry := lx*s + ly*c

	return rl.NewVector2(destRec.X+rx, destRec.Y+ry)
}
