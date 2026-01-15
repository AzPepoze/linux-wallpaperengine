package main

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
	switch l {
	case LevelInfo:
		return "INFO"
	case LevelDebug:
		return "DEBUG"
	case LevelWarn:
		return "WARN"
	case LevelError:
		return "ERROR"
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
