package debug

import (
	"fmt"
	"sort"

	"linux-wallpaperengine/src/types"
)

func (d *DebugOverlay) drawParticleInspector(renderObjects []types.RenderObject, startY int, mx, my int, clicked bool) {
	ui := NewUIContext(10, startY, d.lineHeight, d.fontHeight, d.font, mx, my, clicked)

	// Gather all particle systems
	var particleSystems []*types.RenderObject
	for i := range renderObjects {
		if renderObjects[i].ParticleSystem != nil {
			particleSystems = append(particleSystems, &renderObjects[i])
		}
	}

	ui.Header(fmt.Sprintf("Particle Systems (%d)", len(particleSystems)))
	ui.Separator()

	// Sort by name
	sort.Slice(particleSystems, func(i, j int) bool {
		return particleSystems[i].Object.Name < particleSystems[j].Object.Name
	})

	for _, ro := range particleSystems {
		ps := ro.ParticleSystem
		ui.Label(fmt.Sprintf("%s (%d particles)", ps.Name, len(ps.Particles)))
		
		// Basic stats
		activeEmitters := 0
		for _, e := range ps.Config.Emitter {
			if e.Rate.Value > 0 {
				activeEmitters++
			}
		}
		ui.IndentLabel(fmt.Sprintf("Emitters: %d (Active: %d)", len(ps.Config.Emitter), activeEmitters), 10)
		ui.IndentLabel(fmt.Sprintf("Operators: %d", len(ps.Config.Operator)), 10)
		
		// Draw some key properties from the first emitter if available
		if len(ps.Config.Emitter) > 0 {
			e := ps.Config.Emitter[0]
			rate := e.Rate.Value
			if ps.Override != nil && ps.Override.Rate.Value != 0 {
				rate *= ps.Override.Rate.Value
			}
			ui.IndentLabel(fmt.Sprintf("Rate: %.1f", rate), 20)
		}
		
		ui.Separator()
	}
}