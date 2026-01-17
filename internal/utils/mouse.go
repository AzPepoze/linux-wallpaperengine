package utils

import (
	"github.com/jezek/xgb"
	"github.com/jezek/xgb/xproto"
)

var (
	XConn *xgb.Conn
	XRoot xproto.Window
)

func InitX11() error {
	var err error
	XConn, err = xgb.NewConn()
	if err != nil {
		return err
	}

	setup := xproto.Setup(XConn)
	XRoot = setup.DefaultScreen(XConn).Root
	return nil
}

func GetGlobalMousePosition() (int, int, error) {
	if XConn == nil {
		if err := InitX11(); err != nil {
			return 0, 0, err
		}
	}

	reply, err := xproto.QueryPointer(XConn, XRoot).Reply()
	if err != nil {
		return 0, 0, err
	}

	return int(reply.RootX), int(reply.RootY), nil
}
