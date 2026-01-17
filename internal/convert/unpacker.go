package convert

import (
	"encoding/binary"
	"io"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"sync/atomic"

	"linux-wallpaperengine/internal/utils"
)

type FileEntry struct {
	Name   string
	Offset uint32
	Size   uint32
}

func readPkgString(r io.Reader) (string, error) {
	var size uint32
	if err := binary.Read(r, binary.LittleEndian, &size); err != nil {
		return "", err
	}
	buf := make([]byte, size)
	if _, err := io.ReadFull(r, buf); err != nil {
		return "", err
	}
	return string(buf), nil
}

func ExtractPkg(pkgPath, outputDir string) error {
	utils.Debug("Unpacker: Opening package %s", pkgPath)
	f, err := os.Open(pkgPath)
	if err != nil {
		return err
	}
	defer f.Close()

	version, err := readPkgString(f)
	if err != nil {
		return err
	}
	utils.Debug("Unpacker: Package Version: %s", version)

	var fileCount uint32
	if err := binary.Read(f, binary.LittleEndian, &fileCount); err != nil {
		return err
	}
	utils.Debug("Unpacker: File Count: %d", fileCount)

	entries := make([]FileEntry, fileCount)
	for i := uint32(0); i < fileCount; i++ {
		name, err := readPkgString(f)
		if err != nil {
			return err
		}
		var offset, size uint32
		binary.Read(f, binary.LittleEndian, &offset)
		binary.Read(f, binary.LittleEndian, &size)
		entries[i] = FileEntry{Name: name, Offset: offset, Size: size}
	}

	dataStartPos, _ := f.Seek(0, io.SeekCurrent)

	if err := os.MkdirAll(outputDir, 0755); err != nil {
		return err
	}

	for i, entry := range entries {
		if i%10 == 0 || i == int(fileCount)-1 {
			utils.Debug("Unpacker: Extracting file %d/%d: %s", i+1, fileCount, entry.Name)
		}
		destPath := filepath.Join(outputDir, entry.Name)
		if err := os.MkdirAll(filepath.Dir(destPath), 0755); err != nil {
			return err
		}

		if _, err := f.Seek(dataStartPos+int64(entry.Offset), io.SeekStart); err != nil {
			return err
		}

		outF, err := os.Create(destPath)
		if err != nil {
			return err
		}

		_, err = io.CopyN(outF, f, int64(entry.Size))
		outF.Close()
		if err != nil {
			return err
		}
	}

	utils.Debug("Unpacker: Extraction completed successfully")
	return nil
}

func BulkConvertTextures(root string, outDir string) {
	utils.Info("Starting bulk texture conversion in parallel...")
	var convertedCount int32
	var wg sync.WaitGroup

	// Limit concurrency to avoid RAM spikes
	const maxConcurrency = 10
	sem := make(chan struct{}, maxConcurrency)

	TextureOutDir = outDir
	if outDir != "" {
		os.MkdirAll(outDir, 0755)
	}

	err := filepath.Walk(root, func(path string, info os.FileInfo, err error) error {
		if err == nil && !info.IsDir() && strings.HasSuffix(path, ".tex") {
			wg.Add(1)
			sem <- struct{}{} // Acquire slot
			go func(p string) {
				defer wg.Done()
				defer func() { <-sem }() // Release slot
				err := LoadTexture(p)
				if err != nil {
					utils.Error("Failed to convert %s: %v", p, err)
				} else {
					atomic.AddInt32(&convertedCount, 1)
				}
			}(path)
		}
		return nil
	})

	if err != nil {
		utils.Error("Error walking through directory: %v", err)
	}

	wg.Wait()
	utils.Info("Bulk conversion finished. Processed %d textures.", convertedCount)
}
