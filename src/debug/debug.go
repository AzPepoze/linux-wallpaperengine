package debug

import (
	"image/color"
	"math"
	"runtime"
	"time"

	"linux-wallpaperengine/src/types"

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

func (d *DebugOverlay) Draw(screen *ebiten.Image, renderObjects []types.RenderObject, sceneWidth, sceneHeight int, renderScale float64) {
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