const std = @import("std");

pub const ScalingMode = enum {
    fit,
    fill,
    stretch,
};

pub const UniformType = enum {
    float,
    vec2,
    vec3,
    vec4,
    mat4,
    sampler2D,
};

pub const UniformInfo = struct {
    name: []const u8,
    type: UniformType,
    offset: usize,
    size: usize,
};
