package main

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"image"
	"log"
	"os"
	"os/exec"
	"strings"

	"github.com/galaco/dxt"
	"github.com/hajimehoshi/ebiten/v2"
	"github.com/hajimehoshi/ebiten/v2/ebitenutil"
	"github.com/pierrec/lz4/v4"
)

func loadTexture(path string) (*ebiten.Image, error) {
	data, err := os.ReadFile(path)
	if err != nil { return nil, err }

	if !strings.HasPrefix(string(data), "TEXV0005") {
		return nil, fmt.Errorf("not a TEXV0005 file")
	}

	// 1. ลอง XOR 0x77 ทั้งก้อนแล้วหา PNG/JPG (ไม้ตายลับ)
	xorData := make([]byte, len(data))
	for i := range data { xorData[i] = data[i] ^ 0x77 }
	
	pngMagic := []byte{0x89, 0x50, 0x4E, 0x47}
	if idx := bytes.Index(xorData, pngMagic); idx != -1 {
		log.Printf("Loader: Found XORed PNG in %s!", path)
		img, _, err := ebitenutil.NewImageFromReader(bytes.NewReader(xorData[idx:]))
		if err == nil { return img, nil }
	}

	// 2. วิเคราะห์ Mipmap และคลายบีบอัด
	width := int(binary.LittleEndian.Uint32(data[26:30]))
	height := int(binary.LittleEndian.Uint32(data[30:34]))
	
	wBuf := make([]byte, 4); binary.LittleEndian.PutUint32(wBuf, uint32(width))
	hBuf := make([]byte, 4); binary.LittleEndian.PutUint32(hBuf, uint32(height))
	headerIdx := bytes.Index(data, append(wBuf, hBuf...))
	if headerIdx == -1 { return nil, fmt.Errorf("mipmap header not found") }

	uSize := int(binary.LittleEndian.Uint32(data[headerIdx+12 : headerIdx+16]))
	cSize := int(binary.LittleEndian.Uint32(data[headerIdx+16 : headerIdx+20]))
	pixelData := data[headerIdx+20 : headerIdx+20+cSize]

	// คลาย LZ4
	var rawPixels []byte
	if cSize < uint32(uSize) {
		rawPixels = make([]byte, uSize)
		n, err := lz4.UncompressBlock(pixelData, rawPixels)
		if err != nil {
			log.Printf("Loader: LZ4 failed for %s, trying XOR on pixels", path)
		} else {
			rawPixels = rawPixels[:n]
		}
	} else {
		rawPixels = pixelData
	}

	// 3. ลอง XOR 0x77 บนพิกเซลที่คลายแล้ว (เผื่อพิกเซลถูกพรางไว้)
	xorPixels := make([]byte, len(rawPixels))
	for i := range rawPixels { xorPixels[i] = rawPixels[i] ^ 0x77 }
	
	// ลองสแกนหา PNG อีกรอบในพิกเซลที่ XOR แล้ว
	if idx := bytes.Index(xorPixels, pngMagic); idx != -1 {
		img, _, err := ebitenutil.NewImageFromReader(bytes.NewReader(xorPixels[idx:]))
		if err == nil { return img, nil }
	}

	// 4. ถ้ายังไม่ได้ผล ให้ Decode เป็น DXT1/5 โดยตรง (พยายามล้าง Noise)
	dxtType := dxt.DXT5
	if uSize <= (width*height)/2 + 8192 { dxtType = dxt.DXT1 }

	// เราจะลองหลายๆ Offset เพราะ Noise มักเกิดจาก Offset ผิดไป 4-16 ไบต์
	for _, offset := range []int{0, 4, 8, 12, 16, 24, 32} {
		if offset >= len(rawPixels) { continue }
		
		// ลองทั้งพิกเซลปกติและพิกเซลที่ XOR
		for _, p := range [][]byte{rawPixels, xorPixels} {
			decoded, err := dxt.Decode(p[offset:], width, height, dxtType)
			if err == nil {
				// ตรวจสอบความถูกต้องเบื้องต้น (ถ้าพิกเซลแรกๆ ไม่ใช่ 0 ทั้งหมด)
				if decoded[0] != 0 || decoded[1] != 0 || decoded[2] != 0 {
					log.Printf("Loader: DXT Success for %s at offset %d", path, offset)
					img := image.NewRGBA(image.Rect(0, 0, width, height))
					img.Pix = decoded
					return ebiten.NewImageFromImage(img), nil
				}
			}
		}
	}

	return nil, fmt.Errorf("decoding failed for %s", path)
}
