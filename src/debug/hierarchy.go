package debug

import (
	"fmt"
	"image/color"

	"linux-wallpaperengine/src/types"
	"linux-wallpaperengine/src/wallpaper"

	"github.com/hajimehoshi/ebiten/v2"
	"github.com/hajimehoshi/ebiten/v2/ebitenutil"
	"github.com/hajimehoshi/ebiten/v2/vector"
)

func (d *DebugOverlay) drawHierarchy(screen *ebiten.Image, renderObjects []types.RenderObject, startY int, maxHeight int) {
	if d.SelectedObjectIndex >= len(renderObjects) {
		d.SelectedObjectIndex = -1
	}

	listWidth := d.sidebarWidth / 2
	inspectorX := listWidth

	// Handle inspector clicks for toggling booleans
	if d.editingActive {
		d.editingActive = false
		mx, my := ebiten.CursorPosition()
		if mx >= listWidth && d.SelectedObjectIndex >= 0 && d.SelectedObjectIndex < len(renderObjects) {
			d.handleInspectorClick(renderObjects[d.SelectedObjectIndex].Object, float64(mx), float64(my), startY)
		}
	}

	// Draw object list
	for i, obj := range renderObjects {
		y := startY + i*d.lineHeight - int(d.ScrollOffset)
		if y < startY || y > maxHeight {
			continue
		}

		// Highlight selection
		if i == d.SelectedObjectIndex {
			vector.FillRect(screen, 0, float32(y), float32(listWidth), float32(d.lineHeight), color.RGBA{60, 60, 100, 255}, false)
		}

		name := obj.Object.Name
		if name == "" {
			name = fmt.Sprintf("Object %d", obj.Object.ID)
		}

		info := name
		if obj.ParticleSystem != nil {
			info += " [P]"
		}

		ebitenutil.DebugPrintAt(screen, info, 10, y+4)
	}

	// Draw separator line
	vector.StrokeLine(screen, float32(listWidth), float32(startY), float32(listWidth), float32(maxHeight), 1, color.RGBA{100, 100, 100, 255}, false)

	// Draw inspector panel
	if d.SelectedObjectIndex >= 0 && d.SelectedObjectIndex < len(renderObjects) {
		obj := renderObjects[d.SelectedObjectIndex]
		d.drawInspector(screen, &obj, inspectorX, startY, maxHeight)
	}
}

func (d *DebugOverlay) handleInspectorClick(obj *wallpaper.Object, x, y float64, startY int) {
	// Calculate the base Y position where properties start
	// Title line + Type line + ID line + spacing + "Properties:" line = 4 lines + spacing
	propStartY := startY + d.lineHeight + d.lineHeight/2 + d.lineHeight + d.lineHeight + d.lineHeight/2 + d.lineHeight + int(d.InspectorScroll)

	// Calculate which property line was clicked (relative to properties start)
	clickedLine := int((y - float64(propStartY)) / float64(d.lineHeight))

	// Property lines: 0=Origin, 1=Scale, 2=Angles, 3=Alpha, 4=Brightness, 5=Visible
	if clickedLine == 5 {
		// Toggle Visible property
		obj.Visible.Value = !obj.Visible.Value
	}
}
