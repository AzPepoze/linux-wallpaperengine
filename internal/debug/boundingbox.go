package debug

import (
	"linux-wallpaperengine/internal/engine2D"

	rl "github.com/gen2brain/raylib-go/raylib"
)

func (d *DebugOverlay) getBoundingBoxToggleRect() rl.Rectangle {
	return rl.NewRectangle(
		10, 
		float32(d.tabHeight+5), 
		float32(d.sidebarWidth-20), 
		20,
	)
}

func (d *DebugOverlay) drawBoundingBoxToggle() {
	rect := d.getBoundingBoxToggleRect()

	boxSize := float32(d.fontHeight) * 1.2
	boxX := rect.X
	boxY := rect.Y + (rect.Height-boxSize)/2

	rl.DrawRectangleLines(int32(boxX), int32(boxY), int32(boxSize), int32(boxSize), rl.White)
	if d.ShowBoundingBoxes {
		rl.DrawRectangle(int32(boxX+2), int32(boxY+2), int32(boxSize-4), int32(boxSize-4), rl.White)
	}

	d.DrawText("Show Bounding Boxes", int32(boxX+boxSize+10), int32(boxY), int32(d.fontHeight), rl.White)
}

func (d *DebugOverlay) drawSelectedBoundingBox(obj engine2D.RenderObject, renderScale, sceneOffsetX, sceneOffsetY float64) {
	d.drawObjectBoundingBox(obj, renderScale, sceneOffsetX, sceneOffsetY, rl.NewColor(255, 255, 0, 255), rl.NewColor(255, 255, 0, 150))
}

func (d *DebugOverlay) drawSceneBoundingBoxes(renderObjects []engine2D.RenderObject, renderScale, sceneOffsetX, sceneOffsetY float64) {
	for i, obj := range renderObjects {
		if i == d.SelectedObjectIndex {
			continue
		}
		d.drawObjectBoundingBox(obj, renderScale, sceneOffsetX, sceneOffsetY, rl.NewColor(0, 255, 0, 255), rl.NewColor(0, 255, 255, 100))
	}
}

func (d *DebugOverlay) drawObjectBoundingBox(obj engine2D.RenderObject, renderScale, sceneOffsetX, sceneOffsetY float64, imageCol, particleCol rl.Color) {
		if obj.Image != nil {
			w, h := obj.Image.Width, obj.Image.Height
			sw, sh := float64(w)*obj.Object.Scale.X*renderScale, float64(h)*obj.Object.Scale.Y*renderScale
	
			cx := sceneOffsetX + (obj.Object.Origin.X+obj.Offset.X)*renderScale
			cy := sceneOffsetY + (obj.Object.Origin.Y+obj.Offset.Y)*renderScale
	
			x := cx - sw/2
			y := cy - sh/2
	
			rl.DrawRectangleLines(int32(x), int32(y), int32(sw), int32(sh), imageCol)
	
			// Draw origin point as a small red rectangle
			rl.DrawRectangle(int32(cx-2), int32(cy-2), 4, 4, rl.Red)
		}
	
		if obj.ParticleSystem != nil {
			pw, ph := 0.0, 0.0
			if obj.ParticleSystem.Texture != nil {
				pw, ph = float64(obj.ParticleSystem.Texture.Width), float64(obj.ParticleSystem.Texture.Height)
			} else {
				pw, ph = 2, 2
			}
	
			originX := sceneOffsetX + (obj.Object.Origin.X+obj.Offset.X)*renderScale
			originY := sceneOffsetY + (obj.Object.Origin.Y+obj.Offset.Y)*renderScale
	
			// Draw system origin for particles
			rl.DrawRectangle(int32(originX-2), int32(originY-2), 4, 4, rl.Red)
	
			for _, p := range obj.ParticleSystem.Particles {			scaleX := obj.Object.Scale.X * p.Size / 100.0 * renderScale
			scaleY := obj.Object.Scale.Y * p.Size / 100.0 * renderScale

			currPW := pw * scaleX
			currPH := ph * scaleY

			px := originX + p.Position.X*obj.Object.Scale.X*renderScale - currPW/2
			py := originY + p.Position.Y*obj.Object.Scale.Y*renderScale - currPH/2

			rl.DrawRectangleLines(int32(px), int32(py), int32(currPW), int32(currPH), particleCol)
		}
	}
}
