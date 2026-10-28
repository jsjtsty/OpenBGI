#version 450

layout(set = 2, binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_tint;
layout(location = 0) out vec4 out_color;

void main() {
    out_color = texture(tex, v_uv) * v_tint;
}
