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
			if !effect.Visible.GetBool() {
				continue
			}

			if effect.Name == "shake" || effect.Name == "breathe" || strings.Contains(effect.File, "shake") {
				amount := 0.05
				speed := 1.0

				if len(effect.Passes) > 0 {
					ps := effect.Passes[0]
					
					strength := ps.ConstantShaderValues.GetFloat("strength")
					if strength == 0 {
						strength = ps.ConstantShaderValues.GetFloat("Strength")
					}
					
					if strength != 0 {
						amount = strength
					} else {
						amt := ps.ConstantShaderValues.GetFloat("amount")
						if amt == 0 {
							amt = ps.ConstantShaderValues.GetFloat("Amount")
						}
						if amt != 0 {
							amount = amt
						} else if ps.ConstantValue != 0 {
							amount = ps.ConstantValue * 0.1
						}
					}

					spd := ps.ConstantShaderValues.GetFloat("speed")
					if spd == 0 {
						spd = ps.ConstantShaderValues.GetFloat("Speed")
					}
					if spd != 0 {
						speed = spd
					}
				}

				angle := totalTime * speed * math.Pi
				offset := math.Sin(angle) * amount * 50.0

				if effect.Name == "breathe" {
					offsets[i].Y += offset * 0.6
				} else {
					offsets[i].Y += offset
				}
			}
		}
	}
}
