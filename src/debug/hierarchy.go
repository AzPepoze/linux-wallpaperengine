package debug

import (
	"fmt"

	"linux-wallpaperengine/src/types"

	rl "github.com/gen2brain/raylib-go/raylib"
)

func (d *DebugOverlay) drawHierarchy(renderObjects []types.RenderObject, startY int, maxHeight int) {
	// Left side: Hierarchy List
	listWidth := d.sidebarWidth / 2
	
	rl.BeginScissorMode(0, int32(startY), int32(listWidth), int32(maxHeight-startY))

	y := startY - int(d.ScrollOffset)
	mPos := rl.GetMousePosition()
	mx, my := int(mPos.X), int(mPos.Y)

	for i, ro := range renderObjects {
		if y > maxHeight {
			break
		}
		if y+d.lineHeight < startY {
			y += d.lineHeight
			continue
		}

		// Highlight row if selected or hovered
		selected := i == d.SelectedObjectIndex
		hovered := mx < listWidth && my >= y && my < y+d.lineHeight

		if selected {
			rl.DrawRectangle(0, int32(y), int32(listWidth), int32(d.lineHeight), rl.NewColor(50, 50, 150, 255))
		} else if hovered {
			rl.DrawRectangle(0, int32(y), int32(listWidth), int32(d.lineHeight), rl.NewColor(60, 60, 60, 255))
		}

		// Draw Name
		displayName := ro.Object.Name
		if displayName == "" {
			if ro.Object.Particle != "" {
				displayName = ro.Object.Particle
			} else if ro.Object.Image != "" {
				displayName = ro.Object.Image
			} else {
				displayName = "Unnamed Object"
			}
		}

		info := fmt.Sprintf("%d: %s", i, displayName)
		if ro.ParticleSystem != nil {
			info += " (Particle)"
		} else if ro.Object.GetText() != "" {
			info += " (Text)"
		}

		d.DrawText(info, 10, int32(y+4), int32(d.fontHeight), rl.White)

		y += d.lineHeight
	}
	rl.EndScissorMode()

	// Right side: Inspector
	if d.SelectedObjectIndex >= 0 && d.SelectedObjectIndex < len(renderObjects) {
		d.drawInspector(&renderObjects[d.SelectedObjectIndex], listWidth, startY, maxHeight, d.mouseX, d.mouseY, d.clicked)
	}
}
