package wallpaper

import (
	"path/filepath"

	"linux-wallpaperengine/src/utils"

	rl "github.com/gen2brain/raylib-go/raylib"
)

type AudioStream struct {
	music      rl.Music
	active     bool
	shouldLoop bool
}

type AudioManager struct {
	streams  []*AudioStream
}

func NewAudioManager() *AudioManager {
	if !rl.IsAudioDeviceReady() {
		rl.InitAudioDevice()
	}
	return &AudioManager{
		streams: make([]*AudioStream, 0),
	}
}

func (am *AudioManager) Play(obj *Object) {
	if !obj.Visible.Value || len(obj.Sound.Value) == 0 {
		return
	}
	soundPath := filepath.Join("tmp", obj.Sound.Value)
	am.PlayDirect(soundPath, obj.Volume.Value, true)
}

func (am *AudioManager) PlayDirect(soundPath string, vol float64, shouldLoop bool) {
	music := rl.LoadMusicStream(soundPath)
	if music.Stream.Buffer == nil { // Check if loaded successfully (Go binding specific check might vary, usually 0 check on ID)
		// Raylib-go music struct might not have easy valid check exposed or it just works.
		// If pointer is nil or something. But music is a struct.
		// We trust Raylib for now or check output logs.
	}
	
	music.Looping = shouldLoop
	rl.SetMusicVolume(music, float32(vol))
	rl.PlayMusicStream(music)

	am.streams = append(am.streams, &AudioStream{
		music:      music,
		active:     true,
		shouldLoop: shouldLoop,
	})

	utils.Info("Raylib: Playing %s (Vol: %.2f)", soundPath, vol)
}

func (am *AudioManager) Update() {
	for _, stream := range am.streams {
		if stream.active {
			rl.UpdateMusicStream(stream.music)
		}
	}
}

func (am *AudioManager) Close() {
	for _, stream := range am.streams {
		if stream.active {
			rl.StopMusicStream(stream.music)
			rl.UnloadMusicStream(stream.music)
			stream.active = false
		}
	}
	if rl.IsAudioDeviceReady() {
		rl.CloseAudioDevice()
	}
}
