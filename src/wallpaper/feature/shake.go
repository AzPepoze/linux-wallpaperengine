package feature

import (
	"math"
	"strings"

	"linux-wallpaperengine/src/wallpaper"
)

func UpdateShake(objects []wallpaper.Object, offsets []wallpaper.Vec2, totalTime float64) {
	for i := range objects {
		obj := &objects[i]
		for _, effect := range obj.Effects {
			if !effect.Visible.Value {
				continue
			}

			if effect.Name == "shake" || effect.Name == "breathe" || strings.Contains(effect.File, "shake") {
				amount := 0.05
				speed := 1.0
				
				if len(effect.Passes) > 0 {
					ps := effect.Passes[0]
					if ps.ConstantShaderValues.Strength.Value != 0 {
						amount = ps.ConstantShaderValues.Strength.Value
					} else if ps.ConstantShaderValues.Amount.Value != 0 {
						amount = ps.ConstantShaderValues.Amount.Value
					} else if ps.ConstantValue != 0 {
						amount = ps.ConstantValue * 0.1
					}
					
					if ps.ConstantShaderValues.Speed.Value != 0 {
						speed = ps.ConstantShaderValues.Speed.Value
					}
				}
				
				// Subtle movement: speed is frequency, amount is amplitude
				// Use a lower multiplier (e.g., 50) for a more natural look
				offset := math.Sin(totalTime*speed*2.0) * amount * 50
				offsets[i].Y += offset
			}
		}
	}
}
