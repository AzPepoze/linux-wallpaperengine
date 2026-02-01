package particle

import (
	"linux-wallpaperengine/internal/wallpaper"
)

// NewParticleSystem creates a new particle system with the given options.
func NewParticleSystem(opts ParticleSystemOptions) *ParticleSystem {
	ps := &ParticleSystem{
		Name:          opts.Name,
		Config:        opts.Config,
		Texture:       opts.Texture,
		ExtraTextures: opts.ExtraTextures,
		TextureName:   opts.TextureName,
		Override:      opts.Override,
		ControlPts:    make([]wallpaper.Vec3, 8),
		BlendMode:     opts.BlendMode,
		TexInfo:       opts.TexInfo,
	}

	for i := range ps.ControlPts {
		ps.ControlPts[i] = wallpaper.Vec3{X: 0, Y: 0, Z: 0}
	}

	return ps
}

// Update updates the particle system simulation by delta time.
func (ps *ParticleSystem) Update(dt float64) {
	ps.GlobalTime += dt

	if len(ps.Config.Emitter) == 0 {
		return
	}

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

	countMultiplier := 1.0
	if ps.Override != nil && ps.Override.Count.GetFloat() != 0 {
		countMultiplier = ps.Override.Count.GetFloat()
	}
	maxCount = int(float64(maxCount) * countMultiplier)

	for _, emitter := range ps.Config.Emitter {
		rate := GetFloatFromInterface(emitter.Rate)
		if ps.Override != nil && ps.Override.Rate.GetFloat() != 0 {
			rate *= ps.Override.Rate.GetFloat()
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

	for i := 0; i < len(ps.Particles); i++ {
		particle := ps.Particles[i]
		particle.Life -= dt

		if particle.Life <= 0 {
			ps.Particles = append(ps.Particles[:i], ps.Particles[i+1:]...)
			i--
			continue
		}

		ps.applyOperators(particle, dt)

		particle.Position.X += particle.Velocity.X * dt
		particle.Position.Y += particle.Velocity.Y * dt
		particle.Position.Z += particle.Velocity.Z * dt

		particle.Rotation += particle.AngularVel * dt
	}
}

// SetMousePosition sets the mouse position for control point tracking.
func (ps *ParticleSystem) SetMousePosition(x, y float64) {
	ps.MousePos = wallpaper.Vec3{X: x, Y: y, Z: 0}
}
