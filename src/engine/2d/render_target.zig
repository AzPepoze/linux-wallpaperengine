const sokol = @import("sokol");

const sg = sokol.gfx;

pub const RenderTarget = struct {
    image: sg.Image,
    width: u32,
    height: u32,
    format: sg.PixelFormat,

    pub fn create(width: u32, height: u32, format: sg.PixelFormat) RenderTarget {
        const img_desc = sg.ImageDesc{
            .width = @intCast(width),
            .height = @intCast(height),
            .pixel_format = format,
            .usage = .RENDER_TARGET,
        };
        const image = sg.makeImage(&img_desc);
        return .{ .image = image, .width = width, .height = height, .format = format };
    }

    pub fn destroy(self: *RenderTarget) void {
        sg.destroyImage(self.image);
    }
};
