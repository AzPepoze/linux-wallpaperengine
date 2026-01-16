package debug

import (
	"math"
	"os"
	"runtime"
	"time"

	"linux-wallpaperengine/src/types"

	rl "github.com/gen2brain/raylib-go/raylib"
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
	mouseX              int
	mouseY              int
	clicked             bool
	editingField        string
	editingValue        string
	editingActive       bool

	// Rendering
	uiBuffer          rl.RenderTexture2D
	uiScale           float64
	font              rl.Font
	cachedWidth       int
	cachedHeight      int
	monitorWidth      int
	monitorHeight     int
	bufferInitialized bool

	// Performance Monitoring
	lastUpdateTime time.Time
	frameCount     int
	fps            float64
	memStats       runtime.MemStats

	// Rendering Context (for inspector)
	renderScale  float64
	sceneOffsetX float64
	sceneOffsetY float64
}

func NewDebugOverlay() *DebugOverlay {
	monitor := rl.GetCurrentMonitor()

	d := &DebugOverlay{
		ActiveTab:           TabHierarchy,
		ShowBoundingBoxes:   false,
		SelectedObjectIndex: -1,
		monitorWidth:        rl.GetMonitorWidth(monitor),
		monitorHeight:       rl.GetMonitorHeight(monitor),
		lastUpdateTime:      time.Now(),
		frameCount:          0,
		fps:                 0,
	}

	d.updateLayout()

	// Load system font
	fontPaths := []string{
		"/usr/share/fonts/TTF/DejaVuSans.ttf",
		"/usr/share/fonts/truetype/ttf-dejavu/DejaVuSans.ttf",
		"/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
		"/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
	}

	for _, path := range fontPaths {
		if _, err := os.Stat(path); err == nil {
			d.font = rl.LoadFontEx(path, 64, nil, 0)
			rl.SetTextureFilter(d.font.Texture, rl.FilterBilinear)
			break
		}
	}

	windowH := rl.GetScreenHeight()
	d.uiBuffer = rl.LoadRenderTexture(int32(d.sidebarWidth), int32(windowH))
	d.bufferInitialized = true
	d.cachedWidth = d.sidebarWidth
	d.cachedHeight = windowH

	return d
}

func (d *DebugOverlay) updateLayout() {
	scale := math.Max(1.0, float64(d.monitorHeight)/1080.0)
	d.fontHeight = int(16 * scale)
	d.lineHeight = int(28 * scale)
	d.tabHeight = int(40 * scale)
	d.sidebarWidth = int(800 * scale)
	d.uiScale = scale
}

func (d *DebugOverlay) Update() {
	d.updateLayout()

	d.frameCount++
	now := time.Now()
	if now.Sub(d.lastUpdateTime) >= time.Second {
		d.fps = float64(d.frameCount) / now.Sub(d.lastUpdateTime).Seconds()
		d.frameCount = 0
		d.lastUpdateTime = now
		runtime.ReadMemStats(&d.memStats)
	}

	mPos := rl.GetMousePosition()
	d.mouseX = int(mPos.X)
	d.mouseY = int(mPos.Y)
	x := float64(d.mouseX)
	y := float64(d.mouseY)

	leftPressed := rl.IsMouseButtonDown(rl.MouseLeftButton)
	d.clicked = leftPressed && !d.prevLeftMouseButton
	d.prevLeftMouseButton = leftPressed

	if d.clicked && y < float64(d.tabHeight) && x < float64(d.sidebarWidth) {
		tabWidth := float64(d.sidebarWidth) / 3
		d.ActiveTab = DebugTab(int(x / tabWidth))
	}

	toggleRect := d.getBoundingBoxToggleRect()
	if d.clicked && x >= float64(toggleRect.X) && x <= float64(toggleRect.X+toggleRect.Width) && y >= float64(toggleRect.Y) && y <= float64(toggleRect.Y+toggleRect.Height) {
		d.ShowBoundingBoxes = !d.ShowBoundingBoxes
	}

	if d.ActiveTab == TabHierarchy && d.clicked && x < float64(d.sidebarWidth/2) && y > float64(d.tabHeight+30) {
		index := int((y - float64(d.tabHeight+30) + d.ScrollOffset) / float64(d.lineHeight))
		d.SelectedObjectIndex = index
	}

	if d.ActiveTab == TabHierarchy && d.clicked && x >= float64(d.sidebarWidth/2) && d.SelectedObjectIndex >= 0 {
		d.editingActive = true
	}

	if d.ActiveTab == TabHierarchy && x < float64(d.sidebarWidth) {
		dy := float64(rl.GetMouseWheelMove())
		if x < float64(d.sidebarWidth/2) {
			d.ScrollOffset -= dy * 20
			if d.ScrollOffset < 0 {
				d.ScrollOffset = 0
			}
		} else {
			d.InspectorScroll -= dy * 20
			if d.InspectorScroll < 0 {
				d.InspectorScroll = 0
			}
		}
	}
}

func (d *DebugOverlay) Draw(renderObjects []types.RenderObject, sceneWidth, sceneHeight int, renderScale, sceneOffsetX, sceneOffsetY float64, scalingMode string) {
	d.renderScale = renderScale
	d.sceneOffsetX = sceneOffsetX
	d.sceneOffsetY = sceneOffsetY

	sh := rl.GetScreenHeight()

	if d.cachedWidth != d.sidebarWidth || d.cachedHeight != sh {
		if d.bufferInitialized {
			rl.UnloadRenderTexture(d.uiBuffer)
		}
		d.uiBuffer = rl.LoadRenderTexture(int32(d.sidebarWidth), int32(sh))
		d.bufferInitialized = true
		d.cachedWidth = d.sidebarWidth
		d.cachedHeight = sh
	}

	rl.BeginTextureMode(d.uiBuffer)
	rl.ClearBackground(rl.Blank)

	// Draw Background
	rl.DrawRectangle(0, 0, int32(d.sidebarWidth), int32(sh), rl.NewColor(0, 0, 0, 200))

	// Draw Tabs
	d.drawTabs()

	// Draw Toggle
	d.drawBoundingBoxToggle()

	// Draw Content
	contentY := d.tabHeight + int(float64(d.tabHeight)*0.75)
	switch d.ActiveTab {
	case TabHierarchy:
		d.drawHierarchy(renderObjects, contentY, sh)
	case TabParticle:
		d.drawParticleInspector(renderObjects, contentY, d.mouseX, d.mouseY, d.clicked)
	case TabPerformance:
		d.drawPerformance(contentY, sceneWidth, sceneHeight, renderScale, scalingMode, d.mouseX, d.mouseY, d.clicked)
	}

	rl.EndTextureMode()

	// Draw bounding boxes for selected
	if d.SelectedObjectIndex >= 0 && d.SelectedObjectIndex < len(renderObjects) {
		d.drawSelectedBoundingBox(renderObjects[d.SelectedObjectIndex], renderScale, sceneOffsetX, sceneOffsetY)
	}

	// Draw bounding boxes for all
	if d.ShowBoundingBoxes {
		d.drawSceneBoundingBoxes(renderObjects, renderScale, sceneOffsetX, sceneOffsetY)
	}

	sourceRec := rl.NewRectangle(0, 0, float32(d.sidebarWidth), -float32(sh))
	destRec := rl.NewRectangle(0, 0, float32(d.sidebarWidth), float32(sh))
	rl.DrawTexturePro(d.uiBuffer.Texture, sourceRec, destRec, rl.NewVector2(0, 0), 0, rl.White)
}

func (d *DebugOverlay) drawTabs() {
	tabs := []string{"Hierarchy", "Particle", "Performance"}
	tabWidth := d.sidebarWidth / len(tabs)

	for i, name := range tabs {
		tab := DebugTab(i)
		color := rl.NewColor(100, 100, 100, 255)
		if d.ActiveTab == tab {
			color = rl.NewColor(150, 150, 150, 255)
		}

		x := int32(i * tabWidth)
		rl.DrawRectangle(x, 0, int32(tabWidth), int32(d.tabHeight), color)
		d.DrawText(name, x+10, int32(float64(d.tabHeight)*0.3), int32(d.fontHeight), rl.White)
	}
}

func (d *DebugOverlay) DrawText(text string, x, y int32, fontSize int32, color rl.Color) {
	if d.font.BaseSize > 0 {
		rl.DrawTextEx(d.font, text, rl.NewVector2(float32(x), float32(y)), float32(fontSize), 1, color)
	} else {
		rl.DrawText(text, x, y, fontSize, color)
	}
}
