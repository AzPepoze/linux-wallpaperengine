package debug

import (
	"image/color"

	"github.com/hajimehoshi/ebiten/v2"
	"github.com/hajimehoshi/ebiten/v2/ebitenutil"
	"github.com/hajimehoshi/ebiten/v2/vector"
)

type UIContext struct {
	Screen     *ebiten.Image
	X, Y       int
	BaseX      int
	LineHeight int
	FontHeight int
}

func NewUIContext(screen *ebiten.Image, x, y, lineHeight, fontHeight int) *UIContext {
	return &UIContext{
		Screen:     screen,
		X:          x,
		Y:          y,
		BaseX:      x,
		LineHeight: lineHeight,
		FontHeight: fontHeight,
	}
}

func (ui *UIContext) Label(text string) {
	ebitenutil.DebugPrintAt(ui.Screen, text, ui.X, ui.Y)
	ui.Y += ui.LineHeight
}

func (ui *UIContext) IndentLabel(text string, indent int) {
	ebitenutil.DebugPrintAt(ui.Screen, text, ui.X+indent, ui.Y)
	ui.Y += ui.LineHeight
}

func (ui *UIContext) Separator() {
	ui.Y += ui.LineHeight / 2
}

func (ui *UIContext) Header(text string) {
	ui.Label(text)
}

func (ui *UIContext) Checkbox(label string, checked bool) {
	boxSize := int(float64(ui.FontHeight) * 0.8)
	boxX := ui.X + 5
	boxY := ui.Y + 2
	vector.StrokeRect(ui.Screen, float32(boxX), float32(boxY), float32(boxSize), float32(boxSize), 1, color.RGBA{150, 150, 150, 255}, false)
	if checked {
		vector.FillRect(ui.Screen, float32(boxX+2), float32(boxY+2), float32(boxSize-4), float32(boxSize-4), color.RGBA{100, 255, 100, 255}, false)
	}
	ebitenutil.DebugPrintAt(ui.Screen, label, ui.X+5+boxSize+5, ui.Y)
	ui.Y += ui.LineHeight
}
