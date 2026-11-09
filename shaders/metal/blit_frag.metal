#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct blit_frag_out
{
    float4 out_color [[color(0)]];
};

struct blit_frag_in
{
    float2 v_uv [[user(locn0)]];
    float4 v_tint [[user(locn1)]];
};

fragment blit_frag_out blit_frag(blit_frag_in in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler texSmplr [[sampler(0)]])
{
    blit_frag_out out = {};
    out.out_color = tex.sample(texSmplr, in.v_uv) * in.v_tint;
    return out;
}
