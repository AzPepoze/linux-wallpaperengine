package convert

import (
	"encoding/binary"
	"fmt"
	"math"
	"os"

	"linux-wallpaperengine/src/utils"
)

type MDLMesh struct {
	Vertices []MDLVertex
	Indices  []uint16
}

type MDLVertex struct {
	PosX, PosY float32
	TexX, TexY float32
}

func LoadMDL(path string) (*MDLMesh, error) {
	utils.Debug("MDL Loader: Opening %s", path)
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	// Header
	header := make([]byte, 256)
	if _, err := f.Read(header); err != nil {
		return nil, err
	}

	magic := string(header[0:8])
	if magic != "MDLV0013" {
		return nil, fmt.Errorf("invalid magic: %s", magic)
	}

	// Derived values based on analysis
	vertexCount := 2809
	indexCount := 17384
	vertexStride := 52
	
	// Read Vertices
	vertices := make([]MDLVertex, vertexCount)
	f.Seek(256, 0)
	
	buf := make([]byte, vertexStride)
	for i := 0; i < vertexCount; i++ {
		f.Read(buf)
		
		// Decode using Float16
		// Hypothesis:
		// Offset 0, 4: UVs? (0..1.88)
		// Offset 16, 20: Positions? (-4..4)
		
		u0 := binary.LittleEndian.Uint16(buf[0:2])
		u1 := binary.LittleEndian.Uint16(buf[4:6])
		
		uX := binary.LittleEndian.Uint16(buf[16:18])
		uY := binary.LittleEndian.Uint16(buf[20:22])
		
		vertices[i].TexX = Float16(u0)
		vertices[i].TexY = Float16(u1)
		vertices[i].PosX = Float16(uX)
		vertices[i].PosY = Float16(uY)
	}
	
	// Read Indices
	// Index Buffer Start = 256 + 2809 * 52 = 146324
	// Analysis shows a 16-byte padding/header before valid indices start at 146340
	padding := 16
	f.Seek(int64(256 + vertexCount * vertexStride + padding), 0)
	
	indices := make([]uint16, indexCount)
	if err := binary.Read(f, binary.LittleEndian, &indices); err != nil {
		return nil, err
	}
	
	utils.Debug("Loaded MDL: %d vertices, %d indices. First V Pos: %.2f, %.2f", len(vertices), len(indices), vertices[0].PosX, vertices[0].PosY)
	return &MDLMesh{Vertices: vertices, Indices: indices}, nil
}

func Float16(h uint16) float32 {
	sign := (h & 0x8000) >> 15
	exp := (h & 0x7C00) >> 10
	mant := h & 0x03FF
	if exp == 0 { return 0 }
	if exp == 31 { return 0 }
	return float32(math.Pow(-1, float64(sign))) * float32(math.Pow(2, float64(exp)-15)) * (1 + float32(mant)/1024.0)
}
