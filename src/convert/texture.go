package convert

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"image"
	"image/png"
	"io"
	"os"
	"path/filepath"
	"strings"

	"linux-wallpaperengine/src/utils"

	rl "github.com/gen2brain/raylib-go/raylib"
	"github.com/mauserzjeh/dxt"
	"github.com/pierrec/lz4/v4"
)

// TextureOutDir is the directory where converted PNGs will be saved.
// If empty, they will be saved in the same directory as the source .tex file.
var TextureOutDir string

func readInt(r io.Reader) uint32 {
	var v uint32
	binary.Read(r, binary.LittleEndian, &v)
	return v
}

func readString(r io.Reader, n int) string {
	b := make([]byte, n)
	r.Read(b)
	return string(bytes.Trim(b, "\x00"))
}

func swapRB(pix []byte) {
	for i := 0; i < len(pix); i += 4 {
		pix[i], pix[i+2] = pix[i+2], pix[i]
	}
}

func decodePNG(data []byte, path string) (image.Image, error) {
	pngSignature := []byte{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A}
	if len(data) > 8 && bytes.Equal(data[:8], pngSignature) {
		utils.Debug("    Detected embedded PNG: %s", path)
		img, err := png.Decode(bytes.NewReader(data))
		if err != nil {
			return nil, fmt.Errorf("failed to decode embedded png: %v", err)
		}
		return img, nil
	}
	return nil, nil
}

func decodeRGBA(data []byte) ([]byte, error) {
	utils.Debug("    Type: RGBA")
	for k := 0; k < len(data); k += 4 {
		opacity := data[k+3]
		data[k] = opacity
		data[k+1] = opacity
		data[k+2] = opacity
		data[k+3] = opacity
	}
	return data, nil
}

func decodeDXT5(data []byte, width, height uint32) ([]byte, error) {
	utils.Debug("    Type: DXT5")
	data, err := dxt.DecodeDXT5(data, uint(width), uint(height))
	if err != nil {
		return nil, err
	}
	fixAlpha(data, int(width), int(height))
	return data, nil
}

func decodeDXT1(data []byte, width, height uint32) ([]byte, error) {
	utils.Debug("    Type: DXT1")
	data, err := dxt.DecodeDXT1(data, uint(width), uint(height))
	if err != nil {
		return nil, err
	}
	return data, nil
}

func decodeR8(finalData []byte, width, height uint32) ([]byte, error) {
	utils.Debug("    Type: R8 (Grayscale/Mask)")
	pix := make([]byte, width*height*4)
	for k := 0; k < int(width*height); k++ {
		val := finalData[k]
		pix[k*4] = val
		pix[k*4+1] = val
		pix[k*4+2] = val
		pix[k*4+3] = 255
	}
	return pix, nil
}

func decodeRG88(finalData []byte, width, height uint32) ([]byte, error) {
	utils.Debug("    Type: RG88")
	numPixels := int(width) * int(height)
	pix := make([]byte, numPixels*4)

	for i := 0; i < numPixels; i++ {
		lum := finalData[i*2+0]     // Byte 1: Luminance
		opacity := finalData[i*2+1] // Byte 2: Opacity

		// Write 4 bytes to destination
		pix[i*4+0] = lum     // R
		pix[i*4+1] = lum     // G
		pix[i*4+2] = lum     // B
		pix[i*4+3] = opacity // A
	}
	return pix, nil
}

func decompressLZ4(data []byte, isLZ4 bool, decompressedSize uint32, format uint32, width, height uint32) ([]byte, error) {
	requiredSize := width * height * 4
	finalData := data

	if !isLZ4 && format == 0 && uint32(len(data)) < requiredSize {
		utils.Warn("    Format 0 size mismatch (Got %d, Need %d). Force-enabling LZ4...", len(data), requiredSize)

		decodedLZ4 := make([]byte, requiredSize)
		n, err := lz4.UncompressBlock(data, decodedLZ4)
		if err == nil && uint32(n) == requiredSize {
			utils.Debug("    Forced LZ4 success!")
			finalData = decodedLZ4
		} else {
			utils.Warn("    Forced LZ4 failed: %v", err)
		}
	} else if isLZ4 {
		utils.Debug("    Decompressing LZ4: %d -> %d", len(data), decompressedSize)
		decodedLZ4 := make([]byte, decompressedSize)
		if _, err := lz4.UncompressBlock(data, decodedLZ4); err != nil {
			return nil, err
		}
		finalData = decodedLZ4
	}
	return finalData, nil
}

func DecodeTexToImage(path string) (image.Image, error) {
	utils.Debug("Decoding texture: %s", path)
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	// 1. Read Global Header
	magic1 := readString(f, 8)
	f.Seek(1, io.SeekCurrent)
	_ = readString(f, 8)
	f.Seek(1, io.SeekCurrent)

	if magic1 != "TEXV0005" {
		return nil, fmt.Errorf("invalid magic: %s", magic1)
	}

	format := readInt(f)
	f.Seek(4, io.SeekCurrent)

	// Read Aligned Dimensions (Important for Stride!)
	texW := readInt(f)
	_ = readInt(f)

	imgW := readInt(f)
	imgH := readInt(f)

	utils.Debug("    Magic: %s, Format: %d, Aligned: %d, Target: %dx%d", magic1, format, texW, imgW, imgH)

	readInt(f)
	containerMagic := readString(f, 8)
	f.Seek(1, io.SeekCurrent)
	imageCount := readInt(f)

	if containerMagic == "TEXB0003" {
		readInt(f)
	}

	for i := uint32(0); i < imageCount; i++ {
		mipmapCount := readInt(f)
		for j := uint32(0); j < mipmapCount; j++ {
			mW := readInt(f)
			mH := readInt(f)
			var isLZ4 bool
			var decompressedSize uint32

			// Check compression flag from header
			if containerMagic != "TEXB0001" {
				isLZ4 = readInt(f) == 1
				decompressedSize = readInt(f)
			}

			dataSize := readInt(f)
			data := make([]byte, dataSize)
			if _, err := io.ReadFull(f, data); err != nil {
				return nil, err
			}

			if i == 0 && j == 0 {
				if img, err := decodePNG(data, path); err != nil {
					return nil, err
				} else if img != nil {
					return img, nil
				}

				finalData, err := decompressLZ4(data, isLZ4, decompressedSize, format, mW, mH)
				if err != nil {
					return nil, err
				}
				// ----------------------------------------

				var pix []byte

				// Calculate expected sizes
				numBlocksW := (mW + 3) / 4
				numBlocksH := (mH + 3) / 4
				expectedDXT1 := numBlocksW * numBlocksH * 8
				expectedDXT5 := numBlocksW * numBlocksH * 16
				expectedRGBA := mW * mH * 4

				switch {
				case format == 0 || uint32(len(finalData)) == expectedRGBA:
					pix, err = decodeRGBA(finalData)

				case format == 9:
					pix, err = decodeR8(finalData, mW, mH)

				case format == 8:
					pix, err = decodeRG88(finalData, mW, mH)

				case format == 6 || uint32(len(finalData)) == expectedDXT5:
					pix, err = decodeDXT5(finalData, mW, mH)

				case format == 4 || format == 7 || uint32(len(finalData)) == expectedDXT1:
					pix, err = decodeDXT1(finalData, mW, mH)

				default:
					utils.Error("    Unknown format %d with data size %d", format, len(finalData))
					return nil, fmt.Errorf("decode failed: %v", err)

				}

				utils.Debug("    Successfully decoded: %s", path)
				rgbaImg := &image.RGBA{
					Pix:    pix,
					Stride: int(mW * 4),
					Rect:   image.Rect(0, 0, int(mW), int(mH)),
				}
				return rgbaImg.SubImage(image.Rect(0, 0, int(imgW), int(imgH))), nil
			}
		}
	}
	return nil, fmt.Errorf("no image found in texture")
}

func fixAlpha(pix []byte, width, height int) {
	const (
		alphaThreshold = 200
		edgeThreshold  = 2
	)
	stride := width * 4
	for y := 0; y < height; y++ {
		for x := 0; x < width; x++ {
			idx := (y*width + x) * 4
			alpha := pix[idx+3]
			if alpha > alphaThreshold {
				pix[idx+3] = 255
				continue
			}
			pix[idx+3] = 0
			if x == 0 || x == width-1 || y == 0 || y == height-1 {
				continue
			}
			left, right := pix[idx-4+3], pix[idx+4+3]
			up, down := pix[idx-stride+3], pix[idx+stride+3]
			if (left > edgeThreshold && right > edgeThreshold) || (up > edgeThreshold && down > edgeThreshold) {
				pix[idx+3] = 255
			}
		}
	}
}

func LoadTexture(path string) error {
	var pngPath string
	if TextureOutDir != "" {
		base := filepath.Base(path)
		pngPath = filepath.Join(TextureOutDir, strings.TrimSuffix(base, ".tex")+".png")
	} else {
		pngPath = strings.TrimSuffix(path, ".tex") + ".png"
	}

	if _, err := os.Stat(pngPath); err == nil {
		return nil
	}

	img, err := DecodeTexToImage(path)
	if err != nil {
		return err
	}

	if f, err := os.Create(pngPath); err == nil {
		if err := png.Encode(f, img); err != nil {
			utils.Error("Failed to encode PNG %s: %v", pngPath, err)
			f.Close()
			os.Remove(pngPath)
			return err
		} else {
			f.Close()
		}
	}

	return nil
}

func LoadTextureNative(path string) (*rl.Texture2D, error) {
	var pngPath string
	if TextureOutDir != "" {
		base := filepath.Base(path)
		pngPath = filepath.Join(TextureOutDir, strings.TrimSuffix(base, ".tex")+".png")
	} else {
		pngPath = strings.TrimSuffix(path, ".tex") + ".png"
	}

	if _, err := os.Stat(pngPath); err != nil {
		err = LoadTexture(path)
		if err != nil {
			return nil, err
		}
	}

	tex := rl.LoadTexture(pngPath)
	if tex.ID == 0 {
		return nil, fmt.Errorf("failed to load texture from %s", pngPath)
	}

	// Important: WE effects often rely on texture tiling/wrapping (e.g. noise scrolling)
	rl.SetTextureWrap(tex, rl.TextureWrapRepeat)

	return &tex, nil
}
