#version 450
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texcoord;
layout(location = 0) out vec2 uv;
layout(binding = 0) uniform params {
    mat4 mvp;
};
void main() {
    gl_Position = mvp * vec4(position, 1.0);
    uv = texcoord;
}
