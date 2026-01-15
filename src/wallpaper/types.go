package wallpaper

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
	Sound            BindingString          `json:"sound"`
	Text             TextInfo               `json:"text"`
	VerticalAlign    string                 `json:"verticalalign"`
	Visible          BindingBool            `json:"visible"`
	Volume           BindingFloat           `json:"volume"`
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
	Value float64 `json:"value"`
}

type BindingBool struct {
	Value bool `json:"value"`
}

type BindingString struct {
	Value string `json:"value"`
}

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
	Textures             []string             `json:"textures"`
}

type ConstantShaderValues struct {
	Amount     BindingFloat `json:"amount"`
	Phase      BindingFloat `json:"phase"`
	PhaseScale BindingFloat `json:"phasescale"`
	Power      BindingFloat `json:"power"`
	Scale      BindingFloat `json:"scale"`
	Speed      BindingFloat `json:"speed"`
	Strength   BindingFloat `json:"strength"`
}

type ModelJSON struct {
	Material string `json:"material"`
}

type MaterialJSON struct {
	Passes []struct {
		Textures []string `json:"textures"`
	} `json:"passes"`
}

type ParticleJSON struct {
	Material           string                `json:"material"`
	MaxCount           int                   `json:"maxcount"`
	StartTime          float64               `json:"starttime"`
	Flags              int                   `json:"flags"`
	SequenceMultiplier float64               `json:"sequencemultiplier"`
	Emitter            []ParticleEmitter     `json:"emitter"`
	Initializer        []ParticleInitializer `json:"initializer"`
	Operator           []ParticleOperator    `json:"operator"`
	Renderer           []ParticleRenderer    `json:"renderer"`
	Children           []interface{}         `json:"children"`
	ControlPoint       []ControlPoint        `json:"controlpoint"`
}

type ParticleEmitter struct {
	ID          int          `json:"id"`
	Name        string       `json:"name"`
	Rate        BindingFloat `json:"rate"`
	Origin      Vec3         `json:"origin"`
	Directions  Vec3         `json:"directions"`
	DistanceMax interface{}  `json:"distancemax"` // Can be Vec3 or float64
	DistanceMin interface{}  `json:"distancemin"` // Can be Vec3 or float64
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
	Drag    BindingFloat `json:"drag"`
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
