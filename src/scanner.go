package main

import (
	"encoding/binary"
	"fmt"
	"io/ioutil"
	"path/filepath"
	"strings"

	"github.com/pierrec/lz4/v4"
)

func scanTexFiles(dir string) {
	files, _ := filepath.Glob(filepath.Join(dir, "materials", "*.tex"))
	for _, path := range files {
		data, err := ioutil.ReadFile(path)
		if err != nil {
			continue
		}

		fmt.Printf("\n--- Scanning %s (%d bytes) ---\n", filepath.Base(path), len(data)) // ค้นหา TEXB block
		idx := strings.Index(string(data), "TEXB0003")
		if idx == -1 {
			fmt.Println("TEXB0003 not found")
			continue
		}

		mipmapCount := binary.LittleEndian.Uint32(data[idx+8 : idx+12])
		fmt.Printf("Mipmaps: %d\n", mipmapCount)

		curr := idx + 12
		for i := 0; i < int(mipmapCount); i++ {
			if curr+20 > len(data) { break }
			w := binary.LittleEndian.Uint32(data[curr : curr+4])
			h := binary.LittleEndian.Uint32(data[curr+4 : curr+8])
			uSize := binary.LittleEndian.Uint32(data[curr+12 : curr+16])
			cSize := binary.LittleEndian.Uint32(data[curr+16 : curr+20])
			
			fmt.Printf("  Mipmap %d: %dx%d | Uncompressed: %d | Compressed: %d\n", i, w, h, uSize, cSize)
			
			start := curr + 20
			if start+int(cSize) <= len(data) {
				block := data[start : start+int(cSize)]
				if cSize < uSize {
					// ลองคลาย LZ4 ดูว่าพังไหม
					out := make([]byte, uSize)
					_, err := lz4.UncompressBlock(block, out)
					if err == nil {
						fmt.Printf("    [SUCCESS] LZ4 Decompression works!\n")
					} else {
						fmt.Printf("    [FAILED] LZ4 Error: %v\n", err)
					}
				} else {
					fmt.Printf("    [RAW] Data is not compressed\n")
				}
			}
			curr = start + int(cSize)
		}
	}
}

func main() {
	scanTexFiles("tmp")
}
