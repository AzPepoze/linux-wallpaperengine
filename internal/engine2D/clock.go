package engine2D

import (
	"math"
	"strings"
	"time"

	"linux-wallpaperengine/internal/wallpaper"
)

func UpdateClock(objects []wallpaper.Object, offsets []wallpaper.Vec2) {
	currentTime := time.Now()
	hour, minute, second := currentTime.Clock()
	millisecond := currentTime.Nanosecond() / 1e6

	timeOfDay := (float64(hour*3600+minute*60+second) + float64(millisecond)/1000.0) / 86400.0

	for i := range objects {
		object := &objects[i]
		switch strings.ToLower(object.Name) {
		case "hour":
			// Formula from Wallpaper Engine scripts
			object.Angles.Z = timeOfDay * -720
		case "minute":
			object.Angles.Z = math.Mod(timeOfDay*24, 1) * -360
		case "seconds":
			object.Angles.Z = math.Mod(timeOfDay*1440, 1) * -360
		}
	}
}
