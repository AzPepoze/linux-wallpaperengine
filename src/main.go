package main

import (
	"encoding/json"
	"flag"
	"image/png"
	"os"
	"path/filepath"
	"strings"

	"github.com/hajimehoshi/ebiten/v2"
)

func main() {
	pkgPath := flag.String("pkg", "/home/azpepoze/.local/share/Steam/steamapps/workshop/content/431960/2617953025/scene.pkg", "Path to the scene.pkg file")
	decodeMode := flag.Bool("decode", false, "Enable decode mode to convert a single .tex to .png")
	texToDecode := flag.String("tex", "", "Path to the .tex file to decode (used with -decode)")
	debugFlag := flag.Bool("debug", false, "Enable verbose debug logging")
	flag.Parse()

	DebugMode = *debugFlag

	if *decodeMode && *texToDecode != "" {
		runDecode(*texToDecode)
		return
	}

	Info("--- Wallpaper Engine Start (Fast Native) ---")

	if _, err := os.Stat("tmp"); os.IsNotExist(err) {
		Info("Unpacking scene.pkg...")
		if err := extractPkg(*pkgPath, "tmp"); err != nil {
			Error("Failed to extract pkg: %v", err)
			os.Exit(1)
		}
	}

	sceneData, err := findAndReadSceneJSON("tmp")
	if err != nil {
		Error("Failed to find/read scene.json: %v", err)
		os.Exit(1)
	}

	bulkConvertTextures("tmp")

	var scene Scene
	if err := json.Unmarshal(sceneData, &scene); err != nil {
		Error("Error unmarshalling scene.json: %v", err)
		os.Exit(1)
	}
	Info("Scene loaded: %d objects found", len(scene.Objects))

	ebiten.SetWindowSize(1280, 720)
	ebiten.SetWindowTitle("Linux Wallpaper Engine")

	game := NewGame(scene)

	Info("Starting game loop...")
	if err := ebiten.RunGame(game); err != nil {
		Error("Game loop error: %v", err)
	}
}

func runDecode(texPath string) {
	Info("Testing decode: %s", texPath)
	img, err := decodeTexToImage(texPath)
	if err != nil {
		Error("Decode failed: %v", err)
		os.Exit(1)
	}

	if err := os.MkdirAll("test_out", 0755); err != nil {
		Error("Failed to create test_out directory: %v", err)
		os.Exit(1)
	}

	baseName := filepath.Base(texPath)
	baseName = strings.TrimSuffix(baseName, filepath.Ext(baseName))
	outPath := filepath.Join("test_out", baseName+".png")

	f, err := os.Create(outPath)
	if err != nil {
		Error("Failed to create output file: %v", err)
		os.Exit(1)
	}
	defer f.Close()

	if err := png.Encode(f, img); err != nil {
		Error("Failed to encode PNG: %v", err)
		os.Exit(1)
	}

	Info("Decode successful! Saved to: %s", outPath)
}

func findAndReadSceneJSON(root string) ([]byte, error) {
	var sceneData []byte
	err := filepath.Walk(root, func(path string, info os.FileInfo, err error) error {
		if err == nil && !info.IsDir() && info.Name() == "scene.json" {
			Debug("Found scene.json at: %s", path)
			data, readErr := os.ReadFile(path)
			if readErr != nil {
				return readErr
			}
			sceneData = data
			return filepath.SkipDir
		}
		return nil
	})
	if err != nil {
		return nil, err
	}
	if len(sceneData) == 0 {
		return nil, os.ErrNotExist
	}
	return sceneData, nil
}

func bulkConvertTextures(root string) {
	Info("Starting bulk texture conversion...")
	convertedCount := 0
	filepath.Walk(root, func(path string, info os.FileInfo, err error) error {
		if err == nil && !info.IsDir() && strings.HasSuffix(path, ".tex") {
			_, err := loadTexture(path)
			if err != nil {
				Warn("Failed to convert %s: %v", path, err)
			} else {
				convertedCount++
			}
		}
		return nil
	})
	Info("Bulk conversion finished. Processed %d textures.", convertedCount)
}
