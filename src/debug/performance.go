package debug

import (
	"fmt"
	"runtime"

	rl "github.com/gen2brain/raylib-go/raylib"
)

func (d *DebugOverlay) drawPerformance(startY int, sceneWidth, sceneHeight int, renderScale float64, scalingMode string, mx, my int, clicked bool) {
	ui := NewUIContext(10, startY, d.lineHeight, d.fontHeight, d.font, mx, my, clicked)

	// FPS
	ui.Label(fmt.Sprintf("FPS: %.1f", float64(rl.GetFPS())))

	// TPS (Ticks Per Second) - Raylib runs at FPS usually
	ui.Label(fmt.Sprintf("Frame Time: %.2f ms", rl.GetFrameTime()*1000))

	ui.Separator()

	// Memory Stats
	ui.Header("Memory Usage:")

	allocMB := float64(d.memStats.Alloc) / 1024 / 1024
	ui.IndentLabel(fmt.Sprintf("Allocated: %.2f MB", allocMB), 10)

	totalAllocMB := float64(d.memStats.TotalAlloc) / 1024 / 1024
	ui.IndentLabel(fmt.Sprintf("Total Alloc: %.2f MB", totalAllocMB), 10)

	sysMB := float64(d.memStats.Sys) / 1024 / 1024
	ui.IndentLabel(fmt.Sprintf("System: %.2f MB", sysMB), 10)

	heapAllocMB := float64(d.memStats.HeapAlloc) / 1024 / 1024
	ui.IndentLabel(fmt.Sprintf("Heap Alloc: %.2f MB", heapAllocMB), 10)

	heapSysMB := float64(d.memStats.HeapSys) / 1024 / 1024
	ui.IndentLabel(fmt.Sprintf("Heap System: %.2f MB", heapSysMB), 10)

	ui.Separator()

	// Garbage Collection
	ui.Header("Garbage Collection:")

	ui.IndentLabel(fmt.Sprintf("GC Runs: %d", d.memStats.NumGC), 10)

	lastPauseMicro := d.memStats.PauseNs[(d.memStats.NumGC+255)%256] / 1000
	ui.IndentLabel(fmt.Sprintf("Last Pause: %d µs", lastPauseMicro), 10)

	ui.Separator()

	// Goroutines
	ui.Label(fmt.Sprintf("Goroutines: %d", runtime.NumGoroutine()))

	ui.Separator()

	// GPU Info
	ui.Header("Graphics:")

	// Get graphics driver info
	graphicsDriver := "OpenGL"
	ui.IndentLabel(fmt.Sprintf("Backend: %s", graphicsDriver), 10)

	// Monitor resolution
	ui.IndentLabel(fmt.Sprintf("Monitor: %dx%d", d.monitorWidth, d.monitorHeight), 10)

	// Scene resolution
	if sceneWidth > 0 && sceneHeight > 0 {
		ui.IndentLabel(fmt.Sprintf("Scene: %dx%d", sceneWidth, sceneHeight), 10)
	}

	// Window size (actual render resolution)
	w, h := rl.GetScreenWidth(), rl.GetScreenHeight()
	ui.IndentLabel(fmt.Sprintf("Render: %dx%d", w, h), 10)

	// Render scale
	if renderScale != 1.0 {
		ui.IndentLabel(fmt.Sprintf("Render Scale: %.2fx (%s)", renderScale, scalingMode), 10)
	} else {
		ui.IndentLabel(fmt.Sprintf("Scaling Mode: %s", scalingMode), 10)
	}

	// UI Scale
	ui.IndentLabel(fmt.Sprintf("UI Scale: %.2fx", d.uiScale), 10)
}
