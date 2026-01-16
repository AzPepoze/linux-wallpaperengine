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
		d.drawParticleDetails(ui, ro)
	}
}

func (d *DebugOverlay) drawParticleDetails(ui *UIContext, ro *types.RenderObject) {
	ps := ro.ParticleSystem
	ui.Label(fmt.Sprintf("%s (%d particles)", ps.Name, len(ps.Particles)))
	
	// Paths
	ui.IndentLabel(fmt.Sprintf("Config: %s", ro.Object.Particle), 10)
	if ps.TextureName != "" {
		ui.IndentLabel(fmt.Sprintf("Texture: %s", ps.TextureName), 10)
	}

	if ps.Config.AnimationMode != "" {
		ui.IndentLabel(fmt.Sprintf("Anim Mode: %s", ps.Config.AnimationMode), 10)
	}

	// Rendering Details
	rendererName := "default"
	if len(ps.Config.Renderer) > 0 {
		rendererName = ps.Config.Renderer[0].Name
	}
	ui.IndentLabel(fmt.Sprintf("Renderer: %s", rendererName), 10)
	
	blendMode := "Alpha" // Raylib default in Draw
	ui.IndentLabel(fmt.Sprintf("Blend Mode: %s", blendMode), 10)

	if ps.Texture != nil {
		ui.IndentLabel(fmt.Sprintf("Texture: %dx%d", ps.Texture.Width, ps.Texture.Height), 10)
	}

	if ps.Config.SequenceMultiplier > 1 {
		gridSize := int(ps.Config.SequenceMultiplier * 4)
		ui.IndentLabel(fmt.Sprintf("Sprite Grid: %dx%d (multiplier: %.1f)", gridSize, gridSize, ps.Config.SequenceMultiplier), 10)
	}

	ui.Separator()

	// Emitters
	ui.Header(fmt.Sprintf("Emitters (%d):", len(ps.Config.Emitter)))
	for _, e := range ps.Config.Emitter {
		rate := e.Rate.Value
		if ps.Override != nil && ps.Override.Rate.Value != 0 {
			rate *= ps.Override.Rate.Value
		}
		ui.IndentLabel(fmt.Sprintf("- %s (Rate: %.1f, Mode: %d)", e.Name, rate, e.AudioProcessingMode), 10)
		if e.Name == "boxrandom" {
			ui.IndentLabel(fmt.Sprintf("  Dist: %v", e.DistanceMax), 20)
		}
	}

	ui.Separator()

	// Operators
	ui.Header(fmt.Sprintf("Operators (%d):", len(ps.Config.Operator)))
	for _, op := range ps.Config.Operator {
		ui.IndentLabel(fmt.Sprintf("- %s", op.Name), 10)
	}
	
	ui.Separator()
}