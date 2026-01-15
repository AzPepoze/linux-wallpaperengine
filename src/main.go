package main

import (
	"encoding/json"
	"flag"
	"os"
	"path/filepath"
	"strings"

	"linux-wallpaperengine/src/convert"
	"linux-wallpaperengine/src/utils"
	"linux-wallpaperengine/src/wallpaper"

	"github.com/hajimehoshi/ebiten/v2"
)

func main() {
	// Manual check for debug flag anywhere in the arguments
	for _, arg := range os.Args {
		if arg == "--debug" || arg == "-debug" || strings.HasPrefix(arg, "--debug=") {
			utils.DebugMode = true
			if strings.HasSuffix(arg, "=false") {
				utils.DebugMode = false
			}
			break
		}
	}

	// Custom handling for subcommands
	if len(os.Args) > 1 && os.Args[1] == "decode" {
		var texPath string
		// Find the first argument that doesn't start with "-" and isn't "decode"
		for i := 2; i < len(os.Args); i++ {
			if !strings.HasPrefix(os.Args[i], "-") {
				texPath = os.Args[i]
				break
			}
		}

		if texPath != "" {
			runDecode(texPath)
			return
		}
	}

	pkgPath := flag.String("pkg", "", "Path to the scene.pkg file")
	decodeMode := flag.Bool("decode", false, "Enable decode mode to convert a single .tex to .png")
	texToDecode := flag.String("tex", "", "Path to the .tex file to decode (used with -decode)")
	testSound := flag.String("test-sound", "", "Path to an mp3 file to test playback")
	testSine := flag.Bool("test-sine", false, "Test sine wave generator")
	debugFlag := flag.Bool("debug", false, "Enable verbose debug logging")
	flag.Parse()

	if *debugFlag {
		utils.DebugMode = true
	}

	if *testSine {
		runTestSine()
		return
	}

	if *testSound != "" {
		runTestSound(*testSound)
		return
	}

	if *decodeMode && *texToDecode != "" {
		runDecode(*texToDecode)
		return
	}

	utils.Info("--- Wallpaper Engine ---")

	wallpaperFolder := ""
	if *pkgPath == "" && len(flag.Args()) > 0 {
		wallpaperFolder = flag.Args()[0]
	}

	if wallpaperFolder != "" {
		// If a folder is passed, look for scene.pkg inside
		scenePkg := filepath.Join(wallpaperFolder, "scene.pkg")
		if _, err := os.Stat(scenePkg); err == nil {
			utils.Info("Extracting scene.pkg from folder: %s", wallpaperFolder)
			if err := convert.ExtractPkg(scenePkg, "tmp"); err != nil {
				utils.Error("Failed to extract pkg: %v", err)
				os.Exit(1)
			}
		} else {
			utils.Error("scene.pkg not found in folder: %s", wallpaperFolder)
			os.Exit(1)
		}
	} else {
		// Fallback to --pkg argument
		if *pkgPath == "" {
			utils.Error("No wallpaper folder or scene.pkg specified.")
			os.Exit(1)
		}
		if _, err := os.Stat("tmp"); os.IsNotExist(err) {
			utils.Info("Unpacking scene.pkg...")
			if err := convert.ExtractPkg(*pkgPath, "tmp"); err != nil {
				utils.Error("Failed to extract pkg: %v", err)
				os.Exit(1)
			}
		}
	}

	sceneData, err := findAndReadSceneJSON("tmp")
	if err != nil {
		utils.Error("Failed to find/read scene.json: %v", err)
		os.Exit(1)
	}

	convert.BulkConvertTextures("tmp", "converted")

	var scene wallpaper.Scene
	if err := json.Unmarshal(sceneData, &scene); err != nil {
		utils.Error("Error unmarshalling scene.json: %v", err)
		os.Exit(1)
	}
	utils.Info("Scene loaded: %d objects found", len(scene.Objects))

	// Get monitor size and set window to match
	monitor := ebiten.Monitor()
	monitorW, monitorH := monitor.Size()
	utils.Info("Monitor resolution: %dx%d", monitorW, monitorH)

	// Set window size to monitor size
	ebiten.SetWindowSize(monitorW, monitorH)
	ebiten.SetWindowTitle("Linux Wallpaper Engine")
	ebiten.SetWindowResizingMode(ebiten.WindowResizingModeEnabled)
	ebiten.SetRunnableOnUnfocused(true)

	game := NewWindow(scene)

	utils.Info("Starting game loop...")
	if err := ebiten.RunGame(game); err != nil {
		utils.Error("Game loop error: %v", err)
	}
}

func findAndReadSceneJSON(root string) ([]byte, error) {
	var sceneData []byte
	err := filepath.Walk(root, func(path string, info os.FileInfo, err error) error {
		if err == nil && !info.IsDir() && info.Name() == "scene.json" {
			utils.Debug("Found scene.json at: %s", path)
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
