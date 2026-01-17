package debug

import (
	"fmt"

	"linux-wallpaperengine/src/types"

	rl "github.com/gen2brain/raylib-go/raylib"
)

func (d *DebugOverlay) drawInspector(renderObj *types.RenderObject, startX, startY int, maxHeight int, mx, my int, clicked bool) {
	ui := NewUIContext(startX+10, startY-int(d.InspectorScroll), d.lineHeight, d.fontHeight, d.font, mx, my, clicked)

	// Scissor mode to handle scrolling and clipping
	rl.BeginScissorMode(int32(startX), int32(startY), int32(d.sidebarWidth/2), int32(maxHeight-startY))
	defer rl.EndScissorMode()

	obj := renderObj.Object

	ui.Header(fmt.Sprintf("Object: %s", obj.Name))

	objType := "Unknown"
	switch {
	case obj.Particle != "":
		objType = "Particle System"
	case obj.GetText() != "":
		objType = "Text"
	case obj.Image != "":
		objType = "Image"
	}
	ui.IndentLabel(fmt.Sprintf("Type: %s", objType), 10)

	ui.Separator()

	ui.Header("Transform:")
	ui.IndentLabel(fmt.Sprintf("Logical Pos: %.1f, %.1f", obj.Origin.X, obj.Origin.Y), 10)
	
	renderedX := d.sceneOffsetX + (obj.Origin.X+renderObj.Offset.X)*d.renderScale
	renderedY := d.sceneOffsetY + (obj.Origin.Y+renderObj.Offset.Y)*d.renderScale
	ui.IndentLabel(fmt.Sprintf("Rendered Pos: %.1f, %.1f", renderedX, renderedY), 10)
	
	ui.IndentLabel(fmt.Sprintf("Scale: %.2f, %.2f", obj.Scale.X, obj.Scale.Y), 10)
	ui.IndentLabel(fmt.Sprintf("Rot: %.1f", obj.Angles.Z), 10)

	ui.Separator()

	ui.Header("Visibility:")
	visible := obj.Visible.GetBool()
	if ui.Checkbox("Visible", visible) {
		obj.Visible.Value = !visible
	}
	ui.IndentLabel(fmt.Sprintf("Alpha: %.2f", obj.Alpha.GetFloat()), 10)

	if obj.GetText() != "" {
		ui.Separator()
		ui.Header("Text:")
		ui.IndentLabel(fmt.Sprintf("Content: %s", obj.Text.Value), 10)
		ui.IndentLabel(fmt.Sprintf("Size: %.1f", obj.Pointsize.GetFloat()), 10)
		ui.IndentLabel(fmt.Sprintf("Align: %s, %s", obj.HorizontalAlign, obj.VerticalAlign), 10)
	}

	if obj.Particle != "" {
		ui.Separator()
		ui.Header("Particle System:")
		
		if renderObj.ParticleSystem != nil {
			d.drawParticleDetails(ui, renderObj)
		} else {
			ui.IndentLabel(fmt.Sprintf("Config: %s", obj.Particle), 10)
			ui.IndentLabel("Status: Not Loaded", 10)
		}
	}

	if len(obj.Effects) > 0 {

		ui.Separator()

		ui.Header("Effects:")

		for _, effect := range obj.Effects {

			ui.IndentLabel(fmt.Sprintf("- %s", effect.Name), 10)

		}

	}

}
