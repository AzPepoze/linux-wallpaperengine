package main

import (
	"fmt"
	"image/color"
	"math"
	"path/filepath"
	"strings"

	"github.com/hajimehoshi/ebiten/v2"
	"github.com/hajimehoshi/ebiten/v2/ebitenutil"
)

type RenderObject struct {
	*Object
	Image *ebiten.Image
}

type Game struct {
	scene         Scene
	bgColor       color.RGBA
	renderObjects []RenderObject
}

func NewGame(scene Scene) *Game {
	r, g, b := parseColor(scene.General.ClearColor)
	game := &Game{
		scene:   scene,
		bgColor: color.RGBA{uint8(r * 255), uint8(g * 255), uint8(b * 255), 255},
	}

	for i := range scene.Objects {
		obj := &scene.Objects[i]
		
		cleanName := strings.TrimSuffix(filepath.Base(obj.Image), ".json")
		if cleanName == "" || cleanName == "." {
			cleanName = obj.Name
		}
		
		texPath := filepath.Join("tmp/materials", cleanName+".tex")
		if strings.Contains(strings.ToLower(obj.Name), "bg") {
			texPath = "tmp/materials/gimai_seikatsu_BG_redraw.tex"
		}

		var img *ebiten.Image
		if imgData, err := loadTexture(texPath); err == nil {
			img = imgData
		}

		game.renderObjects = append(game.renderObjects, RenderObject{
			Object: obj,
			Image:  img,
		})
	}

	return game
}

func (g *Game) Update() error {
	return nil
}

func (g *Game) Draw(screen *ebiten.Image) {
	screen.Fill(g.bgColor)

	for _, ro := range g.renderObjects {
		if !ro.Visible || ro.Image == nil || ro.Alpha <= 0 {
			continue
		}

		sw, sh := ro.Size.X, ro.Size.Y
		if sw <= 0 || sh <= 0 {
			continue
		}

		op := &ebiten.DrawImageOptions{}
		
		// Alpha
		op.ColorScale.ScaleAlpha(float32(ro.Alpha))

		iw, ih := ro.Image.Bounds().Dx(), ro.Image.Bounds().Dy()

		// 1. Center the image for rotation
		op.GeoM.Translate(-float64(iw)/2, -float64(ih)/2)

		// 2. Scale to target size (taking Object.Scale into account)
		finalScaleX := (sw / float64(iw)) * ro.Scale.X
		finalScaleY := (sh / float64(ih)) * ro.Scale.Y
		op.GeoM.Scale(finalScaleX, finalScaleY)

		// 3. Rotate (Wallpaper Engine uses degrees for Z rotation in many cases, or radians)
		// Based on scene.json, it looks like degrees. Convert to radians.
		if ro.Angles.Z != 0 {
			radians := ro.Angles.Z * (math.Pi / 180.0)
			op.GeoM.Rotate(radians)
		}

		// 4. Move to final position
		op.GeoM.Translate(ro.Origin.X, ro.Origin.Y)

		screen.DrawImage(ro.Image, op)
	}

	ebitenutil.DebugPrint(screen, fmt.Sprintf("FPS: %.1f\nObjects: %d", ebiten.ActualFPS(), len(g.scene.Objects)))
}

func (g *Game) Layout(outsideWidth, outsideHeight int) (screenWidth, screenHeight int) {
	w := g.scene.General.OrthogonalProjection.Width
	h := g.scene.General.OrthogonalProjection.Height
	if w <= 0 || h <= 0 {
		return 1920, 1080
	}
	return w, h
}