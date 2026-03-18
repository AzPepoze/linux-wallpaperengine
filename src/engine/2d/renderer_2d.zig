const std = @import("std");
const sokol = @import("sokol");
const core = @import("core");
const effect = @import("effect.zig");
const render_object = @import("render_object.zig");

const sg = sokol.gfx;

pub const Renderer2D = struct {
    allocator: std.mem.Allocator,
    pipeline: sg.Pipeline,
    bindings: sg.Bindings,
    sampler: sg.Sampler,

    view_width: f64,
    view_height: f64,

    render_objects: std.ArrayListUnmanaged(render_object.RenderObject) = .{},

    pub fn init(allocator: std.mem.Allocator, width: f64, height: f64) Renderer2D {
        const vertices = [_]f32{
            -0.5, -0.5, 0, 1,
            0.5,  -0.5, 1, 1,
            0.5,  0.5,  1, 0,
            -0.5, 0.5,  0, 0,
        };
        const vbuf = sg.makeBuffer(.{
            .data = sg.asRange(&vertices),
        });

        const indices = [_]u16{ 0, 1, 2, 0, 2, 3 };
        const ibuf = sg.makeBuffer(.{
            .usage = .{ .index_buffer = true },
            .data = sg.asRange(&indices),
        });

        const sampler = sg.makeSampler(.{
            .min_filter = .LINEAR,
            .mag_filter = .LINEAR,
            .wrap_u = .REPEAT,
            .wrap_v = .REPEAT,
        });

        var d = sg.ShaderDesc{};
        d.vertex_func.source =
            \\#version 330
            \\layout(location=0) in vec2 position;
            \\layout(location=1) in vec2 texcoord0;
            \\out vec2 uv;
            \\void main() {
            \\  gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
            \\  uv = texcoord0;
            \\}
        ;
        d.fragment_func.source =
            \\#version 330
            \\precision mediump float;
            \\in vec2 uv;
            \\out vec4 frag_color;
            \\void main() {
            \\  frag_color = vec4(uv.x, uv.y, 0.5, 1.0);
            \\}
        ;
        const shader = sg.makeShader(d);

        var pip_desc = sg.PipelineDesc{
            .shader = shader,
            .index_type = .UINT16,
        };
        pip_desc.layout.attrs[0] = .{ .format = .FLOAT2 };
        pip_desc.layout.attrs[1] = .{ .format = .FLOAT2 };

        pip_desc.colors[0].blend = .{
            .enabled = true,
            .src_factor_rgb = .SRC_ALPHA,
            .dst_factor_rgb = .ONE_MINUS_SRC_ALPHA,
        };

        var bindings = sg.Bindings{};
        bindings.vertex_buffers[0] = vbuf;
        bindings.index_buffer = ibuf;
        bindings.samplers[0] = sampler;

        return .{
            .allocator = allocator,
            .pipeline = sg.makePipeline(pip_desc),
            .bindings = bindings,
            .sampler = sampler,
            .view_width = width,
            .view_height = height,
        };
    }

    pub fn deinit(self: *Renderer2D) void {
        for (self.render_objects.items) |*ro| {
            for (ro.effects) |*eff| {
                eff.deinit();
            }
            self.allocator.free(ro.effects);
        }
        self.render_objects.deinit(self.allocator);
        sg.destroyPipeline(self.pipeline);
        sg.destroyBuffer(self.bindings.vertex_buffers[0]);
        sg.destroyBuffer(self.bindings.index_buffer);
        sg.destroySampler(self.sampler);
    }

    pub fn updateViewport(self: *Renderer2D, width: f64, height: f64) void {
        self.view_width = width;
        self.view_height = height;
    }

    pub fn drawSprite(self: *Renderer2D, image: sg.Image, x: f32, y: f32, w: f32, h: f32, rotation: f32, tint: [4]f32, effects_slice: []const effect.Effect) void {
        if (effects_slice.len > 0) {
            for (effects_slice) |eff| {
                for (eff.passes) |*p| {
                    if (p.pipeline) |pip| {
                        sg.applyPipeline(pip);

                        var pass_bindings = self.bindings;
                        const texture_view_desc: sg.TextureViewDesc = .{ .image = image };
                        const view_desc: sg.ViewDesc = .{ .texture = texture_view_desc };
                        const image_view = sg.makeView(view_desc);
                        pass_bindings.views[0] = image_view;
                        sg.applyBindings(pass_bindings);

                        const mvp = self.calcMVP(x, y, w, h, rotation);
                        sg.applyUniforms(0, sg.asRange(&mvp));

                        if (p.uniform_data.len > 0) {
                            sg.applyUniforms(0, sg.asRange(p.uniform_data));
                        }

                        sg.draw(0, 6, 1);
                        sg.destroyView(image_view);
                        return;
                    }
                }
            }
        }

        sg.applyPipeline(self.pipeline);
        var fallback_bindings = self.bindings;
        const texture_view_desc: sg.TextureViewDesc = .{ .image = image };
        const view_desc: sg.ViewDesc = .{ .texture = texture_view_desc };
        const image_view = sg.makeView(view_desc);
        fallback_bindings.views[0] = image_view;
        sg.applyBindings(fallback_bindings);

        const mvp = self.calcMVP(x, y, w, h, rotation);
        sg.applyUniforms(0, sg.asRange(&mvp));
        sg.applyUniforms(1, sg.asRange(&tint));
        sg.draw(0, 6, 1);
        sg.destroyView(image_view);
    }

    pub fn calcMVP(self: *Renderer2D, x: f32, y: f32, w: f32, h: f32, rotation: f32) core.math.Mat4 {
        const ortho = core.math.ortho(0, @as(f32, @floatCast(self.view_width)), @as(f32, @floatCast(self.view_height)), 0, -1, 1);
        var model = core.math.translate(core.math.identity(), x, y, 0);
        model = core.math.rotate(model, rotation);
        model = core.math.scale(model, w, h, 1);
        return core.math.multiply(ortho, model);
    }
};
