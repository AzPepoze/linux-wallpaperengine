package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"strings"

	"linux-wallpaperengine/src/convert"
	"linux-wallpaperengine/src/utils"
	"linux-wallpaperengine/src/wallpaper"

	rl "github.com/gen2brain/raylib-go/raylib"
)

func main() {
	// clear tmp
	os.RemoveAll("tmp")
	os.Mkdir("tmp", 0755)

	// clear converted
	os.RemoveAll("converted")
	os.Mkdir("converted", 0755)

	// Manual check for log levels before flag.Parse to ensure early logs are captured
	for _, arg := range os.Args {
		if arg == "--debug" {
			utils.CurrentLevel = utils.LevelDebug
			utils.DebugMode = true
			utils.ShowDebugUI = true
		} else if arg == "--debug-ui" {
			utils.ShowDebugUI = true
		} else if arg == "--silent" {
			utils.SilentMode = true
		} else if arg == "--info" && utils.CurrentLevel > utils.LevelInfo {
			utils.CurrentLevel = utils.LevelInfo
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
	debugFlag := flag.Bool("debug", false, "Enable verbose debug logging and UI")
	debugUIFlag := flag.Bool("debug-ui", false, "Enable debug overlay UI")
	infoFlag := flag.Bool("info", false, "Enable info logging")
	raylibInfoFlag := flag.Bool("info-raylib", false, "Show Raylib internal info logs")
	silentFlag := flag.Bool("silent", false, "Mute all audio output")
	scalingMode := flag.String("scaling", "fit", "Scaling mode: cover, fit")
	assetsDir := flag.String("assets-dir", "", "Set custom path for assets")

	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, "Usage: %s [options] [wallpaper_folder or scene.pkg]\n\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "Options:\n")
		flag.PrintDefaults()
		fmt.Fprintf(os.Stderr, "\nExamples:\n")
		fmt.Fprintf(os.Stderr, "  %s ./my_wallpaper\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "  %s --pkg ./scene.pkg --debug\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "  %s --silent --scaling cover\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "  %s --assets-dir /path/to/assets ./wallpaper\n", os.Args[0])
	}

	flag.Parse()

	DiscoverAssets(*assetsDir)
	utils.WallpaperEngineAssets = AssetsPath

	if *debugFlag {
		utils.DebugMode = true
		utils.ShowDebugUI = true
		utils.CurrentLevel = utils.LevelDebug
	} else if *debugUIFlag {
		utils.ShowDebugUI = true
	}

	if *silentFlag {
		utils.SilentMode = true
	}

	if *infoFlag {
		utils.CurrentLevel = utils.LevelInfo
	}

	if *raylibInfoFlag {
		utils.ShowRaylibInfo = true
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

	rl.SetTraceLogCallback(utils.RaylibLogCallback)

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
			flag.Usage()
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
	var scene wallpaper.Scene

	if err != nil {
		utils.Error("Failed to find/read scene.json: %v (skipping)", err)
	} else {
		err := json.Unmarshal(sceneData, &scene)
		if err != nil {
			utils.Error("Fatal JSON error: %v", err)
		} else {
			utils.Info("Scene loaded: %d objects found", len(scene.Objects))
		}
	}

	convert.BulkConvertTextures("tmp", "converted")

	// Manual GC to free up memory after bulk conversion
	runtime.GC()

	// Get monitor size and set window to match
	// Raylib initialization
	rl.SetConfigFlags(rl.FlagWindowUndecorated | rl.FlagWindowResizable)
	rl.InitWindow(1280, 720, "Linux Wallpaper Engine")
	defer rl.CloseWindow()

	monitor := rl.GetCurrentMonitor()
	monitorW := rl.GetMonitorWidth(monitor)
	monitorH := rl.GetMonitorHeight(monitor)

	utils.Info("Monitor resolution: %dx%d", monitorW, monitorH)

	rl.SetWindowSize(monitorW, monitorH)

	game := NewWindow(scene, *scalingMode)

	utils.Info("Starting game loop...")
	game.Run()
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
