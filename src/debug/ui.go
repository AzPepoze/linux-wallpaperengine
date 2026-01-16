package debug

import (
	rl "github.com/gen2brain/raylib-go/raylib"
)

type UIContext struct {
	X, Y         int
	BaseX        int
	LineHeight   int
	FontHeight   int
	Font         rl.Font
	MouseX       int
	MouseY       int
	MouseClicked bool
}

func NewUIContext(x, y, lineHeight, fontHeight int, font rl.Font, mx, my int, clicked bool) *UIContext {
	return &UIContext{
		X:            x,
		Y:            y,
		BaseX:        x,
		LineHeight:   lineHeight,
		FontHeight:   fontHeight,
		Font:         font,
		MouseX:       mx,
		MouseY:       my,
		MouseClicked: clicked,
	}
}

func (ui *UIContext) drawText(text string, x, y int32, color rl.Color) {
	if ui.Font.BaseSize > 0 {
		rl.DrawTextEx(ui.Font, text, rl.NewVector2(float32(x), float32(y)), float32(ui.FontHeight), 1, color)
	} else {
		rl.DrawText(text, x, y, int32(ui.FontHeight), color)
	}
}

func (ui *UIContext) Label(text string) {
	ui.drawText(text, int32(ui.X), int32(ui.Y), rl.White)
	ui.Y += ui.LineHeight
}

func (ui *UIContext) IndentLabel(text string, indent int) {
	ui.drawText(text, int32(ui.X+indent), int32(ui.Y), rl.White)
	ui.Y += ui.LineHeight
}

func (ui *UIContext) Separator() {
	ui.Y += ui.LineHeight / 2
}

func (ui *UIContext) Header(text string) {
	ui.Label(text)
}

func (ui *UIContext) Checkbox(label string, checked bool) bool {
	boxSize := int(float64(ui.FontHeight) * 0.8)
	boxX := ui.X + 5
	boxY := ui.Y + 2

	changed := false
	if ui.MouseClicked &&
		ui.MouseX >= boxX && ui.MouseX <= boxX+boxSize+100 && // Add some width for label click
		ui.MouseY >= boxY && ui.MouseY <= boxY+boxSize {
		changed = true
	}

	rl.DrawRectangleLines(int32(boxX), int32(boxY), int32(boxSize), int32(boxSize), rl.NewColor(150, 150, 150, 255))

	if checked {
		rl.DrawRectangle(int32(boxX+2), int32(boxY+2), int32(boxSize-4), int32(boxSize-4), rl.NewColor(100, 255, 100, 255))
	}
	ui.drawText(label, int32(ui.X+5+boxSize+5), int32(ui.Y), rl.White)
	ui.Y += ui.LineHeight

	return changed
}
