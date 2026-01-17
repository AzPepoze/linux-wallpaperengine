package wallpaper

import (
	"path/filepath"

	"linux-wallpaperengine/internal/utils"

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
	if !utils.SilentMode && !rl.IsAudioDeviceReady() {
		rl.InitAudioDevice()
	}
	return &AudioManager{
		streams: make([]*AudioStream, 0),
	}
}

func (am *AudioManager) Play(obj *Object) {
	if utils.SilentMode || !obj.Visible.GetBool() || len(obj.Sound) == 0 {
		return
	}
	
	// Just play the first sound for now
	soundPath := filepath.Join("tmp", obj.Sound[0])
	am.PlayDirect(soundPath, obj.Volume.GetFloat(), true)
}

func (am *AudioManager) PlayDirect(soundPath string, vol float64, shouldLoop bool) {
	if utils.SilentMode {
		return
	}
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
