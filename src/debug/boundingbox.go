package debug

import (
	"image"
	"image/color"

	"linux-wallpaperengine/src/types"

	"github.com/hajimehoshi/ebiten/v2"
	"github.com/hajimehoshi/ebiten/v2/ebitenutil"
	"github.com/hajimehoshi/ebiten/v2/vector"
)

func (d *DebugOverlay) getBoundingBoxToggleRect() image.Rectangle {
	return image.Rectangle{
		Min: image.Point{X: 10, Y: d.tabHeight + 5},
		Max: image.Point{X: d.sidebarWidth - 10, Y: d.tabHeight + 25},
	}
}

func (d *DebugOverlay) drawBoundingBoxToggle(screen *ebiten.Image) {
	rect := d.getBoundingBoxToggleRect()

	boxSize := int(float64(d.fontHeight) * 1.2)
	boxX := rect.Min.X
	boxY := rect.Min.Y + (rect.Dy()-boxSize)/2

	vector.StrokeRect(screen, float32(boxX), float32(boxY), float32(boxSize), float32(boxSize), 1, color.White, false)
	if d.ShowBoundingBoxes {
		vector.FillRect(screen, float32(boxX+2), float32(boxY+2), float32(boxSize-4), float32(boxSize-4), color.White, false)
	}

	ebitenutil.DebugPrintAt(screen, "Show Bounding Boxes", boxX+boxSize+10, boxY)
}

func (d *DebugOverlay) drawSelectedBoundingBox(screen *ebiten.Image, obj types.RenderObject, renderScale float64) {
	col := color.RGBA{255, 255, 0, 255} // Yellow for selected

	if obj.Image != nil {
		w, h := obj.Image.Bounds().Dx(), obj.Image.Bounds().Dy()
		sw, sh := float64(w)*obj.Object.Scale.X*renderScale, float64(h)*obj.Object.Scale.Y*renderScale

		cx := (obj.Object.Origin.X + obj.Offset.X) * renderScale
		cy := (obj.Object.Origin.Y + obj.Offset.Y) * renderScale

		x := cx - sw/2
		y := cy - sh/2

		vector.StrokeRect(screen, float32(x), float32(y), float32(sw), float32(sh), 2, col, false)
	}

	if obj.ParticleSystem != nil {
		pCol := color.RGBA{255, 255, 0, 150} // Semi-transparent yellow

		pw, ph := 0.0, 0.0
		if obj.ParticleSystem.Texture != nil {
			b := obj.ParticleSystem.Texture.Bounds()
			pw, ph = float64(b.Dx()), float64(b.Dy())
		} else {
			pw, ph = 2, 2
		}

		for _, p := range obj.ParticleSystem.Particles {
			scaleX := obj.Object.Scale.X * p.Size / 100.0 * renderScale
			scaleY := obj.Object.Scale.Y * p.Size / 100.0 * renderScale

			currPW := pw * scaleX
			currPH := ph * scaleY

			originX := (obj.Object.Origin.X + obj.Offset.X) * renderScale
			originY := (obj.Object.Origin.Y + obj.Offset.Y) * renderScale

			px := originX + p.Position.X*renderScale - currPW/2
			py := originY + p.Position.Y*renderScale - currPH/2

			vector.StrokeRect(screen, float32(px), float32(py), float32(currPW), float32(currPH), 1, pCol, false)
		}
	}
}

func (d *DebugOverlay) drawSceneBoundingBoxes(screen *ebiten.Image, renderObjects []types.RenderObject, renderScale float64) {
	for i, obj := range renderObjects {
		// Skip selected object since it's drawn separately
		if i == d.SelectedObjectIndex {
			continue
		}

		col := color.RGBA{0, 255, 0, 255}

		if obj.Image != nil {
			w, h := obj.Image.Bounds().Dx(), obj.Image.Bounds().Dy()
			sw, sh := float64(w)*obj.Object.Scale.X*renderScale, float64(h)*obj.Object.Scale.Y*renderScale

			cx := (obj.Object.Origin.X + obj.Offset.X) * renderScale
			cy := (obj.Object.Origin.Y + obj.Offset.Y) * renderScale

			x := cx - sw/2
			y := cy - sh/2

			vector.StrokeRect(screen, float32(x), float32(y), float32(sw), float32(sh), 1, col, false)
		}

		if obj.ParticleSystem != nil {
			pCol := color.RGBA{0, 255, 255, 100}

			pw, ph := 0.0, 0.0
			if obj.ParticleSystem.Texture != nil {
				b := obj.ParticleSystem.Texture.Bounds()
				pw, ph = float64(b.Dx()), float64(b.Dy())
			} else {
				pw, ph = 2, 2
			}

			for _, p := range obj.ParticleSystem.Particles {
				scaleX := obj.Object.Scale.X * p.Size / 100.0 * renderScale
				scaleY := obj.Object.Scale.Y * p.Size / 100.0 * renderScale

				currPW := pw * scaleX
				currPH := ph * scaleY

				originX := (obj.Object.Origin.X + obj.Offset.X) * renderScale
				originY := (obj.Object.Origin.Y + obj.Offset.Y) * renderScale

				px := originX + p.Position.X*renderScale - currPW/2
				py := originY + p.Position.Y*renderScale - currPH/2

				vector.StrokeRect(screen, float32(px), float32(py), float32(currPW), float32(currPH), 1, pCol, false)
			}
		}
	}
}
