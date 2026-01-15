package main

import (
	"fmt"
	"image"
	"image/color"
	"math"
	"runtime"
	"time"

	"linux-wallpaperengine/src/wallpaper"

	"github.com/hajimehoshi/ebiten/v2"
	"github.com/hajimehoshi/ebiten/v2/ebitenutil"
	"github.com/hajimehoshi/ebiten/v2/vector"
)

type DebugTab int

const (
	TabHierarchy DebugTab = iota
	TabParticle
	TabPerformance
)

type DebugOverlay struct {
	ActiveTab           DebugTab
	ShowBoundingBoxes   bool
	SelectedObjectIndex int
	ScrollOffset        float64
	InspectorScroll     float64

	// UI State
	fontHeight   int
	lineHeight   int
	tabHeight    int
	sidebarWidth int

	// Input State
	prevLeftMouseButton bool
	editingField        string
	editingValue        string
	editingActive       bool

	// Rendering
	uiBuffer      *ebiten.Image
	uiScale       float64
	cachedWidth   int
	cachedHeight  int
	monitorWidth  int
	monitorHeight int

	// Performance Monitoring
	lastUpdateTime time.Time
	frameCount     int
	fps            float64
	memStats       runtime.MemStats
}

func NewDebugOverlay() *DebugOverlay {
	monitor := ebiten.Monitor()
	monitorW, monitorH := monitor.Size()

	// Scale based on monitor height, not window
	scale := math.Max(1.0, float64(monitorH)/1080.0)
	sidebarW := int(400 * scale)

	_, windowH := ebiten.WindowSize()

	return &DebugOverlay{
		ActiveTab:           TabHierarchy,
		ShowBoundingBoxes:   false,
		SelectedObjectIndex: -1,
		fontHeight:          int(16 * scale),
		lineHeight:          int(28 * scale),
		tabHeight:           int(40 * scale),
		sidebarWidth:        sidebarW,
		uiBuffer:            ebiten.NewImage(sidebarW, windowH),
		uiScale:             1.0,
		cachedWidth:         sidebarW,
		cachedHeight:        windowH,
		monitorWidth:        monitorW,
		monitorHeight:       monitorH,
		lastUpdateTime:      time.Now(),
		frameCount:          0,
		fps:                 0,
	}
}

func (d *DebugOverlay) Update() {
	monitor := ebiten.Monitor()
	monitorW, monitorH := monitor.Size()

	// Update if monitor changed
	if d.monitorWidth != monitorW || d.monitorHeight != monitorH {
		d.monitorWidth = monitorW
		d.monitorHeight = monitorH
	}

	// Scale based on physical monitor resolution
	scale := math.Max(1.0, float64(monitorH)/1080.0)

	d.fontHeight = int(16 * scale)
	d.lineHeight = int(28 * scale)
	d.tabHeight = int(40 * scale)
	d.sidebarWidth = int(400 * scale)
	d.uiScale = 1.0

	// Update performance metrics
	d.frameCount++
	now := time.Now()
	if now.Sub(d.lastUpdateTime) >= time.Second {
		d.fps = float64(d.frameCount) / now.Sub(d.lastUpdateTime).Seconds()
		d.frameCount = 0
		d.lastUpdateTime = now
		runtime.ReadMemStats(&d.memStats)
	}

	mx, my := ebiten.CursorPosition()

	x := float64(mx)
	y := float64(my)

	leftPressed := ebiten.IsMouseButtonPressed(ebiten.MouseButtonLeft)
	clicked := leftPressed && !d.prevLeftMouseButton
	d.prevLeftMouseButton = leftPressed

	// Handle Tab Switching
	if clicked && y < float64(d.tabHeight) {
		tabWidth := float64(d.sidebarWidth) / 3
		if x < tabWidth {
			d.ActiveTab = TabHierarchy
		} else if x < tabWidth*2 {
			d.ActiveTab = TabParticle
		} else if x < float64(d.sidebarWidth) {
			d.ActiveTab = TabPerformance
		}
	}

	// Handle Bounding Box Toggle
	toggleRect := d.getBoundingBoxToggleRect()
	if clicked && x >= float64(toggleRect.Min.X) && x <= float64(toggleRect.Max.X) && y >= float64(toggleRect.Min.Y) && y <= float64(toggleRect.Max.Y) {
		d.ShowBoundingBoxes = !d.ShowBoundingBoxes
	}

	// Handle Hierarchy Selection
	if d.ActiveTab == TabHierarchy && clicked && x < float64(d.sidebarWidth/2) && y > float64(d.tabHeight+30) {
		index := int((y - float64(d.tabHeight+30) + d.ScrollOffset) / float64(d.lineHeight))
		d.SelectedObjectIndex = index
	}

	// Handle Inspector Clicks (for toggling booleans)
	if d.ActiveTab == TabHierarchy && clicked && x >= float64(d.sidebarWidth/2) && d.SelectedObjectIndex >= 0 {
		// Need to pass renderObjects here, but we need to refactor Update to receive them
		// For now, store the click position and handle it in Draw
		d.editingActive = true
	}

	// Handle mouse wheel scrolling for object list (left half)
	if d.ActiveTab == TabHierarchy && x < float64(d.sidebarWidth/2) {
		_, dy := ebiten.Wheel()
		d.ScrollOffset -= dy * 20 // Scroll speed multiplier
		if d.ScrollOffset < 0 {
			d.ScrollOffset = 0
		}
	}

	// Handle mouse wheel scrolling for inspector (right half)
	if d.ActiveTab == TabHierarchy && x >= float64(d.sidebarWidth/2) && x < float64(d.sidebarWidth) {
		_, dy := ebiten.Wheel()
		d.InspectorScroll += dy * 20 // Scroll speed multiplier
		if d.InspectorScroll > 0 {
			d.InspectorScroll = 0
		}
	}
}

func (d *DebugOverlay) Draw(screen *ebiten.Image, renderObjects []RenderObject, sceneWidth, sceneHeight int, renderScale float64) {
	_, sh := screen.Bounds().Dx(), screen.Bounds().Dy()

	if d.cachedWidth != d.sidebarWidth || d.cachedHeight != sh {
		if d.uiBuffer != nil {
			d.uiBuffer.Dispose()
		}
		d.uiBuffer = ebiten.NewImage(d.sidebarWidth, sh)
		d.cachedWidth = d.sidebarWidth
		d.cachedHeight = sh
	}

	d.uiBuffer.Clear()

	vector.FillRect(d.uiBuffer, 0, 0, float32(d.sidebarWidth), float32(sh), color.RGBA{0, 0, 0, 200}, false)

	// Draw Tabs
	d.drawTabs(d.uiBuffer)

	// Draw Toggle
	d.drawBoundingBoxToggle(d.uiBuffer)

	// Draw Content
	contentY := d.tabHeight + int(float64(d.tabHeight)*0.75)
	if d.ActiveTab == TabHierarchy {
		d.drawHierarchy(d.uiBuffer, renderObjects, contentY, sh)
	} else if d.ActiveTab == TabParticle {
		d.drawParticleInspector(d.uiBuffer, renderObjects, contentY)
	} else if d.ActiveTab == TabPerformance {
		d.drawPerformance(d.uiBuffer, contentY, sceneWidth, sceneHeight, renderScale)
	}

	screen.DrawImage(d.uiBuffer, &ebiten.DrawImageOptions{})

	// Always show bounding box for selected object, even if ShowBoundingBoxes is off
	if d.SelectedObjectIndex >= 0 && d.SelectedObjectIndex < len(renderObjects) {
		d.drawSelectedBoundingBox(screen, renderObjects[d.SelectedObjectIndex], renderScale)
	}

	if d.ShowBoundingBoxes {
		d.drawSceneBoundingBoxes(screen, renderObjects, renderScale)
	}
}

func (d *DebugOverlay) drawTabs(screen *ebiten.Image) {
	tabWidth := d.sidebarWidth / 3

	// Hierarchy Tab
	colorHier := color.RGBA{100, 100, 100, 255}
	if d.ActiveTab == TabHierarchy {
		colorHier = color.RGBA{150, 150, 150, 255}
	}
	vector.FillRect(screen, 0, 0, float32(tabWidth), float32(d.tabHeight), colorHier, false)
	ebitenutil.DebugPrintAt(screen, "Hierarchy", 10, int(float64(d.tabHeight)*0.3))

	// Particle Tab
	colorPart := color.RGBA{100, 100, 100, 255}
	if d.ActiveTab == TabParticle {
		colorPart = color.RGBA{150, 150, 150, 255}
	}
	vector.FillRect(screen, float32(tabWidth), 0, float32(tabWidth), float32(d.tabHeight), colorPart, false)
	ebitenutil.DebugPrintAt(screen, "Particle", tabWidth+10, int(float64(d.tabHeight)*0.3))

	// Performance Tab
	colorPerf := color.RGBA{100, 100, 100, 255}
	if d.ActiveTab == TabPerformance {
		colorPerf = color.RGBA{150, 150, 150, 255}
	}
	vector.FillRect(screen, float32(tabWidth*2), 0, float32(tabWidth), float32(d.tabHeight), colorPerf, false)
	ebitenutil.DebugPrintAt(screen, "Performance", tabWidth*2+10, int(float64(d.tabHeight)*0.3))
}

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

func (d *DebugOverlay) drawHierarchy(screen *ebiten.Image, renderObjects []RenderObject, startY int, maxHeight int) {
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

func (d *DebugOverlay) drawInspector(screen *ebiten.Image, renderObj *RenderObject, startX, startY int, maxHeight int) {
	y := startY + int(d.InspectorScroll)
	x := startX + 10

	obj := renderObj.Object

	// Title
	ebitenutil.DebugPrintAt(screen, "Inspector", x, y)
	y += d.lineHeight + d.lineHeight/2

	// Object Type
	objType := "Image"
	if obj.Particle != "" {
		objType = "Particle"
	} else if obj.Text.Value != "" || obj.Text.Script != "" {
		objType = "Text"
	} else if obj.Sound.Value != "" {
		objType = "Sound"
	}
	ebitenutil.DebugPrintAt(screen, fmt.Sprintf("Type: %s", objType), x, y)
	y += d.lineHeight

	// ID
	ebitenutil.DebugPrintAt(screen, fmt.Sprintf("ID: %d", obj.ID), x, y)
	y += d.lineHeight

	y += d.lineHeight / 2

	// Properties
	ebitenutil.DebugPrintAt(screen, "Properties:", x, y)
	y += d.lineHeight

	// Origin (Vec3)
	ebitenutil.DebugPrintAt(screen, fmt.Sprintf("Origin: (%.1f, %.1f, %.1f)", obj.Origin.X, obj.Origin.Y, obj.Origin.Z), x+5, y)
	y += d.lineHeight

	// Scale (Vec3)
	ebitenutil.DebugPrintAt(screen, fmt.Sprintf("Scale: (%.2f, %.2f, %.2f)", obj.Scale.X, obj.Scale.Y, obj.Scale.Z), x+5, y)
	y += d.lineHeight

	// Angles (Vec3)
	ebitenutil.DebugPrintAt(screen, fmt.Sprintf("Angles: (%.2f, %.2f, %.2f)", obj.Angles.X, obj.Angles.Y, obj.Angles.Z), x+5, y)
	y += d.lineHeight

	// Alpha (float)
	ebitenutil.DebugPrintAt(screen, fmt.Sprintf("Alpha: %.2f", obj.Alpha.Value), x+5, y)
	y += d.lineHeight

	// Brightness (float)
	ebitenutil.DebugPrintAt(screen, fmt.Sprintf("Brightness: %.2f", obj.Brightness), x+5, y)
	y += d.lineHeight

	// Visible (bool) - clickable
	visibleStr := "true"
	if !obj.Visible.Value {
		visibleStr = "false"
	}

	// Draw clickable indicator
	boxSize := int(float64(d.fontHeight) * 0.8)
	boxX := x + 5
	boxY := y + 2
	vector.StrokeRect(screen, float32(boxX), float32(boxY), float32(boxSize), float32(boxSize), 1, color.RGBA{150, 150, 150, 255}, false)
	if obj.Visible.Value {
		vector.FillRect(screen, float32(boxX+2), float32(boxY+2), float32(boxSize-4), float32(boxSize-4), color.RGBA{100, 255, 100, 255}, false)
	}

	ebitenutil.DebugPrintAt(screen, fmt.Sprintf("Visible: %s", visibleStr), x+5+boxSize+5, y)
	y += d.lineHeight

	// Size (Vec2) - if applicable
	if obj.Size.X > 0 || obj.Size.Y > 0 {
		ebitenutil.DebugPrintAt(screen, fmt.Sprintf("Size: (%.1f, %.1f)", obj.Size.X, obj.Size.Y), x+5, y)
		y += d.lineHeight
	}

	// Color (string)
	if obj.Color != "" {
		ebitenutil.DebugPrintAt(screen, fmt.Sprintf("Color: %s", obj.Color), x+5, y)
		y += d.lineHeight
	}

	// Effects count
	if len(obj.Effects) > 0 {
		y += d.lineHeight / 2
		ebitenutil.DebugPrintAt(screen, fmt.Sprintf("Effects: %d", len(obj.Effects)), x, y)
		y += d.lineHeight

		for i, effect := range obj.Effects {
			if i >= 3 { // Limit display
				ebitenutil.DebugPrintAt(screen, fmt.Sprintf("  ... and %d more", len(obj.Effects)-3), x+5, y)
				break
			}
			effectName := effect.Name
			if effect.File != "" {
				effectName = effect.File
			}
			visStr := "✓"
			if !effect.Visible.Value {
				visStr = "✗"
			}
			ebitenutil.DebugPrintAt(screen, fmt.Sprintf("  [%s] %s", visStr, effectName), x+5, y)
			y += d.lineHeight
		}
	}

	// Particle-specific info
	if obj.Particle != "" {
		y += d.lineHeight / 2
		ebitenutil.DebugPrintAt(screen, fmt.Sprintf("Particle: %s", obj.Particle), x, y)
		y += d.lineHeight

		// Show actual renderer type based on configuration
		if renderObj.ParticleSystem != nil {
			ps := renderObj.ParticleSystem
			rendererName := "default"
			if len(ps.Config.Renderer) > 0 {
				rendererName = ps.Config.Renderer[0].Name
			}

			useSpriteSheet := ps.Config.SequenceMultiplier > 1
			if rendererName == "sprite" && useSpriteSheet {
				ebitenutil.DebugPrintAt(screen, fmt.Sprintf("Renderer: sprite (sheet %dx%d)",
					int(ps.Config.SequenceMultiplier*4), int(ps.Config.SequenceMultiplier*4)), x+5, y)
			} else {
				ebitenutil.DebugPrintAt(screen, fmt.Sprintf("Renderer: %s", rendererName), x+5, y)
			}
			y += d.lineHeight
		}

		if obj.InstanceOverride != nil {
			ebitenutil.DebugPrintAt(screen, "Overrides:", x+5, y)
			y += d.lineHeight
			if obj.InstanceOverride.Count.Value != 0 {
				ebitenutil.DebugPrintAt(screen, fmt.Sprintf("  Count: %.2f", obj.InstanceOverride.Count.Value), x+10, y)
				y += d.lineHeight
			}
			if obj.InstanceOverride.Size.Value != 0 {
				ebitenutil.DebugPrintAt(screen, fmt.Sprintf("  Size: %.2f", obj.InstanceOverride.Size.Value), x+10, y)
				y += d.lineHeight
			}
			if obj.InstanceOverride.Speed.Value != 0 {
				ebitenutil.DebugPrintAt(screen, fmt.Sprintf("  Speed: %.2f", obj.InstanceOverride.Speed.Value), x+10, y)
				y += d.lineHeight
			}
		}
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

			if d.SelectedObjectIndex != -1 && d.SelectedObjectIndex < len(renderObjects) && renderObjects[d.SelectedObjectIndex].Object == obj.Object {
				ebitenutil.DebugPrintAt(screen, fmt.Sprintf("  Rate: %.1f", ps.Config.Emitter[0].Rate.Value), 30, y)
				y += d.lineHeight
			}
		}
	}
}

func (d *DebugOverlay) drawPerformance(screen *ebiten.Image, startY int, sceneWidth, sceneHeight int, renderScale float64) {
	y := startY

	// FPS
	ebitenutil.DebugPrintAt(screen, fmt.Sprintf("FPS: %.1f / %.1f", d.fps, ebiten.ActualTPS()), 10, y)
	y += d.lineHeight

	// TPS (Ticks Per Second)
	ebitenutil.DebugPrintAt(screen, fmt.Sprintf("TPS: %.1f", ebiten.ActualTPS()), 10, y)
	y += d.lineHeight

	y += d.lineHeight / 2

	// Memory Stats
	ebitenutil.DebugPrintAt(screen, "Memory Usage:", 10, y)
	y += d.lineHeight

	allocMB := float64(d.memStats.Alloc) / 1024 / 1024
	ebitenutil.DebugPrintAt(screen, fmt.Sprintf("  Allocated: %.2f MB", allocMB), 10, y)
	y += d.lineHeight

	totalAllocMB := float64(d.memStats.TotalAlloc) / 1024 / 1024
	ebitenutil.DebugPrintAt(screen, fmt.Sprintf("  Total Alloc: %.2f MB", totalAllocMB), 10, y)
	y += d.lineHeight

	sysMB := float64(d.memStats.Sys) / 1024 / 1024
	ebitenutil.DebugPrintAt(screen, fmt.Sprintf("  System: %.2f MB", sysMB), 10, y)
	y += d.lineHeight

	heapAllocMB := float64(d.memStats.HeapAlloc) / 1024 / 1024
	ebitenutil.DebugPrintAt(screen, fmt.Sprintf("  Heap Alloc: %.2f MB", heapAllocMB), 10, y)
	y += d.lineHeight

	heapSysMB := float64(d.memStats.HeapSys) / 1024 / 1024
	ebitenutil.DebugPrintAt(screen, fmt.Sprintf("  Heap System: %.2f MB", heapSysMB), 10, y)
	y += d.lineHeight

	y += d.lineHeight / 2

	// Garbage Collection
	ebitenutil.DebugPrintAt(screen, "Garbage Collection:", 10, y)
	y += d.lineHeight

	ebitenutil.DebugPrintAt(screen, fmt.Sprintf("  GC Runs: %d", d.memStats.NumGC), 10, y)
	y += d.lineHeight

	lastPauseMicro := d.memStats.PauseNs[(d.memStats.NumGC+255)%256] / 1000
	ebitenutil.DebugPrintAt(screen, fmt.Sprintf("  Last Pause: %d µs", lastPauseMicro), 10, y)
	y += d.lineHeight

	y += d.lineHeight / 2

	// Goroutines
	ebitenutil.DebugPrintAt(screen, fmt.Sprintf("Goroutines: %d", runtime.NumGoroutine()), 10, y)
	y += d.lineHeight

	y += d.lineHeight / 2

	// GPU Info (from Ebitengine)
	ebitenutil.DebugPrintAt(screen, "Graphics:", 10, y)
	y += d.lineHeight

	// Get graphics driver info
	graphicsDriver := "OpenGL/Metal/DirectX"
	ebitenutil.DebugPrintAt(screen, fmt.Sprintf("  Backend: %s", graphicsDriver), 10, y)
	y += d.lineHeight

	// Monitor resolution
	ebitenutil.DebugPrintAt(screen, fmt.Sprintf("  Monitor: %dx%d", d.monitorWidth, d.monitorHeight), 10, y)
	y += d.lineHeight

	// Scene resolution
	if sceneWidth > 0 && sceneHeight > 0 {
		ebitenutil.DebugPrintAt(screen, fmt.Sprintf("  Scene: %dx%d", sceneWidth, sceneHeight), 10, y)
		y += d.lineHeight
	}

	// Window size (actual render resolution)
	w, h := ebiten.WindowSize()
	ebitenutil.DebugPrintAt(screen, fmt.Sprintf("  Render: %dx%d", w, h), 10, y)
	y += d.lineHeight

	// Render scale
	if renderScale != 1.0 {
		ebitenutil.DebugPrintAt(screen, fmt.Sprintf("  Render Scale: %.2fx", renderScale), 10, y)
		y += d.lineHeight
	}

	// Device scale
	scale := ebiten.Monitor().DeviceScaleFactor()
	ebitenutil.DebugPrintAt(screen, fmt.Sprintf("  Device Scale: %.2fx", scale), 10, y)
	y += d.lineHeight

	// UI Scale
	uiScale := math.Max(1.0, float64(d.monitorHeight)/1080.0)
	ebitenutil.DebugPrintAt(screen, fmt.Sprintf("  UI Scale: %.2fx", uiScale), 10, y)
	y += d.lineHeight
}

func (d *DebugOverlay) drawSelectedBoundingBox(screen *ebiten.Image, obj RenderObject, renderScale float64) {
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

func (d *DebugOverlay) drawSceneBoundingBoxes(screen *ebiten.Image, renderObjects []RenderObject, renderScale float64) {
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
