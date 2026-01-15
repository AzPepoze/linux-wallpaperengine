package feature

import (
	"image/color"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"linux-wallpaperengine/src/utils"
	"linux-wallpaperengine/src/wallpaper"

	"github.com/hajimehoshi/ebiten/v2"
	"github.com/hajimehoshi/ebiten/v2/text"
	"golang.org/x/image/font"
	"golang.org/x/image/font/opentype"
)

var fontCache = make(map[string]font.Face)

func RenderText(object *wallpaper.Object, img *ebiten.Image) {
	str := object.Text.Value
	if str == "" {
		return
	}

	if strings.Contains(object.Text.Script, "new Date()") {
		format := object.Text.ScriptProperties.Format.Value
		if format == "" {
			format = "2006/01/02 15:04:05"
		} else {
			format = strings.ReplaceAll(format, "yyyy", "2006")
			format = strings.ReplaceAll(format, "MM", "01")
			format = strings.ReplaceAll(format, "dd", "02")
			format = strings.ReplaceAll(format, "hh", "03")
			format = strings.ReplaceAll(format, "mm", "04")
			format = strings.ReplaceAll(format, "ss", "05")
		}
		str = time.Now().Format(format)
	}

	fontSize := object.Pointsize.Value
	if fontSize <= 0 {
		fontSize = 24
	}

	face := getFont(fontSize)
	if face == nil {
		return
	}

	bounds := text.BoundString(face, str)
	tw := bounds.Dx()
	th := bounds.Dy()

	w, h := img.Bounds().Dx(), img.Bounds().Dy()
	x, y := 0, 0

	switch object.HorizontalAlign {
	case "center":
		x = (w - tw) / 2
	case "right":
		x = w - tw
	default:
		x = 0
	}

	ascent := face.Metrics().Ascent.Floor()
	switch object.VerticalAlign {
	case "center":
		y = (h-th)/2 + ascent
	case "bottom":
		y = h - th + ascent
	default:
		y = ascent
	}

	text.Draw(img, str, face, x, y, color.White)
}

func getFont(size float64) font.Face {
	// Use size in cache key to ensure we get a face with the correct size
	cacheKey := "default_" + strings.ReplaceAll(strconv.FormatFloat(size, 'f', 2, 64), ".", "_")
	if face, ok := fontCache[cacheKey]; ok {
		return face
	}

	fontPath := "assets/fonts/NotoSans-Regular.ttf"
	if _, err := os.Stat(fontPath); err != nil {
		files, _ := filepath.Glob("assets/fonts/*.ttf")
		if len(files) > 0 {
			fontPath = files[0]
		} else {
			utils.Warn("No fonts found in assets/fonts")
			return nil
		}
	}

	fontData, err := os.ReadFile(fontPath)
	if err != nil {
		return nil
	}

	tt, err := opentype.Parse(fontData)
	if err != nil {
		return nil
	}

	face, err := opentype.NewFace(tt, &opentype.FaceOptions{
		Size:    size,
		DPI:     72,
		Hinting: font.HintingFull,
	})
	if err != nil {
		return nil
	}

	fontCache[cacheKey] = face
	return face
}
