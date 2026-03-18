// Type definitions
pub const types = @import("common.zig");
pub const utils = @import("utils.zig");

// Import all type definitions
pub const ScalingMode = @import("common.zig").ScalingMode;
pub const UniformType = @import("common.zig").UniformType;
pub const UniformInfo = @import("common.zig").UniformInfo;

pub const RenderObject = @import("render_object.zig").RenderObject;
pub const MaterialPass = @import("material_pass.zig").MaterialPass;
pub const Effect = @import("effect.zig").Effect;
pub const Renderer2D = @import("renderer_2d.zig").Renderer2D;
pub const RenderPass = @import("render_pass.zig").RenderPass;
pub const RenderGraph = @import("render_graph.zig").RenderGraph;
pub const RenderTarget = @import("render_target.zig").RenderTarget;
pub const RenderPipeline = @import("render_pipeline.zig").RenderPipeline;
pub const Renderer = @import("renderer.zig").Renderer;
