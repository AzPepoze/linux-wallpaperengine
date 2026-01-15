package main

import (
	"fmt"
	"image/color"
	"log"
	"os"
	"path/filepath"
	"strings"
	"strconv"

	"github.com/hajimehoshi/ebiten/v2"
	"github.com/hajimehoshi/ebiten/v2/ebitenutil"
	"github.com/hajimehoshi/ebiten/v2/vector"
)

type Game struct {
	scene      Scene
	bgColor    color.RGBA
	debugImage *ebiten.Image
	imageCache map[string]*ebiten.Image
}

func parseColor(s string) color.RGBA {
	var r, g, b float64
	fmt.Sscanf(s, "%f %f %f", &r, &g, &b)
	return color.RGBA{uint8(r * 255), uint8(g * 255), uint8(b * 255), 255}
}

func parseVec2(v interface{}) (float64, float64) {
	s, ok := v.(string)
	if !ok {
		return 0, 0
	}
	parts := strings.Fields(s)
	if len(parts) < 2 {
		return 0, 0
	}
	x, _ := strconv.ParseFloat(parts[0], 64)
	y, _ := strconv.ParseFloat(parts[1], 64)
	return x, y
}

func (g *Game) Update() error {
	return nil
}

func (g *Game) Draw(screen *ebiten.Image) {
	screen.Fill(g.bgColor)

	if g.imageCache == nil {
		g.imageCache = make(map[string]*ebiten.Image)
	}

	for i, obj := range g.scene.Objects {
		isVisible := true
		if v, ok := obj.Visible.(bool); ok {
			isVisible = v
		}
		if !isVisible {
			continue
		}

		x, y := parseVec2(obj.Origin)
		sw, sh := parseVec2(obj.Size)

		if sw > 0 && sh > 0 {
			texPath := ""
			cleanName := strings.TrimSuffix(filepath.Base(obj.Image), ".json")
			if cleanName != "" && cleanName != "." {
				texPath = "tmp/materials/" + cleanName + ".tex"
			}
			
			// บังคับหา BG
			if strings.Contains(strings.ToLower(obj.Name), "bg") {
				texPath = "tmp/materials/gimai_seikatsu_BG_redraw.tex"
			}

			var img *ebiten.Image
			if texPath != "" {
				if _, err := os.Stat(texPath); err == nil {
					var cached bool
					img, cached = g.imageCache[texPath]
					if !cached {
						log.Printf("Game: Loading texture %s", texPath)
						img, _ = loadTexture(texPath)
						g.imageCache[texPath] = img
					}
				}
			}

			if img != nil {
				op := &ebiten.DrawImageOptions{}
				iw, ih := img.Bounds().Dx(), img.Bounds().Dy()
				op.GeoM.Scale(sw/float64(iw), sh/float64(ih))
				op.GeoM.Translate(x-sw/2, y-sh/2)
				screen.DrawImage(img, op)
			} else {
				// วาดกรอบสุ่มถ้าไม่มีรูป
				r := uint8((i * 50) % 255)
				g_c := uint8((i * 80) % 255)
				b := uint8((i * 110) % 255)
				vector.FillRect(screen, float32(x-sw/2), float32(y-sh/2), float32(sw), float32(sh), color.RGBA{r, g_c, b, 100}, false)
			}
		}
	}

	// Show Status
	ebitenutil.DebugPrint(screen, fmt.Sprintf("FPS: %.1f\nObjects: %d", ebiten.ActualFPS(), len(g.scene.Objects)))
}

func (g *Game) Layout(outsideWidth, outsideHeight int) (screenWidth, screenHeight int) {
	w := g.scene.General.OrthogonalProjection.Width
	h := g.scene.General.OrthogonalProjection.Height
	if w <= 0 || h <= 0 { return 1920, 1080 }
	return w, h
}