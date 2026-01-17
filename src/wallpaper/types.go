package wallpaper

import "strings"

type Vec2 struct {
	X, Y float64
}

type Vec3 struct {
	X, Y, Z float64
}

type Camera struct {
	Center Vec3 `json:"center"`
	Eye    Vec3 `json:"eye"`
	Up     Vec3 `json:"up"`
}

type Scene struct {
	Camera  Camera   `json:"camera"`
	General General  `json:"general"`
	Objects []Object `json:"objects"`
	Version int      `json:"version"`
}

type General struct {
	AmbientColor                 string               `json:"ambientcolor"`
	Bloom                        BindingBool          `json:"bloom"`
	BloomHDRFeather              float64              `json:"bloomhdrfeather"`
	BloomHDRIterations           int                  `json:"bloomhdriterations"`
	BloomHDRScatter              float64              `json:"bloomhdrscatter"`
	BloomHDRStrength             float64              `json:"bloomhdrstrength"`
	BloomHDRThreshold            float64              `json:"bloomhdrthreshold"`
	BloomStrength                float64              `json:"bloomstrength"`
	BloomThreshold               float64              `json:"bloomthreshold"`
	CameraFade                   bool                 `json:"camerafade"`
	CameraParallax               bool                 `json:"cameraparallax"`
	CameraParallaxAmount         float64              `json:"cameraparallaxamount"`
	CameraParallaxDelay          float64              `json:"cameraparallaxdelay"`
	CameraParallaxMouseInfluence float64              `json:"cameraparallaxmouseinfluence"`
	CameraPreview                bool                 `json:"camerapreview"`
	CameraShake                  BindingBool          `json:"camerashake"`
	CameraShakeAmplitude         float64              `json:"camerashakeamplitude"`
	CameraShakeRoughness         float64              `json:"camerashakeroughness"`
	CameraShakeSpeed             float64              `json:"camerashakespeed"`
	ClearColor                   string               `json:"clearcolor"`
	ClearEnabled                 bool                 `json:"clearenabled"`
	FarZ                         float64              `json:"farz"`
	FOV                          float64              `json:"fov"`
	HDR                          bool                 `json:"hdr"`
	NearZ                        float64              `json:"nearz"`
	OrthogonalProjection         OrthogonalProjection `json:"orthogonalprojection"`
	SkylightColor                string               `json:"skylightcolor"`
	Zoom                         float64              `json:"zoom"`
}

type OrthogonalProjection struct {
	Width  int `json:"width"`
	Height int `json:"height"`
}

type Object struct {
	ID               int                    `json:"id"`
	Name             string                 `json:"name"`
	Alignment        string                 `json:"alignment"`
	Alpha            BindingFloat           `json:"alpha"`
	Angles           Vec3                   `json:"angles"`
	Brightness       float64                `json:"brightness"`
	Color            string                 `json:"color"`
	ColorBlendMode   int                    `json:"colorBlendMode"`
	Config           map[string]interface{} `json:"config"`
	CopyBackground   bool                   `json:"copybackground"`
	Effects          []Effect               `json:"effects"`
	HorizontalAlign  string                 `json:"horizontalalign"`
	Image            string                 `json:"image"`
	InstanceOverride *InstanceOverride      `json:"instanceoverride"`
	LEDSource        bool                   `json:"ledsource"`
	LockTransforms   bool                   `json:"locktransforms"`
	Model            string                 `json:"model"`
	Origin           Vec3                   `json:"origin"`
	ParallaxDepth    Vec2                   `json:"parallaxDepth"`
	Particle         string                 `json:"particle"`
	Perspective      bool                   `json:"perspective"`
	Pointsize        BindingFloat           `json:"pointsize"`
	Scale            Vec3                   `json:"scale"`
	Size             Vec2                   `json:"size"`
	Solid            bool                   `json:"solid"`
	Sound            BindingStringArray     `json:"sound"`
	Text             TextInfo               `json:"text"`
	VerticalAlign    string                 `json:"verticalalign"`
	Visible          BindingBool            `json:"visible"`
	Volume           BindingFloat           `json:"volume"`
	Intensity        BindingFloat           `json:"intensity"`
	Light            string                 `json:"light"`
	MaxTime          float64                `json:"maxtime"`
	MinTime          float64                `json:"mintime"`
	MuteInEditor     bool                   `json:"muteineditor"`
	PlaybackMode     string                 `json:"playbackmode"`
	StartSilent      bool                   `json:"startsilent"`
	Radius           float64                `json:"radius"`
}

type InstanceOverride struct {
	ID         int          `json:"id"`
	Alpha      BindingFloat `json:"alpha"`
	Brightness BindingFloat `json:"brightness"`
	ColorN     string       `json:"colorn"`
	Count      BindingFloat `json:"count"`
	Lifetime   BindingFloat `json:"lifetime"`
	Rate       BindingFloat `json:"rate"`
	Size       BindingFloat `json:"size"`
	Speed      BindingFloat `json:"speed"`
}

type BindingFloat struct {
	Value interface{} `json:"value"` // Can be float64 or Animation
}

func (bf BindingFloat) GetFloat() float64 {
	switch v := bf.Value.(type) {
	case float64:
		return v
	case int:
		return float64(v)
	case map[string]interface{}:
		if val, ok := v["value"].(float64); ok {
			return val
		}
	}
	return 0
}

type Animation struct {
	Animation struct {
		C0 []Keyframe `json:"c0"`
		Options struct {
			FPS      float64 `json:"fps"`
			Length   float64 `json:"length"`
			Mode     string  `json:"mode"`
			WrapLoop bool    `json:"wraploop"`
		} `json:"options"`
	} `json:"animation"`
}

type Keyframe struct {
	Frame int `json:"frame"`
	Value float64 `json:"value"`
	Back  struct {
		Enabled bool    `json:"enabled"`
		X       float64 `json:"x"`
		Y       float64 `json:"y"`
	} `json:"back"`
	Front struct {
		Enabled bool    `json:"enabled"`
		X       float64 `json:"x"`
		Y       float64 `json:"y"`
	} `json:"front"`
	LockAngle  bool `json:"lockangle"`
	LockLength bool `json:"locklength"`
}

type BindingBool struct {
	Value interface{} `json:"value"` // Can be bool or struct { User string, Value bool }
}

func (bb BindingBool) GetBool() bool {
	switch v := bb.Value.(type) {
	case bool:
		return v
	case map[string]interface{}:
		if val, ok := v["value"].(bool); ok {
			return val
		}
	}
	return true // Default to true if unsure? Or false. Existing code mostly checks !visible.
}

type BindingString struct {
	Value string `json:"value"`
}

// Added BindingStringArray to handle "sound" field which is array of strings
type BindingStringArray []string

type TextInfo struct {
	Value            string `json:"value"`
	Script           string `json:"script"`
	ScriptProperties struct {
		Format struct {
			Value string `json:"value"`
		} `json:"format"`
	} `json:"scriptproperties"`
}

type Effect struct {
	ID      int          `json:"id"`
	Name    string       `json:"name"`
	File    string       `json:"file"`
	Alpha   BindingFloat `json:"alpha"`
	Visible BindingBool  `json:"visible"`
	Passes  []EffectPass `json:"passes"`
}

type EffectPass struct {
	ID                   int                  `json:"id"`
	ConstantValue        float64              `json:"constantvalue"`
	ConstantColor        Vec3                 `json:"constantcolor"`
	ConstantShaderValues ConstantShaderValues `json:"constantshadervalues"`
	Textures             []*string            `json:"textures"` // Pointer to string to handle nulls
	Combos               map[string]int       `json:"combos"`
}

type ConstantShaderValues map[string]interface{}

func (c ConstantShaderValues) GetFloat(key string) float64 {
	val, ok := c[key]
	if !ok {
		// Try lowercase
		val, ok = c[strings.ToLower(key)]
		if !ok {
			return 0
		}
	}

	switch v := val.(type) {
	case float64:
		return v
	case map[string]interface{}:
		if val, ok := v["value"].(float64); ok {
			return val
		}
		// Handle animation inside constant shader value if needed (future proofing)
	}
	return 0
}

type ModelJSON struct {
	Material string `json:"material"`
	Puppet   string `json:"puppet"`
	Autosize bool   `json:"autosize"`
}

type MaterialJSON struct {
	Passes []struct {
		Textures             []string              `json:"textures"`
		Blending             string                `json:"blending"`
		CullMode             string                `json:"cullmode"`
		DepthTest            string                `json:"depthtest"`
		DepthWrite           string                `json:"depthwrite"`
		Shader               string                `json:"shader"`
		Combos               map[string]int        `json:"combos"`
		ConstantShaderValues ConstantShaderValues  `json:"constantshadervalues"`
	} `json:"passes"`
}

type ParticleJSON struct {
	Material           string                `json:"material"`
	MaxCount           int                   `json:"maxcount"`
	StartTime          float64               `json:"starttime"`
	Flags              interface{}           `json:"flags"` // Can be null or int
	SequenceMultiplier float64               `json:"sequencemultiplier"`
	AnimationMode      string                `json:"animationmode"`
	Emitter            []ParticleEmitter     `json:"emitter"`
	Initializer        []ParticleInitializer `json:"initializer"`
	Operator           []ParticleOperator    `json:"operator"`
	Renderer           []ParticleRenderer    `json:"renderer"`
	Children           []interface{}         `json:"children"`
	ControlPoint       []ControlPoint        `json:"controlpoint"`
}

type ParticleEmitter struct {
	ID                         int          `json:"id"`
	Name                       string       `json:"name"`
	Rate                       interface{}  `json:"rate"` // Can be BindingFloat or float64
	Origin                     interface{}  `json:"origin"` // Can be Vec3 or string
	Directions                 interface{}  `json:"directions"` // Can be Vec3 or string
	DistanceMax                interface{}  `json:"distancemax"` // Can be Vec3 or float64
	DistanceMin                interface{}  `json:"distancemin"` // Can be Vec3 or float64
	AudioProcessingBounds      string       `json:"audioprocessingbounds"`
	AudioProcessingExponent    float64      `json:"audioprocessingexponent"`
	AudioProcessingFrequencyEnd float64      `json:"audioprocessingfrequencyend"`
	AudioProcessingMode        int          `json:"audioprocessingmode"`
}

type ParticleInitializer struct {
	ID       int         `json:"id"`
	Name     string      `json:"name"`
	Min      interface{} `json:"min"` // Can be float64, Vec3, or string
	Max      interface{} `json:"max"` // Can be float64, Vec3, or string
	Exponent float64     `json:"exponent"`
}

type ParticleOperator struct {
	ID   int    `json:"id"`
	Name string `json:"name"`
	// Movement operator
	Gravity interface{}  `json:"gravity"` // Can be BindingFloat or Vec3
	Drag    interface{}  `json:"drag"` // Can be BindingFloat or float
	// Alpha fade operator
	FadeInTime  float64 `json:"fadeintime"`
	FadeOutTime float64 `json:"fadeouttime"`
	// Control point attract operator
	ControlPoint int     `json:"controlpoint"`
	Origin       Vec3    `json:"origin"`
	Scale        float64 `json:"scale"`
	Threshold    float64 `json:"threshold"`
	// Turbulence operator
	TimeScale float64 `json:"timescale"`
	SpeedMin  float64 `json:"speedmin"`
	SpeedMax  float64 `json:"speedmax"`
	// Color change operator
	StartTime  float64     `json:"starttime"`
	StartValue interface{} `json:"startvalue"`
	EndTime    float64     `json:"endtime"`
	EndValue   interface{} `json:"endvalue"`
	// Oscillate alpha operator
	FrequencyMax float64 `json:"frequencymax"`
	FrequencyMin float64 `json:"frequencymin"`
	ScaleMin     float64 `json:"scalemin"`
	ScaleMax     float64 `json:"scalemax"`
}

type ParticleRenderer struct {
	ID        int     `json:"id"`
	Name      string  `json:"name"`
	Length    float64 `json:"length"`
	MaxLength float64 `json:"maxlength"`
}

type ControlPoint struct {
	ID            int  `json:"id"`
	Flags         int  `json:"flags"`
	LockToPointer bool `json:"locktopointer"`
	Offset        Vec3 `json:"offset"`
}

type TexJSON struct {
	ClampUVs             bool                  `json:"clampuvs"`
	Format               string                `json:"format"`
	NonPowerOfTwo        bool                  `json:"nonpoweroftwo"`
	SpriteSheetSequences []SpriteSheetSequence `json:"spritesheetsequences"`
}

type SpriteSheetSequence struct {
	Duration float64 `json:"duration"`
	Frames   int     `json:"frames"`
	Height   int     `json:"height"`
	Width    int     `json:"width"`
}
