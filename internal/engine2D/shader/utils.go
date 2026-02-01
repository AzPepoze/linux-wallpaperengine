package shader

import (
	"strings"

	rl "github.com/gen2brain/raylib-go/raylib"
)

var BlackTexture *rl.Texture2D

// InitDefaults initializes default textures and resources used by shaders.
func InitDefaults() {
	if BlackTexture == nil {
		img := rl.GenImageColor(1, 1, rl.Black)
		tex := rl.LoadTextureFromImage(img)
		rl.SetTextureWrap(tex, rl.TextureWrapRepeat)
		rl.UnloadImage(img)
		BlackTexture = &tex
	}
}

// getUniformName is a helper for debugging that attempts to reverse-map
// a uniform location to its source variable name.
func getUniformName(loc int32, pass *LoadedPass) string {
	for k := range pass.Constants {
		// This is a rough guess, as we don't store the exact mapping.
		// But valid for most params.
		if strings.Contains(strings.ToLower(k), "speed") {
			return k
		}
	}
	return "?"
}
