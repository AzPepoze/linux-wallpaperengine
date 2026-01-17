package utils

import (
	"fmt"
	"log"
	"strings"
)

type LogLevel int

const (
	LevelDebug LogLevel = iota
	LevelInfo
	LevelWarn
	LevelError
)

var (
	DebugMode      bool
	CurrentLevel   LogLevel = LevelWarn
	ShowRaylibInfo bool
	ShowDebugUI    bool
	SilentMode     bool

	GPURenderer string
	GPUVendor   string
	GLVersion   string
)

func (l LogLevel) String() string {
	switch l {
	case LevelDebug:
		return "DEBUG"
	case LevelInfo:
		return "INFO"
	case LevelWarn:
		return "WARN"
	case LevelError:
		return "ERROR"
	}
	return "UNKNOWN"
}

func logMessage(level LogLevel, format string, v ...interface{}) {
	if level < CurrentLevel {
		return
	}

	const (
		colorReset  = "\033[0m"
		colorCyan   = "\033[36m"
		colorBlue   = "\033[34m"
		colorYellow = "\033[33m"
		colorRed    = "\033[31m"
	)

	var colorCode string
	switch level {
	case LevelDebug:
		colorCode = colorCyan
	case LevelInfo:
		colorCode = colorBlue
	case LevelWarn:
		colorCode = colorYellow
	case LevelError:
		colorCode = colorRed
	}

	prefix := fmt.Sprintf("%s[%s]%s ", colorCode, level.String(), colorReset)
	log.Printf(prefix+format, v...)
}

func Info(format string, v ...interface{})  { logMessage(LevelInfo, format, v...) }
func Debug(format string, v ...interface{}) { logMessage(LevelDebug, format, v...) }
func Warn(format string, v ...interface{})  { logMessage(LevelWarn, format, v...) }
func Error(format string, v ...interface{}) { logMessage(LevelError, format, v...) }

func Dump(label string, v interface{}) {
	if CurrentLevel > LevelDebug {
		return
	}
	raw := fmt.Sprintf("%#v", v)
	indent := 0
	var formatted strings.Builder
	for i := 0; i < len(raw); i++ {
		char := raw[i]
		switch char {
		case '{', '[':
			formatted.WriteByte(char)
			formatted.WriteByte('\n')
			indent++
			formatted.WriteString(strings.Repeat("  ", indent))
		case '}', ']':
			formatted.WriteByte('\n')
			indent--
			if indent < 0 {
				indent = 0
			}
			formatted.WriteString(strings.Repeat("  ", indent))
			formatted.WriteByte(char)
		case ',':
			formatted.WriteByte(char)
			formatted.WriteByte('\n')
			formatted.WriteString(strings.Repeat("  ", indent))
			if i+1 < len(raw) && raw[i+1] == ' ' {
				i++
			}
		default:
			formatted.WriteByte(char)
		}
	}
	Debug("%s:\n%s", label, formatted.String())
}

func RaylibLogCallback(level int, text string) {
	// Capture GPU info from Raylib startup logs
	if strings.Contains(text, "> Vendor:") {
		GPUVendor = strings.TrimSpace(strings.TrimPrefix(text, "    > Vendor:"))
	}
	if strings.Contains(text, "> Renderer:") {
		GPURenderer = strings.TrimSpace(strings.TrimPrefix(text, "    > Renderer:"))
	}
	if strings.Contains(text, "> Version:") {
		GLVersion = strings.TrimSpace(strings.TrimPrefix(text, "    > Version:"))
	}

	const colorMagenta = "\033[35m"
	const colorReset = "\033[0m"
	formattedText := colorMagenta + "[RAYLIB] " + colorReset + text
	switch level {
	case 2: // LOG_TRACE, LOG_DEBUG
		if CurrentLevel <= LevelDebug {
			Debug(formattedText)
		}
	case 3: // LOG_INFO
		if ShowRaylibInfo || CurrentLevel <= LevelInfo {
			Info(formattedText)
		}
	case 4: // LOG_WARNING
		Warn(formattedText)
	case 5, 6: // LOG_ERROR, LOG_FATAL
		Error(formattedText)
	}
}
