const std = @import("std");
const sokol = @import("sokol");
const sapp = sokol.app;
const lifecycle = @import("lifecycle.zig");
const core = @import("core");
const logger = core.logger;

/// EventHandler processes Sokol events and updates runtime state
pub const EventHandler = struct {
    runtime: ?*lifecycle.RuntimeState = null,

    pub fn init(runtime: *lifecycle.RuntimeState) EventHandler {
        return .{
            .runtime = runtime,
        };
    }

    pub fn deinit(self: *EventHandler) void {
        self.runtime = null;
    }

    /// Process mouse motion event
    pub fn handleMouseMove(self: *EventHandler, x: f32, y: f32) void {
        if (self.runtime) |rt| {
            rt.input.mouse_x = x;
            rt.input.mouse_y = y;
        }
    }

    /// Process mouse button event
    pub fn handleMouseButton(self: *EventHandler, pressed: bool) void {
        if (self.runtime) |rt| {
            rt.input.mouse_down = pressed;
        }
    }

    /// Process keyboard input event
    pub fn handleKeyDown(self: *EventHandler, key: sapp.Keycode) void {
        if (self.runtime) |rt| {
            switch (key) {
                .F8 => {
                    rt.toggleDebugUI();
                },
                else => {},
            }
        }
    }

    /// Process window resize
    pub fn handleResize(self: *EventHandler, width: i32, height: i32) void {
        if (self.runtime) |rt| {
            rt.input.window_width = width;
            rt.input.window_height = height;
            logger.App.info("Window resized: {}x{}", .{ width, height });
        }
    }

    /// Process focus change
    pub fn handleFocusChange(self: *EventHandler, focused: bool) void {
        if (self.runtime) |rt| {
            if (!focused) {
                rt.input.mouse_down = false;
                logger.App.info("Window lost focus, clearing input state", .{});
            }
        }
    }
};

/// Global event handler instance (set by main during init)
var global_event_handler: ?*EventHandler = null;

/// Set global event handler (called from main)
pub fn setGlobalEventHandler(handler: *EventHandler) void {
    global_event_handler = handler;
}

/// Sokol event callback bridge
pub fn sokolEventCallback(ev: [*c]const sapp.Event) callconv(.c) void {
    if (global_event_handler) |handler| {
        switch (ev.*.type) {
            .MOUSE_MOVE => {
                handler.handleMouseMove(ev.*.mouse_x, ev.*.mouse_y);
            },
            .MOUSE_DOWN => {
                handler.handleMouseButton(true);
            },
            .MOUSE_UP => {
                handler.handleMouseButton(false);
            },
            .KEY_DOWN => {
                handler.handleKeyDown(ev.*.key_code);
            },
            .RESIZED => {
                handler.handleResize(sapp.width(), sapp.height());
            },
            .FOCUSED => {
                handler.handleFocusChange(true);
            },
            .UNFOCUSED => {
                handler.handleFocusChange(false);
            },
            else => {},
        }
    }
}
