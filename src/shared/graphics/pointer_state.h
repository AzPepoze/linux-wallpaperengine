#ifndef POINTER_STATE_H
#define POINTER_STATE_H

// Pointer values for shaders (g_PointerPosition, g_PointerPositionLast, g_PointerState), normalized to the output
// surface with y down. The frame loop refreshes this once per frame.
struct ShaderPointerState {
    float x = 0.5f, y = 0.5f;
    float last_x = 0.5f, last_y = 0.5f;
    float pressed = 0.0f;  // 1 while the left button is down
};
inline ShaderPointerState g_shader_pointer;

#endif  // POINTER_STATE_H
