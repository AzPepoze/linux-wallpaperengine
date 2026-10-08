#ifndef POINTER_STATE_H
#define POINTER_STATE_H

// Pointer uniforms normalized to the output with y down; refreshed once per frame.
struct ShaderPointerState {
    float x = 0.5f, y = 0.5f;
    float last_x = 0.5f, last_y = 0.5f;
    float pressed = 0.0f;  // 1 while the left button is down
    bool valid = false;    // true once a real pointer position has been seen
};
inline ShaderPointerState g_shader_pointer;

#endif  // POINTER_STATE_H
