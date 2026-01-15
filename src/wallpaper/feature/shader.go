package feature

import (
	"linux-wallpaperengine/src/wallpaper"
)

func ApplyShaderEffects(obj *wallpaper.Object, alpha *float64) {
	for _, effect := range obj.Effects {
		if !effect.Visible.Value {
			continue
		}

		// Handle basic shader-like properties that are stored in passes
		if effect.Name == "opacity" {
			if len(effect.Passes) > 0 {
				*alpha *= effect.Passes[0].ConstantValue
			} else {
				*alpha *= effect.Alpha.Value
			}
		}
	}
}
