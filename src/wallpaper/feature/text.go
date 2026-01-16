package feature

import (
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"linux-wallpaperengine/src/utils"
	"linux-wallpaperengine/src/wallpaper"

	rl "github.com/gen2brain/raylib-go/raylib"
)

var fontCache = make(map[string]rl.Font)

func RenderText(object *wallpaper.Object, rt *rl.RenderTexture2D) {
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

	fontSize := float32(object.Pointsize.Value)
	if fontSize <= 0 {
		fontSize = 24
	}

	font := getFont(fontSize)

	// Measure text
	textSize := rl.MeasureTextEx(font, str, fontSize, 0)
	tw := textSize.X
	th := textSize.Y

	w := float32(rt.Texture.Width)
	h := float32(rt.Texture.Height)
	x, y := float32(0), float32(0)

	switch object.HorizontalAlign {
	case "center":
		x = (w - tw) / 2
	case "right":
		x = w - tw
	default:
		x = 0
	}

	switch object.VerticalAlign {
	case "center":
		y = (h - th) / 2
	case "bottom":
		y = h - th
	default:
		y = 0
	}

	rl.BeginTextureMode(*rt)
	rl.ClearBackground(rl.Blank)
	rl.DrawTextEx(font, str, rl.NewVector2(x, y), fontSize, 0, rl.White)
	rl.EndTextureMode()
}

func getFont(size float32) rl.Font {
	// Use size in cache key to ensure we get a face with the correct size
	cacheKey := "default_" + strings.ReplaceAll(strconv.FormatFloat(float64(size), 'f', 2, 64), ".", "_")
	if font, ok := fontCache[cacheKey]; ok {
		return font
	}

	fontPath := "assets/fonts/NotoSans-Regular.ttf"
	if _, err := os.Stat(fontPath); err != nil {
		files, _ := filepath.Glob("assets/fonts/*.ttf")
		if len(files) > 0 {
			fontPath = files[0]
		} else {
			utils.Warn("No fonts found in assets/fonts")
			return rl.GetFontDefault()
		}
	}

	font := rl.LoadFontEx(fontPath, int32(size), nil, 0)
	rl.SetTextureFilter(font.Texture, rl.FilterBilinear)

	fontCache[cacheKey] = font
	return font
}
