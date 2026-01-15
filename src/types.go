package main

import (
	"encoding/json"
	"fmt"
	"strconv"
	"strings"
)

type Vec2 struct {
	X, Y float64
}

func (v *Vec2) UnmarshalJSON(data []byte) error {
	var s string
	if err := json.Unmarshal(data, &s); err != nil {
		// Try unmarshaling as float (some fields might be a single number)
		var f float64
		if err := json.Unmarshal(data, &f); err == nil {
			v.X, v.Y = f, f
			return nil
		}

		// Try unmarshaling as an object (e.g., {"value": "x y", "script": "..."})
		var m map[string]interface{}
		if err := json.Unmarshal(data, &m); err == nil {
			if val, ok := m["value"].(string); ok {
				return v.fromString(val)
			}
			return nil
		}
		return err
	}
	return v.fromString(s)
}

func (v *Vec2) fromString(s string) error {
	parts := strings.Fields(s)
	if len(parts) < 2 {
		if len(parts) == 1 {
			f, _ := strconv.ParseFloat(parts[0], 64)
			v.X, v.Y = f, f
		}
		return nil
	}
	v.X, _ = strconv.ParseFloat(parts[0], 64)
	v.Y, _ = strconv.ParseFloat(parts[1], 64)
	return nil
}

type Vec3 struct {
	X, Y, Z float64
}

func (v *Vec3) UnmarshalJSON(data []byte) error {
	var s string
	if err := json.Unmarshal(data, &s); err != nil {
		var f float64
		if err := json.Unmarshal(data, &f); err == nil {
			v.X, v.Y, v.Z = f, f, f
			return nil
		}

		var m map[string]interface{}
		if err := json.Unmarshal(data, &m); err == nil {
			if val, ok := m["value"].(string); ok {
				return v.fromString(val)
			}
			return nil
		}
		return err
	}
	return v.fromString(s)
}

func (v *Vec3) fromString(s string) error {
	parts := strings.Fields(s)
	if len(parts) < 3 {
		if len(parts) == 1 {
			f, _ := strconv.ParseFloat(parts[0], 64)
			v.X, v.Y, v.Z = f, f, f
		}
		return nil
	}
	v.X, _ = strconv.ParseFloat(parts[0], 64)
	v.Y, _ = strconv.ParseFloat(parts[1], 64)
	v.Z, _ = strconv.ParseFloat(parts[2], 64)
	return nil
}

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
	Name    string  `json:"name"`
	Image   string  `json:"image"`
	Origin  Vec2    `json:"origin"`
	Size    Vec2    `json:"size"`
	Scale   Vec3    `json:"scale"`
	Angles  Vec3    `json:"angles"`
	Alpha   float64 `json:"alpha"`
	Visible bool    `json:"visible"`
}

func (o *Object) UnmarshalJSON(data []byte) error {
	type Alias Object
	aux := &struct {
		Visible interface{} `json:"visible"`
		Alpha   interface{} `json:"alpha"`
		*Alias
	}{
		Alias: (*Alias)(o),
	}

	// Default values
	o.Alpha = 1.0
	o.Scale = Vec3{1, 1, 1}

	if err := json.Unmarshal(data, &aux); err != nil {
		return err
	}

	switch v := aux.Visible.(type) {
	case bool:
		o.Visible = v
	case float64:
		o.Visible = v != 0
	case string:
		o.Visible = strings.ToLower(v) == "true"
	case map[string]interface{}:
		if val, ok := v["value"].(bool); ok {
			o.Visible = val
		} else {
			o.Visible = true
		}
	default:
		o.Visible = true
	}

	if aux.Alpha != nil {
		switch v := aux.Alpha.(type) {
		case float64:
			o.Alpha = v
		case map[string]interface{}:
			if val, ok := v["value"].(float64); ok {
				o.Alpha = val
			}
		}
	}

	return nil
}

func parseColor(s string) (float64, float64, float64) {
	var r, g, b float64
	fmt.Sscanf(s, "%f %f %f", &r, &g, &b)
	return r, g, b
}
