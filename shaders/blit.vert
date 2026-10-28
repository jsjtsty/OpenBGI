#version 450

// Textured quad from gl_VertexIndex (triangle strip, 4 vertices); no vertex buffer.
// rect = (x0, y0, x1, y1) in normalized device coordinates, y up; uv_rect = (u0, v0, u1, v1) for the
// same corners; tint.a is the layer alpha.
layout(std140, set = 1, binding = 0) uniform Params {
    vec4 rect;
    vec4 uv_rect;
    vec4 tint;
};

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_tint;

void main() {
    vec2 c = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1)); // 0/1 corner
    gl_Position = vec4(mix(rect.xy, rect.zw, c), 0.0, 1.0);
    v_uv = mix(uv_rect.xy, uv_rect.zw, c);
    v_tint = tint;
}
