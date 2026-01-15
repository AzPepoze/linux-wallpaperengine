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

	"github.com/hajimehoshi/ebiten/v2"
	"github.com/hajimehoshi/ebiten/v2/ebitenutil"
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

func DecodeTexToImage(path string) (image.Image, error) {
	utils.Debug("Decoding texture: %s", path)
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	magic1 := readString(f, 8)
	f.Seek(1, io.SeekCurrent)
	_ = readString(f, 8)
	f.Seek(1, io.SeekCurrent)

	if magic1 != "TEXV0005" {
		return nil, fmt.Errorf("invalid magic: %s", magic1)
	}

	format := readInt(f)
	f.Seek(4, io.SeekCurrent)
	_ = readInt(f)
	_ = readInt(f)
	imgW := readInt(f)
	imgH := readInt(f)

	utils.Debug("    Format: %d, Target Size: %dx%d", format, imgW, imgH)

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
				finalData := data
				if isLZ4 {
					utils.Debug("    Decompressing LZ4: %d -> %d", dataSize, decompressedSize)
					decodedLZ4 := make([]byte, decompressedSize)
					if _, err := lz4.UncompressBlock(data, decodedLZ4); err != nil {
						return nil, err
					}
					finalData = decodedLZ4
				}

				numBlocksW := (mW + 3) / 4
				numBlocksH := (mH + 3) / 4

				expectedDXT1 := (mW + 3) / 4 * (mH + 3) / 4 * 8
				expectedDXT5 := numBlocksW * numBlocksH * 16
				expectedRGBA := mW * mH * 4

				var pix []byte
				var err error

				switch {
				case uint32(len(finalData)) == expectedRGBA:
					utils.Debug("    Type: RGBA")
					pix = finalData
					// swapRB(pix)
					for k := 0; k < len(finalData); k += 4 {
						opacity := pix[k+3]
						pix[k] = opacity
						pix[k+1] = opacity
						pix[k+2] = opacity
						pix[k+3] = opacity
					}
				case uint32(len(finalData)) == expectedDXT5 || format == 6:
					utils.Debug("    Type: DXT5")
					pix, err = dxt.DecodeDXT5(finalData, uint(mW), uint(mH))
					if err != nil {
						return nil, err
					}
					fixAlpha(pix, int(mW), int(mH))
				case uint32(len(finalData)) == expectedDXT1 || format == 4 || format == 7:
					utils.Debug("    Type: DXT1")
					pix, err = dxt.DecodeDXT1(finalData, uint(mW), uint(mH))
					if err != nil {
						return nil, err
					}
				case format == 9 && uint32(len(finalData)) == expectedRGBA/4:
					utils.Debug("    Type: R8 (Grayscale/Mask)")
					pix = make([]byte, mW*mH*4)
					for k := 0; k < int(mW*mH); k++ {
						val := finalData[k]
						pix[k*4] = val
						pix[k*4+1] = val
						pix[k*4+2] = val
						pix[k*4+3] = 255
					}
				case format == 8 && uint32(len(finalData)) == expectedRGBA/2:
					utils.Debug("    Type: RG88")
					numPixels := int(mW) * int(mH)
					pix = make([]byte, numPixels*4)

					for i := 0; i < numPixels; i++ {
						lum := finalData[i*2+1] // Byte 2: Opacity

						// Write 4 bytes to destination
						pix[i*4+0] = lum // R
						pix[i*4+1] = lum // G
						pix[i*4+2] = lum // B
						pix[i*4+3] = lum // A
					}
				default:
					return nil, fmt.Errorf("unsupported format %d with size %d", format, len(finalData))
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

func LoadTexture(path string) (*ebiten.Image, error) {
	var pngPath string
	if TextureOutDir != "" {
		base := filepath.Base(path)
		pngPath = filepath.Join(TextureOutDir, strings.TrimSuffix(base, ".tex")+".png")
	} else {
		pngPath = strings.TrimSuffix(path, ".tex") + ".png"
	}

	if _, err := os.Stat(pngPath); err == nil {
		img, _, err := ebitenutil.NewImageFromFile(pngPath)
		return img, err
	}

	img, err := DecodeTexToImage(path)
	if err != nil {
		return nil, err
	}

	if f, err := os.Create(pngPath); err == nil {
		if err := png.Encode(f, img); err != nil {
			utils.Warn("Failed to encode PNG %s: %v", pngPath, err)
			f.Close()
			os.Remove(pngPath)
		} else {
			f.Close()
		}
	}

	return ebiten.NewImageFromImage(img), nil
}
