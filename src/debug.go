package main

import (
	"fmt"
	"image"
	"image/color"

	"github.com/hajimehoshi/ebiten/v2"
	"github.com/hajimehoshi/ebiten/v2/ebitenutil"
	"github.com/hajimehoshi/ebiten/v2/vector"
)

type DebugTab int

const (
	TabHierarchy DebugTab = iota
	TabParticle
)

type DebugOverlay struct {
	ActiveTab           DebugTab
	ShowBoundingBoxes   bool
	SelectedObjectIndex int
	ScrollOffset        float64
	
	// UI State
	fontHeight     int
	lineHeight     int
	tabHeight      int
	sidebarWidth   int
	
	// Input State
	prevLeftMouseButton bool
}

func NewDebugOverlay() *DebugOverlay {
	return &DebugOverlay{
		ActiveTab:           TabHierarchy,
		ShowBoundingBoxes:   false,
		SelectedObjectIndex: -1,
		fontHeight:          12,
		lineHeight:          20,
		tabHeight:           30,
		sidebarWidth:        300,
	}
}

func (d *DebugOverlay) Update() {
	x, y := ebiten.CursorPosition()
	leftPressed := ebiten.IsMouseButtonPressed(ebiten.MouseButtonLeft)
	clicked := leftPressed && !d.prevLeftMouseButton
	d.prevLeftMouseButton = leftPressed

	// Handle Tab Switching
	if clicked && y < d.tabHeight {
		if x < d.sidebarWidth/2 {
			d.ActiveTab = TabHierarchy
		} else if x < d.sidebarWidth {
			d.ActiveTab = TabParticle
		}
	}
	
	// Handle Bounding Box Toggle (Positioned at top right of sidebar area)
	toggleRect := d.getBoundingBoxToggleRect()
	if clicked && x >= toggleRect.Min.X && x <= toggleRect.Max.X && y >= toggleRect.Min.Y && y <= toggleRect.Max.Y {
		d.ShowBoundingBoxes = !d.ShowBoundingBoxes
	}

	// Handle Hierarchy Selection
	if d.ActiveTab == TabHierarchy && clicked && x < d.sidebarWidth && y > d.tabHeight+30 {
		index := (y - (d.tabHeight + 30)) / d.lineHeight
		// Note: Actual bounds check needs object count, handled in Draw or by passing data to Update
		// For simplicity, we'll optimistically update the index here and clamp in Draw or logic using it.
		d.SelectedObjectIndex = index
	}
}

func (d *DebugOverlay) Draw(screen *ebiten.Image, renderObjects []RenderObject) {
	// Draw Sidebar Background
	vector.DrawFilledRect(screen, 0, 0, float32(d.sidebarWidth), float32(screen.Bounds().Dy()), color.RGBA{0, 0, 0, 200}, false)

	// Draw Tabs
	d.drawTabs(screen)

	// Draw Toggle
	d.drawBoundingBoxToggle(screen)

	// Draw Content
	contentY := d.tabHeight + 30
	if d.ActiveTab == TabHierarchy {
		d.drawHierarchy(screen, renderObjects, contentY)
	} else {
		d.drawParticleInspector(screen, renderObjects, contentY)
	}

	// Draw Bounding Boxes in Scene
	if d.ShowBoundingBoxes {
		d.drawSceneBoundingBoxes(screen, renderObjects)
	}
}

func (d *DebugOverlay) drawTabs(screen *ebiten.Image) {
	// Hierarchy Tab
	colorHier := color.RGBA{100, 100, 100, 255}
	if d.ActiveTab == TabHierarchy {
		colorHier = color.RGBA{150, 150, 150, 255}
	}
	vector.DrawFilledRect(screen, 0, 0, float32(d.sidebarWidth/2), float32(d.tabHeight), colorHier, false)
	ebitenutil.DebugPrintAt(screen, "Hierarchy", 10, 8)

	// Particle Tab
	colorPart := color.RGBA{100, 100, 100, 255}
	if d.ActiveTab == TabParticle {
		colorPart = color.RGBA{150, 150, 150, 255}
	}
	vector.DrawFilledRect(screen, float32(d.sidebarWidth/2), 0, float32(d.sidebarWidth/2), float32(d.tabHeight), colorPart, false)
	ebitenutil.DebugPrintAt(screen, "Particle", d.sidebarWidth/2+10, 8)
}

func (d *DebugOverlay) getBoundingBoxToggleRect() image.Rectangle {
	return image.Rectangle{
		Min: image.Point{X: 10, Y: d.tabHeight + 5},
		Max: image.Point{X: d.sidebarWidth - 10, Y: d.tabHeight + 25},
	}
}

func (d *DebugOverlay) drawBoundingBoxToggle(screen *ebiten.Image) {
	rect := d.getBoundingBoxToggleRect()
	
	// Draw Checkbox
	boxSize := 14
	boxX := rect.Min.X
	boxY := rect.Min.Y + (rect.Dy()-boxSize)/2
	
	vector.StrokeRect(screen, float32(boxX), float32(boxY), float32(boxSize), float32(boxSize), 1, color.White, false)
	if d.ShowBoundingBoxes {
		vector.DrawFilledRect(screen, float32(boxX+2), float32(boxY+2), float32(boxSize-4), float32(boxSize-4), color.White, false)
	}
	
	ebitenutil.DebugPrintAt(screen, "Show Bounding Boxes", boxX + boxSize + 10, boxY)
}

func (d *DebugOverlay) drawHierarchy(screen *ebiten.Image, renderObjects []RenderObject, startY int) {
	if d.SelectedObjectIndex >= len(renderObjects) {
		d.SelectedObjectIndex = -1
	}

	for i, obj := range renderObjects {
		y := startY + i*d.lineHeight
		if y > screen.Bounds().Dy() {
			break
		}

		// Highlight selection
		if i == d.SelectedObjectIndex {
			vector.DrawFilledRect(screen, 0, float32(y), float32(d.sidebarWidth), float32(d.lineHeight), color.RGBA{60, 60, 100, 255}, false)
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
}

func (d *DebugOverlay) drawParticleInspector(screen *ebiten.Image, renderObjects []RenderObject, startY int) {
	// Summary
	totalParticles := 0
	activeSystems := 0
	
	for _, obj := range renderObjects {
		if obj.ParticleSystem != nil {
			activeSystems++
			totalParticles += len(obj.ParticleSystem.Particles)
		}
	}
	
	ebitenutil.DebugPrintAt(screen, fmt.Sprintf("Total Particles: %d", totalParticles), 10, startY)
	ebitenutil.DebugPrintAt(screen, fmt.Sprintf("Active Systems: %d", activeSystems), 10, startY+d.lineHeight)
	
	y := startY + d.lineHeight*3
	ebitenutil.DebugPrintAt(screen, "Systems:", 10, y)
	y += d.lineHeight

	for _, obj := range renderObjects {
		if obj.ParticleSystem != nil {
			ps := obj.ParticleSystem
			text := fmt.Sprintf("- %s: %d", ps.Name, len(ps.Particles))
			ebitenutil.DebugPrintAt(screen, text, 20, y)
			y += d.lineHeight
			
			// If this object is selected, show more details
			// (We reuse the global selection index even though it's set in hierarchy tab, 
			// it feels consistent to inspect the selected object)
			if d.SelectedObjectIndex != -1 && d.SelectedObjectIndex < len(renderObjects) && renderObjects[d.SelectedObjectIndex].Object == obj.Object {
				ebitenutil.DebugPrintAt(screen, fmt.Sprintf("  Rate: %.1f", ps.Config.Emitter[0].Rate.Value), 30, y)
				y += d.lineHeight
			}
		}
	}
}

func (d *DebugOverlay) drawSceneBoundingBoxes(screen *ebiten.Image, renderObjects []RenderObject) {
	for i, obj := range renderObjects {
		// Draw Object Bounding Box
		// Note: This is a rough approximation based on Image size and scale
		// A real bounding box would need to account for rotation and origin
		
		// Color for selected object is different
		col := color.RGBA{0, 255, 0, 255}
		if i == d.SelectedObjectIndex {
			col = color.RGBA{255, 255, 0, 255}
		}

		if obj.Image != nil {
			w, h := obj.Image.Bounds().Dx(), obj.Image.Bounds().Dy()
			sw, sh := float64(w)*obj.Object.Scale.X, float64(h)*obj.Object.Scale.Y
			
			// Adjust for origin (Wallpaper Engine origin is often center or specific point)
			// Assuming Origin.X/Y are offsets in pixels from center if not normalized, 
			// but usually they are translation offsets. 
			// Based on window.go Draw: Translate(-w/2, -h/2) -> Scale -> Translate(Origin+Offset)
			
			// Center of the object in screen space
			cx := obj.Object.Origin.X + obj.Offset.X
			cy := obj.Object.Origin.Y + obj.Offset.Y
			
			// Top-left corner (ignoring rotation for simple AABB debug)
			x := cx - sw/2
			y := cy - sh/2
			
			vector.StrokeRect(screen, float32(x), float32(y), float32(sw), float32(sh), 1, col, false)
		}

		// Draw Particle Bounding Boxes (Individual particles)
		if obj.ParticleSystem != nil {
			// Use a lighter color for particles
			pCol := color.RGBA{0, 255, 255, 100}
			
			// Get texture size for particles
			pw, ph := 0.0, 0.0
			if obj.ParticleSystem.Texture != nil {
				b := obj.ParticleSystem.Texture.Bounds()
				pw, ph = float64(b.Dx()), float64(b.Dy())
			} else {
				pw, ph = 2, 2 // Fallback
			}

			for _, p := range obj.ParticleSystem.Particles {
				// Particle draw logic: Translate(-w/2, -h/2) -> Scale(objScale * p.Size/100) -> Translate(Origin + p.Pos)
				scaleX := obj.Object.Scale.X * p.Size / 100.0
				scaleY := obj.Object.Scale.Y * p.Size / 100.0
				
				currPW := pw * scaleX
				currPH := ph * scaleY
				
				originX := obj.Object.Origin.X + obj.Offset.X
				originY := obj.Object.Origin.Y + obj.Offset.Y
				
				px := originX + p.Position.X - currPW/2
				py := originY + p.Position.Y - currPH/2
				
				vector.StrokeRect(screen, float32(px), float32(py), float32(currPW), float32(currPH), 1, pCol, false)
			}
		}
	}
}
