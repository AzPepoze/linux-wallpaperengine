package main

type Scene struct {
	General struct {
		OrthogonalProjection struct {
			Width  int `json:"width"`
			Height int `json:"height"`
		} `json:"orthogonalprojection"`
		ClearColor string `json:"clearcolor"`
	} `json:"general"`
	Objects []Object `json:"objects"`
}

type Object struct {
	Name    string      `json:"name"`
	Image   string      `json:"image"`
	Origin  interface{} `json:"origin"`
	Size    interface{} `json:"size"`
	Visible interface{} `json:"visible"`
}
