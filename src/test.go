package main

import (
	"fmt"
	"image/png"
	"math"
	"os"
	"path/filepath"
	"strings"

	"linux-wallpaperengine/src/convert"
	"linux-wallpaperengine/src/utils"
	"linux-wallpaperengine/src/wallpaper"
)

func runTestSound(path string) {
	utils.Info("Testing sound playback (Malgo): %s", path)
	am := wallpaper.NewAudioManager()
	defer am.Close()
	
	am.PlayDirect(path, 1.0)
	
	utils.Info("Playing... Press Enter to stop.")
	var input string
	fmt.Scanln(&input)
}

func runTestSine() {
	utils.Info("Testing sine wave generator (440Hz)...")
	
	sampleRate := 48000
	frequency := 440.0
	duration := 2.0
	amplitude := 0.1

	utils.Info("Generating %f seconds of %f Hz sine wave at %d Hz sample rate", duration, frequency, sampleRate)
	
	for i := 0; i < 10; i++ {
		t := float64(i) / float64(sampleRate)
		val := amplitude * math.Sin(2*math.Pi*frequency*t)
		utils.Debug("Sample %d: %f", i, val)
	}

	utils.Info("Sine generator test completed (Dry run).")
}

func runDecode(texPath string) {
	utils.Info("Testing decode: %s", texPath)
	img, err := convert.DecodeTexToImage(texPath)
	if err != nil {
		utils.Error("Decode failed: %v", err)
		os.Exit(1)
	}

	if err := os.MkdirAll("test_out", 0755); err != nil {
		utils.Error("Failed to create test_out directory: %v", err)
		os.Exit(1)
	}

	baseName := filepath.Base(texPath)
	baseName = strings.TrimSuffix(baseName, filepath.Ext(baseName))
	outPath := filepath.Join("test_out", baseName+".png")

	f, err := os.Create(outPath)
	if err != nil {
		utils.Error("Failed to create output file: %v", err)
		os.Exit(1)
	}
	defer f.Close()

	if err := png.Encode(f, img); err != nil {
		utils.Error("Failed to encode PNG: %v", err)
		os.Exit(1)
	}

	utils.Info("Decode successful! Saved to: %s", outPath)
}