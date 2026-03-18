#version 450
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 frag_color;
layout(binding = 0) uniform sampler2D tex;
void main() {
    frag_color = texture(tex, uv);
}
