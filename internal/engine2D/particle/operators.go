package particle

import (
	"math"
	"math/rand"
)

// applyOperators applies configured operators to a particle during update.
func (ps *ParticleSystem) applyOperators(particle *Particle, dt float64) {
	hasAlphaFade := false

	for _, op := range ps.Config.Operator {
		if op.Name == "alphafade" {
			hasAlphaFade = true
			break
		}
	}

	baseAlpha := particle.InitialAlpha
	fadeMultiplier := 1.0
	oscillateMultiplier := 1.0

	if !hasAlphaFade {
		ageRatio := (particle.MaxLife - particle.Life) / particle.MaxLife

		if ageRatio < 0.1 {
			fadeMultiplier = ageRatio / 0.1
		}

		lifeRemaining := particle.Life / particle.MaxLife
		if lifeRemaining < 0.2 {
			fadeOutMultiplier := lifeRemaining / 0.2
			fadeMultiplier = math.Min(fadeMultiplier, fadeOutMultiplier)
		}
	}

	for _, op := range ps.Config.Operator {
		switch op.Name {
		case "movement":
			if op.Gravity != nil {
				switch gravity := op.Gravity.(type) {
				case string:
					gx, gy, gz := ParseVec3String(gravity)
					particle.Velocity.X += gx * dt
					particle.Velocity.Y += gy * dt
					particle.Velocity.Z += gz * dt
				case float64:
					particle.Velocity.Y += gravity * dt
				case map[string]interface{}:
					if val, ok := gravity["value"].(float64); ok {
						particle.Velocity.Y += val * dt
					}
				}
			}

			dragVal := GetFloatFromInterface(op.Drag)
			if dragVal > 0 {
				dragFactor := 1.0 - (dragVal * dt)
				if dragFactor < 0 {
					dragFactor = 0
				}
				particle.Velocity.X *= dragFactor
				particle.Velocity.Y *= dragFactor
				particle.Velocity.Z *= dragFactor
			}

		case "alphafade":
			ageRatio := (particle.MaxLife - particle.Life) / particle.MaxLife
			fadeMultiplier = 1.0

			if op.FadeInTime > 0 && ageRatio < op.FadeInTime {
				fadeMultiplier = ageRatio / op.FadeInTime
			}

			if op.FadeOutTime > 0 {
				fadeStartAge := 1.0 - op.FadeOutTime
				if ageRatio > fadeStartAge {
					fadeProgress := (ageRatio - fadeStartAge) / op.FadeOutTime
					fadeOutMultiplier := 1.0 - fadeProgress
					fadeMultiplier = math.Min(fadeMultiplier, fadeOutMultiplier)
				}
			} else {
				lifeRemaining := particle.Life / particle.MaxLife
				if lifeRemaining < 0.2 {
					fadeOutMultiplier := lifeRemaining / 0.2
					fadeMultiplier = math.Min(fadeMultiplier, fadeOutMultiplier)
				}
			}

		case "turbulence":
			if op.SpeedMax > 0 {
				timeScale := op.TimeScale
				if timeScale == 0 {
					timeScale = 1
				}

				scale := op.Scale
				if scale == 0 {
					scale = 1
				}

				time := ps.GlobalTime * timeScale
				noiseX := math.Sin(time*0.7+particle.Position.X*scale) * math.Cos(time*0.3)
				noiseY := math.Cos(time*0.5+particle.Position.Y*scale) * math.Sin(time*0.8)

				speed := op.SpeedMin + rand.Float64()*(op.SpeedMax-op.SpeedMin)
				particle.Velocity.X += noiseX * speed * dt
				particle.Velocity.Y += noiseY * speed * dt
			}

		case "controlpointattract":
			if op.ControlPoint >= 0 && op.ControlPoint < len(ps.ControlPts) {
				cp := ps.ControlPts[op.ControlPoint]
				dx := cp.X - particle.Position.X
				dy := cp.Y - particle.Position.Y

				distSq := dx*dx + dy*dy
				threshold := op.Threshold
				if threshold <= 0 {
					threshold = 100
				}

				if distSq < threshold*threshold && distSq > 1 {
					dist := math.Sqrt(distSq)
					strength := op.Scale / distSq

					particle.Velocity.X += (dx / dist) * strength * dt
					particle.Velocity.Y += (dy / dist) * strength * dt
				}
			}

		case "colorchange":
			age := (particle.MaxLife - particle.Life) / particle.MaxLife
			if age >= op.StartTime && age <= op.EndTime {
				progress := (age - op.StartTime) / (op.EndTime - op.StartTime)

				startColor := GetVec3OrFloat(op.StartValue)
				endColor := GetVec3OrFloat(op.EndValue)

				particle.Color.X = startColor.X + (endColor.X-startColor.X)*progress
				particle.Color.Y = startColor.Y + (endColor.Y-startColor.Y)*progress
				particle.Color.Z = startColor.Z + (endColor.Z-startColor.Z)*progress
			}

		case "oscillateposition":
			time := ps.GlobalTime - particle.SpawnTime

			freq := op.FrequencyMax
			if op.FrequencyMin > 0 && op.FrequencyMax > op.FrequencyMin {
				t := (math.Sin(particle.RandomValue) + 1.0) / 2.0
				freq = op.FrequencyMin + t*(op.FrequencyMax-op.FrequencyMin)
			}

			if freq == 0 {
				freq = 1.0
			}

			phaseX := time*freq*math.Pi*2.0 + particle.RandomValue
			phaseY := time*freq*math.Pi*2.0 + particle.RandomValue + 1.0

			particle.Position.X += math.Sin(phaseX) * op.ScaleMax * dt
			particle.Position.Y += math.Cos(phaseY) * op.ScaleMax * dt

		case "sizechange":
			age := (particle.MaxLife - particle.Life) / particle.MaxLife
			startSize := GetFloatFromInterface(op.StartValue)
			endSize := GetFloatFromInterface(op.EndValue)

			if startSize == 0 {
				startSize = 1.0
			}
			if endSize == 0 {
				endSize = 1.0
			}

			currentScale := startSize + (endSize-startSize)*age
			particle.Size = particle.InitialSize * currentScale

		case "oscillatealpha":
			frequency := op.FrequencyMax
			if op.FrequencyMin > 0 && op.FrequencyMax > op.FrequencyMin {
				seed := math.Sin(particle.SpawnTime)
				frequency = op.FrequencyMin + math.Abs(seed)*(op.FrequencyMax-op.FrequencyMin)
			}

			if frequency == 0 {
				frequency = 1
			}

			scaleMin := op.ScaleMin
			scaleMax := op.ScaleMax
			if scaleMax == 0 {
				scaleMax = 1
			}

			age := ps.GlobalTime - particle.SpawnTime
			oscillation := (math.Sin(age*frequency*math.Pi*2) + 1.0) / 2.0
			oscillateMultiplier = scaleMin + oscillation*(scaleMax-scaleMin)
		}
	}

	particle.Alpha = baseAlpha * fadeMultiplier * oscillateMultiplier
}
