package debug

import (
	"fmt"
	"sort"

	"linux-wallpaperengine/src/types"

	rl "github.com/gen2brain/raylib-go/raylib"
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
	
	blendModeStr := "Unknown"
	switch ps.BlendMode {
	case rl.BlendAlpha:
		blendModeStr = "Alpha"
	case rl.BlendAdditive:
		blendModeStr = "Additive"
	case rl.BlendMultiplied:
		blendModeStr = "Multiplied"
	case rl.BlendAddColors:
		blendModeStr = "AddColors"
	case rl.BlendSubtractColors:
		blendModeStr = "SubtractColors"
	}
	ui.IndentLabel(fmt.Sprintf("Blend Mode: %s", blendModeStr), 10)

	if ps.Texture != nil {
		ui.IndentLabel(fmt.Sprintf("Texture: %dx%d", ps.Texture.Width, ps.Texture.Height), 10)
	}

	gridX, gridY := 1, 1
	if ps.TexInfo != nil && len(ps.TexInfo.SpriteSheetSequences) > 0 {
		seq := ps.TexInfo.SpriteSheetSequences[0]
		if seq.Width > 0 && seq.Height > 0 && ps.Texture != nil {
			gridX = int(ps.Texture.Width) / seq.Width
			gridY = int(ps.Texture.Height) / seq.Height
			ui.IndentLabel(fmt.Sprintf("Sprite Grid: %dx%d (TexInfo)", gridX, gridY), 10)
		}
	} else if ps.Config.SequenceMultiplier > 1 {
		val := int(ps.Config.SequenceMultiplier * 4)
		if val < 1 {
			val = 1
		}
		gridX = val
		gridY = val
		ui.IndentLabel(fmt.Sprintf("Sprite Grid: %dx%d (multiplier: %.1f)", gridX, gridY, ps.Config.SequenceMultiplier), 10)
	} else if len(ps.Particles) > 0 {
		p := ps.Particles[0]
		if p.GridX > 0 && p.GridY > 0 {
			gridX = p.GridX
			gridY = p.GridY
			ui.IndentLabel(fmt.Sprintf("Sprite Grid: %dx%d (inferred)", gridX, gridY), 10)
		}
	}

	if ps.Texture != nil && (gridX > 1 || gridY > 1) {
		spriteW := int(ps.Texture.Width) / gridX
		spriteH := int(ps.Texture.Height) / gridY
		ui.IndentLabel(fmt.Sprintf("Sprite Size: %dx%d", spriteW, spriteH), 10)
	}

	if len(ps.Particles) > 0 && ps.Texture != nil {
		minW, maxW := 999999.0, 0.0
		minH, maxH := 999999.0, 0.0

		objScaleX := ro.Object.Scale.X
		objScaleY := ro.Object.Scale.Y

		for _, p := range ps.Particles {
			currGridX, currGridY := gridX, gridY
			if p.GridX > 0 {
				currGridX = p.GridX
			}
			if p.GridY > 0 {
				currGridY = p.GridY
			}

			pBaseW := float64(ps.Texture.Width) / float64(currGridX)
			pBaseH := float64(ps.Texture.Height) / float64(currGridY)

			scale := p.Size / 100.0
			w := pBaseW * scale * objScaleX
			h := pBaseH * scale * objScaleY

			if w < minW {
				minW = w
			}
			if w > maxW {
				maxW = w
			}
			if h < minH {
				minH = h
			}
			if h > maxH {
				maxH = h
			}
		}

		if minW != 999999.0 {
			ui.IndentLabel(fmt.Sprintf("Render Size: %.0f-%.0f x %.0f-%.0f", minW, maxW, minH, maxH), 10)
		}
	}

	ui.Separator()

	// Emitters
	ui.Header(fmt.Sprintf("Emitters (%d):", len(ps.Config.Emitter)))
	for _, e := range ps.Config.Emitter {
		var rate float64
		switch v := e.Rate.(type) {
		case float64:
			rate = v
		case map[string]interface{}:
			if val, ok := v["value"].(float64); ok {
				rate = val
			}
		}
		
		if ps.Override != nil && ps.Override.Rate.GetFloat() != 0 {
			rate *= ps.Override.Rate.GetFloat()
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