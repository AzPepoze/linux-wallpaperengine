package debug

import (
	"fmt"

	"linux-wallpaperengine/src/types"

	"github.com/hajimehoshi/ebiten/v2"
)

func (d *DebugOverlay) drawParticleInspector(screen *ebiten.Image, renderObjects []types.RenderObject, startY int) {
	ui := NewUIContext(screen, 10, startY, d.lineHeight, d.fontHeight)

	// Summary
	totalParticles := 0
	activeSystems := 0

	for _, obj := range renderObjects {
		if obj.ParticleSystem != nil {
			activeSystems++
			totalParticles += len(obj.ParticleSystem.Particles)
		}
	}

	ui.Label(fmt.Sprintf("Total Particles: %d", totalParticles))
	ui.Label(fmt.Sprintf("Active Systems: %d", activeSystems))

	ui.Separator()
	ui.Header("Systems:")

	for _, obj := range renderObjects {
		if obj.ParticleSystem != nil {
			ps := obj.ParticleSystem
			text := fmt.Sprintf("- %s: %d", ps.Name, len(ps.Particles))
			ui.IndentLabel(text, 10)

			if d.SelectedObjectIndex != -1 && d.SelectedObjectIndex < len(renderObjects) && renderObjects[d.SelectedObjectIndex].Object == obj.Object {
				ui.IndentLabel(fmt.Sprintf("Rate: %.1f", ps.Config.Emitter[0].Rate.Value), 20)
			}
		}
	}
}