#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct Params
{
    float4 rect;
    float4 uv_rect;
    float4 tint;
};

struct blit_vert_out
{
    float2 v_uv [[user(locn0)]];
    float4 v_tint [[user(locn1)]];
    float4 gl_Position [[position]];
};

vertex blit_vert_out blit_vert(constant Params& _32 [[buffer(0)]], uint gl_VertexIndex [[vertex_id]])
{
    blit_vert_out out = {};
    float2 c = float2(float(int(gl_VertexIndex) & 1), float((int(gl_VertexIndex) >> 1) & 1));
    out.gl_Position = float4(mix(_32.rect.xy, _32.rect.zw, c), 0.0, 1.0);
    out.v_uv = mix(_32.uv_rect.xy, _32.uv_rect.zw, c);
    out.v_tint = _32.tint;
    return out;
}
