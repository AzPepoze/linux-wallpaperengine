package utils

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

func FindTextureFile(name string) string {
	if name == "" {
		return ""
	}

	cleanName := strings.TrimPrefix(name, "materials/")
	cleanName = strings.TrimSuffix(cleanName, ".tex")

	searchDirs := []string{
		"converted",
		"tmp/materials",
		"tmp/materials/workshop",
		"tmp/materials/presets",
		"tmp",
		"assets/materials",
		"assets",
	}

	for _, dir := range searchDirs {
		p := filepath.Join(dir, cleanName+".tex")
		if _, err := os.Stat(p); err == nil {
			return p
		}

		// Try with original name inside dir too
		p = filepath.Join(dir, name+".tex")
		if _, err := os.Stat(p); err == nil {
			return p
		}
	}

	// Recursive fallback for assets folder specifically (deep search)
	var foundPath string
	filepath.Walk("assets", func(path string, info os.FileInfo, err error) error {
		if err == nil && !info.IsDir() && (strings.HasSuffix(path, cleanName+".tex") || strings.HasSuffix(path, filepath.Base(cleanName)+".tex")) {
			foundPath = path
			return fmt.Errorf("found")
		}
		return nil
	})

	return foundPath
}
