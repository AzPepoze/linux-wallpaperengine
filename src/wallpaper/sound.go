package wallpaper

import (
	"io"
	"os"
	"path/filepath"
	"sync"

	"linux-wallpaperengine/src/utils"

	"github.com/gen2brain/malgo"
	"github.com/hajimehoshi/go-mp3"
)

type AudioStream struct {
	decoder *mp3.Decoder
	volume  float64
	active  bool
}

type AudioManager struct {
	ctx      *malgo.AllocatedContext
	device   *malgo.Device
	streams  []*AudioStream
	mutex    sync.Mutex
	initOnce sync.Once
}

func NewAudioManager() *AudioManager {
	return &AudioManager{
		streams: make([]*AudioStream, 0),
	}
}

func (am *AudioManager) initDevice(sampleRate uint32) {
	am.initOnce.Do(func() {
		var err error
		am.ctx, err = malgo.InitContext(nil, malgo.ContextConfig{}, nil)
		if err != nil {
			utils.Error("Malgo: Failed to init context: %v", err)
			return
		}

		deviceConfig := malgo.DefaultDeviceConfig(malgo.Playback)
		deviceConfig.Playback.Format = malgo.FormatS16
		deviceConfig.Playback.Channels = 2
		deviceConfig.SampleRate = sampleRate
		deviceConfig.Alsa.NoMMap = 1

		onSamples := func(pOutputSample, pInputSample []byte, frameCount uint32) {
			am.mutex.Lock()
			defer am.mutex.Unlock()

			// Clear output buffer
			for i := range pOutputSample {
				pOutputSample[i] = 0
			}

			// Mix all active streams
			sampleSize := uint32(2 * 2) // S16 (2 bytes) * 2 channels
			tempBuf := make([]byte, frameCount*sampleSize)

			for _, stream := range am.streams {
				if !stream.active {
					continue
				}

				n, err := io.ReadFull(stream.decoder, tempBuf)
				if err != nil {
					if err == io.EOF || err == io.ErrUnexpectedEOF {
						stream.decoder.Seek(0, io.SeekStart) // Loop
						io.ReadFull(stream.decoder, tempBuf)
					} else {
						stream.active = false
						continue
					}
				}

				// Simple Mixing (Addition with volume)
				for i := 0; i < n; i += 2 {
					// Read S16LE
					val := int16(tempBuf[i]) | int16(tempBuf[i+1])<<8
					mixedVal := int16(float64(val) * stream.volume)

					// Mix into output
					outVal := int16(pOutputSample[i]) | int16(pOutputSample[i+1])<<8
					newVal := outVal + mixedVal
					
					pOutputSample[i] = byte(newVal & 0xff)
					pOutputSample[i+1] = byte(newVal >> 8)
				}
			}
		}

		am.device, err = malgo.InitDevice(am.ctx.Context, deviceConfig, malgo.DeviceCallbacks{
			Data: onSamples,
		})
		if err != nil {
			utils.Error("Malgo: Failed to init device: %v", err)
			return
		}

		if err := am.device.Start(); err != nil {
			utils.Error("Malgo: Failed to start device: %v", err)
		}
		utils.Info("Malgo: Device started at %dHz", sampleRate)
	})
}

func (am *AudioManager) Play(obj *Object) {
	if !obj.Visible.Value || len(obj.Sound.Value) == 0 {
		return
	}
	soundPath := filepath.Join("tmp", obj.Sound.Value)
	am.PlayDirect(soundPath, obj.Volume.Value)
}

func (am *AudioManager) PlayDirect(soundPath string, vol float64) {
	f, err := os.Open(soundPath)
	if err != nil {
		utils.Warn("Failed to open sound file %s: %v", soundPath, err)
		return
	}

	dec, err := mp3.NewDecoder(f)
	if err != nil {
		utils.Warn("Failed to decode mp3 %s: %v", soundPath, err)
		f.Close()
		return
	}

	am.initDevice(uint32(dec.SampleRate()))

	am.mutex.Lock()
	am.streams = append(am.streams, &AudioStream{
		decoder: dec,
		volume:  vol,
		active:  true,
	})
	am.mutex.Unlock()

	utils.Info("Malgo: Playing %s (Vol: %.2f)", soundPath, vol)
}

func (am *AudioManager) Close() {
	if am.device != nil {
		am.device.Uninit()
	}
	if am.ctx != nil {
		am.ctx.Free()
	}
}
