package particle

import (
	"linux-wallpaperengine/internal/wallpaper"
)

func ParseVec3String(s string) (float64, float64, float64) {
	return wallpaper.ParseColor(s)
}

func GetVec3FromInterface(val interface{}) wallpaper.Vec3 {
	switch v := val.(type) {
	case string:
		x, y, z := ParseVec3String(v)
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

func GetVec3OrFloat(val interface{}) wallpaper.Vec3 {
	switch v := val.(type) {
	case float64:
		return wallpaper.Vec3{X: v, Y: v, Z: v}
	case int:
		f := float64(v)
		return wallpaper.Vec3{X: f, Y: f, Z: f}
	case string:
		x, y, z := ParseVec3String(v)
		return wallpaper.Vec3{X: x, Y: y, Z: z}
	}
	return GetVec3FromInterface(val)
}

func GetFloatFromInterface(val interface{}) float64 {
	switch v := val.(type) {
	case float64:
		return v
	case int:
		return float64(v)
	case string:
		x, _, _ := ParseVec3String(v)
		return x
	case map[string]interface{}:
		if value, ok := v["value"].(float64); ok {
			return value
		}
	}
	return 0
}
