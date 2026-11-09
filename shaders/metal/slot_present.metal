#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct Params
{
    uint4 info;
};

struct Slot
{
    uint px[1];
};

constant uint3 gl_WorkGroupSize [[maybe_unused]] = uint3(8u, 8u, 1u);

kernel void slot_present(constant Params& _23 [[buffer(0)]], const device Slot& _47 [[buffer(1)]], texture2d<float, access::write> out_img [[texture(0)]], uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    uint2 p = gl_GlobalInvocationID.xy;
    bool _29 = p.x >= _23.info.x;
    bool _39;
    if (!_29)
    {
        _39 = p.y >= _23.info.y;
    }
    else
    {
        _39 = _29;
    }
    if (_39)
    {
        return;
    }
    uint v = _47.px[(p.y * _23.info.x) + p.x];
    float4 c;
    if (_23.info.z == 0u)
    {
        float3 rgb = float3(float((v >> uint(10)) & 31u), float((v >> uint(5)) & 31u), float(v & 31u)) / float3(31.0);
        c = float4(rgb, 1.0);
    }
    else
    {
        if (_23.info.z == 3u)
        {
            float g = float(v & 255u) / 255.0;
            c = float4(g, g, g, 1.0);
        }
        else
        {
            float _130;
            if (_23.info.z == 2u)
            {
                _130 = float(v >> uint(24));
            }
            else
            {
                _130 = 255.0;
            }
            c = float4(float((v >> uint(16)) & 255u), float((v >> uint(8)) & 255u), float(v & 255u), _130) / float4(255.0);
        }
    }
    out_img.write(c, uint2(int2(p)));
}
