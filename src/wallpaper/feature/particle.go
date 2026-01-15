package feature

import (
	"image/color"
	"math"
	"math/rand"

	"linux-wallpaperengine/src/wallpaper"

	"github.com/hajimehoshi/ebiten/v2"
)

type Particle struct {
	Position     wallpaper.Vec3
	Velocity     wallpaper.Vec3
	Color        wallpaper.Vec3
	Life         float64
	MaxLife      float64
	Alpha        float64
	InitialAlpha float64
	Rotation     float64
	AngularVel   float64
	Size         float64
	InitialSize  float64
	SpawnTime    float64
}

type ParticleSystem struct {
	Name       string
	Config     wallpaper.ParticleJSON
	Texture    *ebiten.Image
	Particles  []*Particle
	Timer      float64
	GlobalTime float64
	Override   *wallpaper.InstanceOverride
	ControlPts []wallpaper.Vec3
	MousePos   wallpaper.Vec3
}

func NewParticleSystem(name string, config wallpaper.ParticleJSON, texture *ebiten.Image, override *wallpaper.InstanceOverride) *ParticleSystem {
	ps := &ParticleSystem{
		Name:       name,
		Config:     config,
		Texture:    texture,
		Override:   override,
		ControlPts: make([]wallpaper.Vec3, 8),
	}

	// Initialize control points
	for i := range ps.ControlPts {
		ps.ControlPts[i] = wallpaper.Vec3{X: 0, Y: 0, Z: 0}
	}

	return ps
}

func (ps *ParticleSystem) Update(dt float64) {
	ps.GlobalTime += dt

	if len(ps.Config.Emitter) == 0 {
		return
	}

	// Update control points
	for i, cp := range ps.Config.ControlPoint {
		if i < len(ps.ControlPts) {
			if cp.LockToPointer {
				ps.ControlPts[i] = ps.MousePos
			} else {
				ps.ControlPts[i] = cp.Offset
			}
		}
	}

	maxCount := ps.Config.MaxCount
	if maxCount <= 0 {
		maxCount = 100
	}

	// Apply instance override count multiplier
	countMultiplier := 1.0
	if ps.Override != nil && ps.Override.Count.Value != 0 {
		countMultiplier = ps.Override.Count.Value
	}
	maxCount = int(float64(maxCount) * countMultiplier)

	// Emit particles
	for _, emitter := range ps.Config.Emitter {
		rate := emitter.Rate.Value
		if ps.Override != nil && ps.Override.Rate.Value != 0 {
			rate *= ps.Override.Rate.Value
		}

		if rate > 0 {
			ps.Timer += dt
			spawnInterval := 1.0 / rate
			for ps.Timer >= spawnInterval && len(ps.Particles) < maxCount {
				ps.spawnParticle(emitter)
				ps.Timer -= spawnInterval
			}
		}
	}

	// Update existing particles
	for i := 0; i < len(ps.Particles); i++ {
		particle := ps.Particles[i]
		particle.Life -= dt

		if particle.Life <= 0 {
			ps.Particles = append(ps.Particles[:i], ps.Particles[i+1:]...)
			i--
			continue
		}

		// Apply operators
		ps.applyOperators(particle, dt)

		// Update position
		particle.Position.X += particle.Velocity.X * dt
		particle.Position.Y += particle.Velocity.Y * dt
		particle.Position.Z += particle.Velocity.Z * dt

		// Update rotation
		particle.Rotation += particle.AngularVel * dt
	}
}

func (ps *ParticleSystem) applyOperators(particle *Particle, dt float64) {
	hasAlphaFade := false

	// Check what alpha operators exist
	for _, op := range ps.Config.Operator {
		if op.Name == "alphafade" {
			hasAlphaFade = true
			break
		}
	}

	// Start with initial alpha
	baseAlpha := particle.InitialAlpha
	fadeMultiplier := 1.0
	oscillateMultiplier := 1.0

	// Apply default fade if no alphafade operator
	if !hasAlphaFade {
		ageRatio := (particle.MaxLife - particle.Life) / particle.MaxLife

		// Default fade in (first 10% of life)
		if ageRatio < 0.1 {
			fadeMultiplier = ageRatio / 0.1
		}

		// Default fade out (last 20% of life)
		lifeRemaining := particle.Life / particle.MaxLife
		if lifeRemaining < 0.2 {
			fadeOutMultiplier := lifeRemaining / 0.2
			fadeMultiplier = math.Min(fadeMultiplier, fadeOutMultiplier)
		}
	}

	for _, op := range ps.Config.Operator {
		switch op.Name {
		case "movement":
			// Apply gravity
			if op.Gravity != nil {
				switch gravity := op.Gravity.(type) {
				case string:
					gx, gy, gz := parseVec3String(gravity)
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

			// Apply drag
			if op.Drag.Value > 0 {
				dragFactor := 1.0 - (op.Drag.Value * dt)
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

			// Fade in at start of life
			if op.FadeInTime > 0 && ageRatio < op.FadeInTime {
				fadeMultiplier = ageRatio / op.FadeInTime
			}

			// Fade out at end of life
			// fadeouttime represents the portion of lifetime over which to fade out
			// e.g., 0.9 means fade out starts at age 0.1 (1.0 - 0.9) and ends at age 1.0
			if op.FadeOutTime > 0 {
				fadeStartAge := 1.0 - op.FadeOutTime
				if ageRatio > fadeStartAge {
					// Progress through the fade-out period (0 at start, 1 at end)
					fadeProgress := (ageRatio - fadeStartAge) / op.FadeOutTime
					fadeOutMultiplier := 1.0 - fadeProgress
					fadeMultiplier = math.Min(fadeMultiplier, fadeOutMultiplier)
				}
			} else {
				// Default fade out in last 20% of life if not specified
				lifeRemaining := particle.Life / particle.MaxLife
				if lifeRemaining < 0.2 {
					fadeOutMultiplier := lifeRemaining / 0.2
					fadeMultiplier = math.Min(fadeMultiplier, fadeOutMultiplier)
				}
			}

		case "angularmovement":
			// Rotation is handled by angular velocity set in initializers

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

				sr, sg, sb := wallpaper.ParseColor(op.StartValue)
				er, eg, eb := wallpaper.ParseColor(op.EndValue)

				particle.Color.X = sr + (er-sr)*progress
				particle.Color.Y = sg + (eg-sg)*progress
				particle.Color.Z = sb + (eb-sb)*progress
			}

		case "oscillatealpha":
			frequency := op.FrequencyMax
			if op.FrequencyMin > 0 && op.FrequencyMax > op.FrequencyMin {
				// Random frequency per particle (we'll use spawn time as seed)
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
			oscillation := (math.Sin(age*frequency*math.Pi*2) + 1.0) / 2.0 // 0 to 1
			oscillateMultiplier = scaleMin + oscillation*(scaleMax-scaleMin)
		}
	}

	// Apply all alpha modifiers together
	particle.Alpha = baseAlpha * fadeMultiplier * oscillateMultiplier
}

func parseVec3String(s string) (float64, float64, float64) {
	return wallpaper.ParseColor(s)
}

func (ps *ParticleSystem) spawnParticle(emitter wallpaper.ParticleEmitter) {
	particle := &Particle{
		Position:     wallpaper.Vec3{X: 0, Y: 0, Z: 0},
		Velocity:     wallpaper.Vec3{X: 0, Y: 0, Z: 0},
		Color:        wallpaper.Vec3{X: 1, Y: 1, Z: 1},
		Life:         1.0,
		MaxLife:      1.0,
		Alpha:        1.0,
		InitialAlpha: 1.0,
		Rotation:     0,
		AngularVel:   0,
		Size:         1.0,
		InitialSize:  1.0,
		SpawnTime:    ps.GlobalTime,
	}

	// Set origin from emitter
	particle.Position = emitter.Origin

	// Apply emitter positioning
	switch emitter.Name {
	case "boxrandom":
		distMax := getVec3OrFloat(emitter.DistanceMax)
		distMin := getVec3OrFloat(emitter.DistanceMin)

		// Add minimal randomness if both min and max are 0 to prevent stacking
		if distMax.X == 0 && distMin.X == 0 {
			distMax.X = 1.0
		}
		if distMax.Y == 0 && distMin.Y == 0 {
			distMax.Y = 1.0
		}
		if distMax.Z == 0 && distMin.Z == 0 {
			distMax.Z = 1.0
		}

		particle.Position.X += (rand.Float64()*2-1)*(distMax.X-distMin.X)/2 + distMin.X
		particle.Position.Y += (rand.Float64()*2-1)*(distMax.Y-distMin.Y)/2 + distMin.Y
		particle.Position.Z += (rand.Float64()*2-1)*(distMax.Z-distMin.Z)/2 + distMin.Z

	case "sphererandom":
		distMax := getFloatFromInterface(emitter.DistanceMax)
		distMin := getFloatFromInterface(emitter.DistanceMin)

		// If both are 0, add minimal randomness to prevent stacking
		if distMax == 0 && distMin == 0 {
			distMax = 1.0 // Small randomness to prevent particle overlap
		} else if distMax == 0 {
			distMax = 100
		}

		// Random point in sphere
		angle := rand.Float64() * math.Pi * 2
		elevation := rand.Float64()*math.Pi - math.Pi/2
		radius := distMin + rand.Float64()*(distMax-distMin)

		particle.Position.X += math.Cos(elevation) * math.Cos(angle) * radius
		particle.Position.Y += math.Cos(elevation) * math.Sin(angle) * radius
		particle.Position.Z += math.Sin(elevation) * radius
	}

	// Apply initializers
	for _, init := range ps.Config.Initializer {
		ps.applyInitializer(particle, init)
	}

	// Apply instance overrides
	if ps.Override != nil {
		if ps.Override.Lifetime.Value != 0 {
			particle.MaxLife *= ps.Override.Lifetime.Value
			particle.Life = particle.MaxLife
		}
		if ps.Override.Alpha.Value != 0 {
			particle.Alpha = ps.Override.Alpha.Value
			particle.InitialAlpha = ps.Override.Alpha.Value
		}
		if ps.Override.Size.Value != 0 {
			particle.Size *= ps.Override.Size.Value
			particle.InitialSize = particle.Size
		}
		if ps.Override.Speed.Value != 0 {
			particle.Velocity.X *= ps.Override.Speed.Value
			particle.Velocity.Y *= ps.Override.Speed.Value
			particle.Velocity.Z *= ps.Override.Speed.Value
		}
		// Apply color override
		if ps.Override.ColorN != "" {
			r, g, b := wallpaper.ParseColor(ps.Override.ColorN)
			particle.Color = wallpaper.Vec3{X: r, Y: g, Z: b}
		}
	}

	ps.Particles = append(ps.Particles, particle)
}

func (ps *ParticleSystem) applyInitializer(particle *Particle, init wallpaper.ParticleInitializer) {
	switch init.Name {
	case "lifetimerandom":
		minLife := getFloatFromInterface(init.Min)
		maxLife := getFloatFromInterface(init.Max)
		particle.MaxLife = minLife + rand.Float64()*(maxLife-minLife)
		particle.Life = particle.MaxLife

	case "sizerandom", "size_random":
		minSize := getFloatFromInterface(init.Min)
		maxSize := getFloatFromInterface(init.Max)

		// Apply exponent if specified (for non-linear distribution)
		if init.Exponent != 0 {
			t := math.Pow(rand.Float64(), init.Exponent)
			particle.Size = minSize + t*(maxSize-minSize)
		} else {
			particle.Size = minSize + rand.Float64()*(maxSize-minSize)
		}
		particle.InitialSize = particle.Size

	case "velocityrandom", "velocity_random":
		minVel := getVec3FromInterface(init.Min)
		maxVel := getVec3FromInterface(init.Max)

		particle.Velocity.X = minVel.X + rand.Float64()*(maxVel.X-minVel.X)
		particle.Velocity.Y = minVel.Y + rand.Float64()*(maxVel.Y-minVel.Y)
		particle.Velocity.Z = minVel.Z + rand.Float64()*(maxVel.Z-minVel.Z)

	case "rotationrandom", "rotation_random":
		minRot := getFloatFromInterface(init.Min)
		maxRot := getFloatFromInterface(init.Max)
		particle.Rotation = minRot + rand.Float64()*(maxRot-minRot)

		// If no min/max specified, use full range
		if minRot == 0 && maxRot == 0 {
			particle.Rotation = rand.Float64() * math.Pi * 2
		}

	case "angularvelocityrandom", "angularvelocity_random":
		minAngVel := getFloatFromInterface(init.Min)
		maxAngVel := getFloatFromInterface(init.Max)
		particle.AngularVel = minAngVel + rand.Float64()*(maxAngVel-minAngVel)

	case "colorrandom", "color_random":
		minColor := getVec3FromInterface(init.Min)
		maxColor := getVec3FromInterface(init.Max)

		particle.Color.X = (minColor.X + rand.Float64()*(maxColor.X-minColor.X)) / 255.0
		particle.Color.Y = (minColor.Y + rand.Float64()*(maxColor.Y-minColor.Y)) / 255.0
		particle.Color.Z = (minColor.Z + rand.Float64()*(maxColor.Z-minColor.Z)) / 255.0

	case "alpharandom", "alpha_random":
		minAlpha := getFloatFromInterface(init.Min)
		maxAlpha := getFloatFromInterface(init.Max)
		particle.Alpha = minAlpha + rand.Float64()*(maxAlpha-minAlpha)
		particle.InitialAlpha = particle.Alpha
	}
}

// Helper functions to extract values from interface{}
func getFloatFromInterface(val interface{}) float64 {
	switch v := val.(type) {
	case float64:
		return v
	case int:
		return float64(v)
	case string:
		// Parse string as float
		x, _, _ := parseVec3String(v)
		return x
	case map[string]interface{}:
		if value, ok := v["value"].(float64); ok {
			return value
		}
	}
	return 0
}

func getVec3FromInterface(val interface{}) wallpaper.Vec3 {
	switch v := val.(type) {
	case string:
		x, y, z := parseVec3String(v)
		return wallpaper.Vec3{X: x, Y: y, Z: z}
	case float64:
		return wallpaper.Vec3{X: v, Y: v, Z: v}
	case int:
		f := float64(v)
		return wallpaper.Vec3{X: f, Y: f, Z: f}
	case map[string]interface{}:
		vec := wallpaper.Vec3{}
		if x, ok := v["x"].(float64); ok {
			vec.X = x
		}
		if y, ok := v["y"].(float64); ok {
			vec.Y = y
		}
		if z, ok := v["z"].(float64); ok {
			vec.Z = z
		}
		return vec
	}
	return wallpaper.Vec3{X: 0, Y: 0, Z: 0}
}

func getVec3OrFloat(val interface{}) wallpaper.Vec3 {
	switch v := val.(type) {
	case float64:
		return wallpaper.Vec3{X: v, Y: v, Z: v}
	case int:
		f := float64(v)
		return wallpaper.Vec3{X: f, Y: f, Z: f}
	case string:
		x, y, z := parseVec3String(v)
		return wallpaper.Vec3{X: x, Y: y, Z: z}
	}
	return getVec3FromInterface(val)
}

var fallbackTexture *ebiten.Image

func (ps *ParticleSystem) Draw(screen *ebiten.Image, originX, originY float64, objScale wallpaper.Vec3) {
	img := ps.Texture
	if img == nil {
		if fallbackTexture == nil {
			fallbackTexture = ebiten.NewImage(2, 2)
			fallbackTexture.Fill(color.White)
		}
		img = fallbackTexture
	}

	for _, particle := range ps.Particles {
		drawOptions := &ebiten.DrawImageOptions{}

		width, height := img.Bounds().Dx(), img.Bounds().Dy()

		// Center the particle
		drawOptions.GeoM.Translate(-float64(width)/2, -float64(height)/2)

		// Apply rotation
		drawOptions.GeoM.Rotate(particle.Rotation)

		// Apply particle size and object scale
		scaleX := objScale.X * particle.Size / 100.0
		scaleY := objScale.Y * particle.Size / 100.0
		drawOptions.GeoM.Scale(scaleX, scaleY)

		// Translate to particle position
		drawOptions.GeoM.Translate(originX+particle.Position.X, originY+particle.Position.Y)

		// For additive blending, multiply color by alpha to get proper fade
		// This makes alpha=0 actually invisible
		drawOptions.ColorScale.Scale(
			float32(particle.Color.X*particle.Alpha),
			float32(particle.Color.Y*particle.Alpha),
			float32(particle.Color.Z*particle.Alpha),
			1.0, // Keep alpha channel at 1 for additive blending
		)

		// Use lighter blending for additive effect
		drawOptions.Blend = ebiten.BlendLighter
		screen.DrawImage(img, drawOptions)
	}
}

func (ps *ParticleSystem) SetMousePosition(x, y float64) {
	ps.MousePos = wallpaper.Vec3{X: x, Y: y, Z: 0}
}

func ApplyEffects(obj *wallpaper.Object, alpha *float64, tint *color.RGBA) {
	for _, effect := range obj.Effects {
		if !effect.Visible.Value {
			continue
		}

		if effect.Name == "opacity" {
			*alpha *= effect.Alpha.Value
		}
		if effect.Name == "tint" {
			// Basic tint logic
		}
	}
}
