const std = @import("std");

pub const Mat4 = [16]f32;

pub fn identity() Mat4 {
    return .{
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };
}

pub fn ortho(left: f32, right: f32, bottom: f32, top: f32, near: f32, far: f32) Mat4 {
    var m = std.mem.zeroes(Mat4);
    m[0] = 2.0 / (right - left);
    m[5] = 2.0 / (top - bottom);
    m[10] = -2.0 / (far - near);
    m[12] = -(right + left) / (right - left);
    m[13] = -(top + bottom) / (top - bottom);
    m[14] = -(far + near) / (far - near);
    m[15] = 1.0;
    return m;
}

pub fn translate(m: Mat4, x: f32, y: f32, z: f32) Mat4 {
    var res = m;
    res[12] = m[0] * x + m[4] * y + m[8] * z + m[12];
    res[13] = m[1] * x + m[5] * y + m[9] * z + m[13];
    res[14] = m[2] * x + m[6] * y + m[10] * z + m[14];
    res[15] = m[3] * x + m[7] * y + m[11] * z + m[15];
    return res;
}

pub fn scale(m: Mat4, x: f32, y: f32, z: f32) Mat4 {
    var res = m;
    res[0] *= x;
    res[1] *= x;
    res[2] *= x;
    res[3] *= x;
    res[4] *= y;
    res[5] *= y;
    res[6] *= y;
    res[7] *= y;
    res[8] *= z;
    res[9] *= z;
    res[10] *= z;
    res[11] *= z;
    return res;
}

pub fn rotate(m: Mat4, angle_radians: f32) Mat4 {
    const cos_a = @cos(angle_radians);
    const sin_a = @sin(angle_radians);

    var res = m;
    const m0 = m[0];
    const m1 = m[1];
    const m2 = m[2];
    const m3 = m[3];
    const m4 = m[4];
    const m5 = m[5];
    const m6 = m[6];
    const m7 = m[7];

    res[0] = m0 * cos_a + m4 * sin_a;
    res[1] = m1 * cos_a + m5 * sin_a;
    res[2] = m2 * cos_a + m6 * sin_a;
    res[3] = m3 * cos_a + m7 * sin_a;
    res[4] = m4 * cos_a - m0 * sin_a;
    res[5] = m5 * cos_a - m1 * sin_a;
    res[6] = m6 * cos_a - m2 * sin_a;
    res[7] = m7 * cos_a - m3 * sin_a;

    return res;
}

pub fn multiply(a: Mat4, b: Mat4) Mat4 {
    var res: Mat4 = undefined;
    for (0..4) |i| {
        for (0..4) |j| {
            res[i * 4 + j] = a[i * 4 + 0] * b[0 * 4 + j] +
                a[i * 4 + 1] * b[1 * 4 + j] +
                a[i * 4 + 2] * b[2 * 4 + j] +
                a[i * 4 + 3] * b[3 * 4 + j];
        }
    }
    return res;
}
