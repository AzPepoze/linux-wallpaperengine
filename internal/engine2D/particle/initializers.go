package particle

import (
	"math"
	"math/rand"

	"linux-wallpaperengine/internal/wallpaper"
)

// applyInitializer applies a particle initializer to a new particle.
func (ps *ParticleSystem) applyInitializer(particle *Particle, init wallpaper.ParticleInitializer) {
	switch init.Name {
	case "lifetimerandom":
		minLife := GetFloatFromInterface(init.Min)
		maxLife := GetFloatFromInterface(init.Max)
		particle.MaxLife = minLife + rand.Float64()*(maxLife-minLife)
		particle.Life = particle.MaxLife

	case "sizerandom", "size_random":
		minSize := GetFloatFromInterface(init.Min)
		maxSize := GetFloatFromInterface(init.Max)

		if init.Exponent != 0 {
			t := math.Pow(rand.Float64(), init.Exponent)
			particle.Size = minSize + t*(maxSize-minSize)
		} else {
			particle.Size = minSize + rand.Float64()*(maxSize-minSize)
		}
		particle.InitialSize = particle.Size

	case "velocityrandom", "velocity_random":
		minVel := GetVec3FromInterface(init.Min)
		maxVel := GetVec3FromInterface(init.Max)

		particle.Velocity.X = minVel.X + rand.Float64()*(maxVel.X-minVel.X)
		particle.Velocity.Y = minVel.Y + rand.Float64()*(maxVel.Y-minVel.Y)
		particle.Velocity.Z = minVel.Z + rand.Float64()*(maxVel.Z-minVel.Z)

	case "rotationrandom", "rotation_random":
		minRot := GetFloatFromInterface(init.Min)
		maxRot := GetFloatFromInterface(init.Max)
		particle.Rotation = minRot + rand.Float64()*(maxRot-minRot)

		if minRot == 0 && maxRot == 0 {
			particle.Rotation = rand.Float64() * math.Pi * 2
		}

	case "angularvelocityrandom", "angularvelocity_random":
		minAngVel := GetFloatFromInterface(init.Min)
		maxAngVel := GetFloatFromInterface(init.Max)
		particle.AngularVel = minAngVel + rand.Float64()*(maxAngVel-minAngVel)

	case "colorrandom", "color_random":
		minColor := GetVec3FromInterface(init.Min)
		maxColor := GetVec3FromInterface(init.Max)

		particle.Color.X = (minColor.X + rand.Float64()*(maxColor.X-minColor.X)) / 255.0
		particle.Color.Y = (minColor.Y + rand.Float64()*(maxColor.Y-minColor.Y)) / 255.0
		particle.Color.Z = (minColor.Z + rand.Float64()*(maxColor.Z-minColor.Z)) / 255.0

	case "alpharandom", "alpha_random":
		minAlpha := GetFloatFromInterface(init.Min)
		maxAlpha := GetFloatFromInterface(init.Max)
		particle.Alpha = minAlpha + rand.Float64()*(maxAlpha-minAlpha)
		particle.InitialAlpha = particle.Alpha
	}
}
