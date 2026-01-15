package main

import (
	"encoding/binary"
	"io"
	"os"
	"path/filepath"
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

func extractPkg(pkgPath, outputDir string) error {
	Debug("Unpacker: Opening package %s", pkgPath)
	f, err := os.Open(pkgPath)
	if err != nil {
		return err
	}
	defer f.Close()

	version, err := readPkgString(f)
	if err != nil {
		return err
	}
	Debug("Unpacker: Package Version: %s", version)

	var fileCount uint32
	if err := binary.Read(f, binary.LittleEndian, &fileCount); err != nil {
		return err
	}
	Debug("Unpacker: File Count: %d", fileCount)

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
	Debug("Unpacker: Data starts at offset %d", dataStartPos)
	
	if err := os.MkdirAll(outputDir, 0755); err != nil {
		return err
	}

	for i, entry := range entries {
		if i%10 == 0 || i == int(fileCount)-1 {
			Debug("Unpacker: Extracting file %d/%d: %s", i+1, fileCount, entry.Name)
		}
		destPath := filepath.Join(outputDir, entry.Name)
		if err := os.MkdirAll(filepath.Dir(destPath), 0755); err != nil {
			return err
		}

		if _, err := f.Seek(dataStartPos+int64(entry.Offset), io.SeekStart); err != nil {
			return err
		}
		
		buf := make([]byte, entry.Size)
		if _, err := io.ReadFull(f, buf); err != nil {
			return err
		}
		
		if err := os.WriteFile(destPath, buf, 0644); err != nil {
			return err
		}
	}

	Debug("Unpacker: Extraction completed successfully")
	return nil
}
