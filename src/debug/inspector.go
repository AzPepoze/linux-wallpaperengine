package debug

import (
	"fmt"

	"linux-wallpaperengine/src/types"

	"github.com/hajimehoshi/ebiten/v2"
)

func (d *DebugOverlay) drawInspector(screen *ebiten.Image, renderObj *types.RenderObject, startX, startY int, maxHeight int) {
	y := startY + int(d.InspectorScroll)
	x := startX + 10

	ui := NewUIContext(screen, x, y, d.lineHeight, d.fontHeight)

	obj := renderObj.Object

	// Title
	ui.Header("Inspector")
	ui.Separator()

	// Object Type
	objType := "Image"
	if obj.Particle != "" {
		objType = "Particle"
	} else if obj.Text.Value != "" || obj.Text.Script != "" {
		objType = "Text"
	} else if obj.Sound.Value != "" {
		objType = "Sound"
	}
	ui.Label(fmt.Sprintf("Type: %s", objType))

	// ID
	ui.Label(fmt.Sprintf("ID: %d", obj.ID))

	ui.Separator()

	// Properties
	ui.Header("Properties:")

	// Origin (Vec3)
	ui.IndentLabel(fmt.Sprintf("Origin: (%.1f, %.1f, %.1f)", obj.Origin.X, obj.Origin.Y, obj.Origin.Z), 5)

	// Scale (Vec3)
	ui.IndentLabel(fmt.Sprintf("Scale: (%.2f, %.2f, %.2f)", obj.Scale.X, obj.Scale.Y, obj.Scale.Z), 5)

	// Angles (Vec3)
	ui.IndentLabel(fmt.Sprintf("Angles: (%.2f, %.2f, %.2f)", obj.Angles.X, obj.Angles.Y, obj.Angles.Z), 5)

	// Alpha (float)
	ui.IndentLabel(fmt.Sprintf("Alpha: %.2f", obj.Alpha.Value), 5)

	// Brightness (float)
	ui.IndentLabel(fmt.Sprintf("Brightness: %.2f", obj.Brightness), 5)

	// Visible (bool) - clickable
	visibleStr := "true"
	if !obj.Visible.Value {
		visibleStr = "false"
	}

	ui.Checkbox(fmt.Sprintf("Visible: %s", visibleStr), obj.Visible.Value)

	// Size (Vec2) - if applicable
	if obj.Size.X > 0 || obj.Size.Y > 0 {
		ui.IndentLabel(fmt.Sprintf("Size: (%.1f, %.1f)", obj.Size.X, obj.Size.Y), 5)
	}

	// Color (string)
	if obj.Color != "" {
		ui.IndentLabel(fmt.Sprintf("Color: %s", obj.Color), 5)
	}

	// Effects count
	if len(obj.Effects) > 0 {
		ui.Separator()
		ui.Label(fmt.Sprintf("Effects: %d", len(obj.Effects)))

		for i, effect := range obj.Effects {
			if i >= 3 { // Limit display
				ui.IndentLabel(fmt.Sprintf("... and %d more", len(obj.Effects)-3), 5)
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
			ui.IndentLabel(fmt.Sprintf("[%s] %s", visStr, effectName), 5)
		}
	}

	// Particle-specific info
	if obj.Particle != "" {
		ui.Separator()
		ui.Label(fmt.Sprintf("Particle: %s", obj.Particle))

		// Show actual renderer type based on configuration
		if renderObj.ParticleSystem != nil {
			ps := renderObj.ParticleSystem
			rendererName := "default"
			if len(ps.Config.Renderer) > 0 {
				rendererName = ps.Config.Renderer[0].Name
			}

			useSpriteSheet := ps.Config.SequenceMultiplier > 1
			if rendererName == "sprite" && useSpriteSheet {
				ui.IndentLabel(fmt.Sprintf("Renderer: sprite (sheet %dx%d)",
					int(ps.Config.SequenceMultiplier*4), int(ps.Config.SequenceMultiplier*4)), 5)
			} else {
				ui.IndentLabel(fmt.Sprintf("Renderer: %s", rendererName), 5)
			}
		}

		if obj.InstanceOverride != nil {
			ui.IndentLabel("Overrides:", 5)
			if obj.InstanceOverride.Count.Value != 0 {
				ui.IndentLabel(fmt.Sprintf("Count: %.2f", obj.InstanceOverride.Count.Value), 10)
			}
			if obj.InstanceOverride.Size.Value != 0 {
				ui.IndentLabel(fmt.Sprintf("Size: %.2f", obj.InstanceOverride.Size.Value), 10)
			}
			if obj.InstanceOverride.Speed.Value != 0 {
				ui.IndentLabel(fmt.Sprintf("Speed: %.2f", obj.InstanceOverride.Speed.Value), 10)
			}
		}
	}
}