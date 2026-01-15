package main

import (
	"encoding/json"
	"log"
	"os"
	"os/exec"

	"github.com/hajimehoshi/ebiten/v2"
)

func main() {
	log.Println("--- Wallpaper Engine Start ---")
	unpackerPath := "./bin/unpacker"
	scenePkgPath := "/home/azpepoze/.local/share/Steam/steamapps/workshop/content/431960/2617953025/scene.pkg"

	if _, err := os.Stat(unpackerPath); os.IsNotExist(err) {
		log.Println("Compiling unpacker...")
		exec.Command("make").Run()
	}

	log.Println("Unpacking scene.pkg...")
	exec.Command(unpackerPath, "x", scenePkgPath).Run()

	log.Println("Reading scene.json...")
	sceneData, err := os.ReadFile("tmp/scene.json")
	if err != nil {
		log.Fatalf("Error: Failed to read scene.json: %v", err)
	}

	log.Println("Parsing scene.json...")
	var scene Scene
	if err := json.Unmarshal(sceneData, &scene); err != nil {
		log.Fatalf("Error: Failed to parse scene.json: %v", err)
	}

	log.Println("Setting up window...")
	w, h := scene.General.OrthogonalProjection.Width, scene.General.OrthogonalProjection.Height
	displayW := 1280
	displayH := (displayW * h) / w
	
	ebiten.SetWindowSize(displayW, displayH)
	ebiten.SetWindowTitle("Linux Wallpaper Engine Prototype")
	
	log.Println("Creating game instance...")
	game := &Game{
		scene:   scene,
		bgColor: parseColor(scene.General.ClearColor),
	}
	
	log.Println("Running Ebiten game loop...")
	if err := ebiten.RunGame(game); err != nil {
		log.Fatalf("Error: Ebiten RunGame failed: %v", err)
	}
	log.Println("--- Wallpaper Engine Finished ---")
}
