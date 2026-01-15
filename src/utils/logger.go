package utils

import (
	"fmt"
	"log"
)

type LogLevel int

const (
	LevelInfo LogLevel = iota
	LevelDebug
	LevelWarn
	LevelError
)

var DebugMode bool

func (l LogLevel) String() string {
	// ANSI color codes
	const (
		colorReset  = "\033[0m"
		colorBlue   = "\033[34m"
		colorCyan   = "\033[36m"
		colorYellow = "\033[33m"
		colorRed    = "\033[31m"
	)

	switch l {
	case LevelInfo:
		return colorBlue + "INFO" + colorReset
	case LevelDebug:
		return colorCyan + "DEBUG" + colorReset
	case LevelWarn:
		return colorYellow + "WARN" + colorReset
	case LevelError:
		return colorRed + "ERROR" + colorReset
	}
	return "UNKNOWN"
}

func logMessage(level LogLevel, format string, v ...interface{}) {
	if level == LevelDebug && !DebugMode {
		return
	}
	prefix := fmt.Sprintf("[%5s] ", level.String())
	log.Printf(prefix+format, v...)
}

func Info(format string, v ...interface{})  { logMessage(LevelInfo, format, v...) }
func Debug(format string, v ...interface{}) { logMessage(LevelDebug, format, v...) }
func Warn(format string, v ...interface{})  { logMessage(LevelWarn, format, v...) }
func Error(format string, v ...interface{}) { logMessage(LevelError, format, v...) }
