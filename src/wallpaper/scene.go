package wallpaper

import (
	"encoding/json"
	"strconv"
	"strings"
)

func (vec2 *Vec2) UnmarshalJSON(data []byte) error {
	var str string
	if err := json.Unmarshal(data, &str); err == nil {
		fields := strings.Fields(str)
		if len(fields) >= 2 {
			vec2.X, _ = strconv.ParseFloat(fields[0], 64)
			vec2.Y, _ = strconv.ParseFloat(fields[1], 64)
		} else if len(fields) == 1 {
			value, _ := strconv.ParseFloat(fields[0], 64)
			vec2.X, vec2.Y = value, value
		}
		return nil
	}
	var floatVal float64
	if err := json.Unmarshal(data, &floatVal); err == nil {
		vec2.X, vec2.Y = floatVal, floatVal
		return nil
	}
	var result struct {
		X float64 `json:"x"`
		Y float64 `json:"y"`
	}
	if err := json.Unmarshal(data, &result); err == nil {
		vec2.X, vec2.Y = result.X, result.Y
		return nil
	}
	return nil
}

func (vec3 *Vec3) UnmarshalJSON(data []byte) error {
	var str string
	if err := json.Unmarshal(data, &str); err == nil {
		fields := strings.Fields(str)
		if len(fields) >= 3 {
			vec3.X, _ = strconv.ParseFloat(fields[0], 64)
			vec3.Y, _ = strconv.ParseFloat(fields[1], 64)
			vec3.Z, _ = strconv.ParseFloat(fields[2], 64)
		} else if len(fields) == 1 {
			value, _ := strconv.ParseFloat(fields[0], 64)
			vec3.X, vec3.Y, vec3.Z = value, value, value
		}
		return nil
	}
	var floatVal float64
	if err := json.Unmarshal(data, &floatVal); err == nil {
		vec3.X, vec3.Y, vec3.Z = floatVal, floatVal, floatVal
		return nil
	}
	return nil
}

func (binding *BindingFloat) UnmarshalJSON(data []byte) error {
	var floatVal float64
	if err := json.Unmarshal(data, &floatVal); err == nil {
		binding.Value = floatVal
		return nil
	}
	var str string
	if err := json.Unmarshal(data, &str); err == nil {
		if parsedValue, err := strconv.ParseFloat(str, 64); err == nil {
			binding.Value = parsedValue
			return nil
		}
	}
	type Alias BindingFloat
	var temp struct {
		Value interface{} `json:"value"`
	}
	if err := json.Unmarshal(data, &temp); err == nil {
		switch value := temp.Value.(type) {
		case float64:
			binding.Value = value
		case string:
			if parsedValue, err := strconv.ParseFloat(value, 64); err == nil {
				binding.Value = parsedValue
			}
		}
		return nil
	}
	return nil
}

func (binding *BindingBool) UnmarshalJSON(data []byte) error {
	var boolVal bool
	if err := json.Unmarshal(data, &boolVal); err == nil {
		binding.Value = boolVal
		return nil
	}
	type Alias BindingBool
	var temp struct {
		Value bool `json:"value"`
	}
	if err := json.Unmarshal(data, &temp); err == nil {
		binding.Value = temp.Value
		return nil
	}
	return nil
}

func (binding *BindingString) UnmarshalJSON(data []byte) error {
	var str string
	if err := json.Unmarshal(data, &str); err == nil {
		binding.Value = str
		return nil
	}
	var strArray []string
	if err := json.Unmarshal(data, &strArray); err == nil {
		if len(strArray) > 0 {
			binding.Value = strArray[0]
		}
		return nil
	}
	return nil
}

func (textInfo *TextInfo) UnmarshalJSON(data []byte) error {
	var str string
	if err := json.Unmarshal(data, &str); err == nil {
		textInfo.Value = str
		return nil
	}
	type Alias TextInfo
	temp := &struct {
		*Alias
	}{
		Alias: (*Alias)(textInfo),
	}
	return json.Unmarshal(data, &temp)
}

func (obj *Object) UnmarshalJSON(data []byte) error {
	obj.Alpha = BindingFloat{Value: 1.0}
	obj.Scale = Vec3{1, 1, 1}
	obj.Visible = BindingBool{Value: true}

	type Alias Object
	return json.Unmarshal(data, (*Alias)(obj))
}

func (obj *Object) GetText() string {
	return obj.Text.Value
}

func ParseColor(colorStr string) (float64, float64, float64) {
	colorParts := strings.Fields(colorStr)
	if len(colorParts) < 3 {
		return 0, 0, 0
	}
	red, _ := strconv.ParseFloat(colorParts[0], 64)
	green, _ := strconv.ParseFloat(colorParts[1], 64)
	blue, _ := strconv.ParseFloat(colorParts[2], 64)
	return red, green, blue
}
