package debug

import (
	"fmt"
	"runtime"

	"linux-wallpaperengine/src/utils"

	rl "github.com/gen2brain/raylib-go/raylib"
)

func (d *DebugOverlay) drawPerformance(startY int, sceneWidth, sceneHeight int, renderScale float64, scalingMode string, mx, my int, clicked bool) {
	ui := NewUIContext(10, startY, d.lineHeight, d.fontHeight, d.font, mx, my, clicked)

	// FPS & Timing
	ui.Header("Timing:")
	ui.IndentLabel(fmt.Sprintf("FPS: %.1f", float64(rl.GetFPS())), 10)
	ui.IndentLabel(fmt.Sprintf("Frame Time: %.2f ms", rl.GetFrameTime()*1000), 10)
	
	monitor := rl.GetCurrentMonitor()
	ui.IndentLabel(fmt.Sprintf("Refresh Rate: %d Hz", rl.GetMonitorRefreshRate(monitor)), 10)

	ui.Separator()

	// Memory Stats
	ui.Header("Memory Usage:")

	allocMB := float64(d.memStats.Alloc) / 1024 / 1024
	ui.IndentLabel(fmt.Sprintf("Allocated: %.2f MB", allocMB), 10)

	heapAllocMB := float64(d.memStats.HeapAlloc) / 1024 / 1024
	ui.IndentLabel(fmt.Sprintf("Heap Alloc: %.2f MB", heapAllocMB), 10)

	sysMB := float64(d.memStats.Sys) / 1024 / 1024
	ui.IndentLabel(fmt.Sprintf("Process Total: %.2f MB", sysMB), 10)

	ui.Separator()

	// System Info
	ui.Header("System:")
	ui.IndentLabel(fmt.Sprintf("Cores: %d", runtime.NumCPU()), 10)
	ui.IndentLabel(fmt.Sprintf("Goroutines: %d", runtime.NumGoroutine()), 10)
	ui.IndentLabel(fmt.Sprintf("OS/Arch: %s/%s", runtime.GOOS, runtime.GOARCH), 10)

	ui.Separator()

	// GPU Info
	ui.Header("Graphics:")

	if utils.GPURenderer != "" {
		ui.IndentLabel(fmt.Sprintf("GPU: %s", utils.GPURenderer), 10)
		ui.IndentLabel(fmt.Sprintf("Vendor: %s", utils.GPUVendor), 10)
		ui.IndentLabel(fmt.Sprintf("Version: %s", utils.GLVersion), 10)
	} else {
		ui.IndentLabel("Backend: OpenGL", 10)
	}

	// Get graphics driver info
	ui.IndentLabel(fmt.Sprintf("Monitor: %s", rl.GetMonitorName(monitor)), 10)
	ui.IndentLabel(fmt.Sprintf("Monitor Native: %dx%d", d.monitorWidth, d.monitorHeight), 10)

	// Window size (actual render resolution)
	w, h := rl.GetScreenWidth(), rl.GetScreenHeight()
	ui.IndentLabel(fmt.Sprintf("Window: %dx%d", w, h), 10)

	// Fullscreen status
	fs := "No"
	if rl.IsWindowFullscreen() {
		fs = "Yes"
	}
	ui.IndentLabel(fmt.Sprintf("Fullscreen: %s", fs), 10)

	// Scene resolution
	if sceneWidth > 0 && sceneHeight > 0 {
		ui.IndentLabel(fmt.Sprintf("Scene Size: %dx%d", sceneWidth, sceneHeight), 10)
	}

	// Render scale
	if renderScale != 1.0 {
		ui.IndentLabel(fmt.Sprintf("Render Scale: %.2fx (%s)", renderScale, scalingMode), 10)
	} else {
		ui.IndentLabel(fmt.Sprintf("Scaling Mode: %s", scalingMode), 10)
	}

	ui.IndentLabel(fmt.Sprintf("UI Scale: %.2fx", d.uiScale), 10)
}
