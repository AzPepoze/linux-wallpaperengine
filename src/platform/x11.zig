const std = @import("std");

const xcb = @cImport({
    @cInclude("xcb/xcb.h");
    @cInclude("xcb/xproto.h");
});

pub const X11Context = struct {
    conn: *xcb.xcb_connection_t,
    screen: *xcb.xcb_screen_t,
    root: xcb.xcb_window_t,

    pub fn init() !X11Context {
        var screen_num: i32 = 0;
        const conn = xcb.xcb_connect(null, &screen_num) orelse return error.XcbConnectFailed;
        if (xcb.xcb_connection_has_error(conn) != 0) return error.XcbConnectionError;

        const setup = xcb.xcb_get_setup(conn);
        var iter = xcb.xcb_setup_roots_iterator(setup);
        
        var i: i32 = 0;
        while (i < screen_num) : (i += 1) {
            xcb.xcb_screen_next(&iter);
        }

        const screen = iter.data;
        return X11Context{
            .conn = conn,
            .screen = screen,
            .root = screen.*.root,
        };
    }

    pub fn deinit(self: *X11Context) void {
        xcb.xcb_disconnect(self.conn);
    }

    pub fn getGlobalMousePos(self: *X11Context) !struct { x: i16, y: i16 } {
        const cookie = xcb.xcb_query_pointer(self.conn, self.root);
        const reply = xcb.xcb_query_pointer_reply(self.conn, cookie, null) orelse return error.XcbReplyError;
        defer std.c.free(reply);

        return .{ .x = reply.*.root_x, .y = reply.*.root_y };
    }

    pub fn setAsDesktopBackground(self: *X11Context, window: xcb.xcb_window_t) !void {
        const atom_name = "_NET_WM_WINDOW_TYPE";
        const type_name = "_NET_WM_WINDOW_TYPE_DESKTOP";

        const type_cookie = xcb.xcb_intern_atom(self.conn, 0, @intCast(atom_name.len), atom_name);
        const desktop_cookie = xcb.xcb_intern_atom(self.conn, 0, @intCast(type_name.len), type_name);

        const type_reply = xcb.xcb_intern_atom_reply(self.conn, type_cookie, null) orelse return error.XcbAtomError;
        const desktop_reply = xcb.xcb_intern_atom_reply(self.conn, desktop_cookie, null) orelse return error.XcbAtomError;
        defer std.c.free(type_reply);
        defer std.c.free(desktop_reply);

        const type_atom = type_reply.*.atom;
        const desktop_atom = desktop_reply.*.atom;

        _ = xcb.xcb_change_property(
            self.conn,
            xcb.XCB_PROP_MODE_REPLACE,
            window,
            type_atom,
            xcb.XCB_ATOM_ATOM,
            32,
            1,
            &desktop_atom,
        );

        _ = xcb.xcb_flush(self.conn);
    }
};
