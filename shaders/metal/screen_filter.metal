#pragma clang diagnostic ignored "-Wmissing-prototypes"

#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct Params
{
    uint4 info;
    int4 effect;
};

struct Screen
{
    uint pixels[1];
};

struct Gray
{
    uint gray_px[1];
};

constant uint3 gl_WorkGroupSize [[maybe_unused]] = uint3(8u, 8u, 1u);

static inline __attribute__((always_inline))
int4 px_unpack(thread const uint& v)
{
    return int4(int(v & 255u), int((v >> uint(8)) & 255u), int((v >> uint(16)) & 255u), int(v >> uint(24)));
}

static inline __attribute__((always_inline))
uint px_pack(thread const int4& c)
{
    return ((uint(c.x & 255) | (uint(c.y & 255) << uint(8))) | (uint(c.z & 255) << uint(16))) | (uint(c.w & 255) << uint(24));
}

kernel void screen_filter(constant Params& _84 [[buffer(0)]], device Gray& _150 [[buffer(1)]], device Screen& _115 [[buffer(2)]], uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    uint2 p = gl_GlobalInvocationID.xy;
    bool _89 = p.x >= _84.info.x;
    bool _98;
    if (!_89)
    {
        _98 = p.y >= _84.info.y;
    }
    else
    {
        _98 = _89;
    }
    if (_98)
    {
        return;
    }
    uint i = (p.y * _84.info.x) + p.x;
    uint param = _115.pixels[i];
    int4 src = px_unpack(param);
    uint param_1 = _84.info.z;
    int3 colour = px_unpack(param_1).xyz;
    int level0 = clamp(_84.effect.x, 0, 256);
    int inv = 256 - level0;
    if (_84.info.w == 4u)
    {
        int v = ((int(_150.gray_px[i] & 255u) << _84.effect.y) + 256) - (((1 << _84.effect.y) + 1) * level0);
        if (v <= 0)
        {
            _115.pixels[i] = _84.info.z;
        }
        else
        {
            if (v < 256)
            {
                int4 param_2 = int4(clamp(colour + (((src.xyz - colour) * int3(v >> 1)) >> int3(7)), int3(0), int3(255)), 0);
                _115.pixels[i] = px_pack(param_2);
            }
        }
        return;
    }
    if (level0 == 0)
    {
        return;
    }
    int alpha = 0;
    bool _213 = _84.info.w == 0u;
    bool _226;
    if (!_213)
    {
        bool _219 = _84.info.w == 1u;
        bool _225;
        if (_219)
        {
            _225 = _84.info.z == 0u;
        }
        else
        {
            _225 = _219;
        }
        _226 = _225;
    }
    else
    {
        _226 = _213;
    }
    int3 rgb;
    if (_226)
    {
        rgb = ((src.xyz * int3(inv)) >> int3(8)) + ((colour * int3(level0)) >> int3(8));
    }
    else
    {
        if (_84.info.w == 1u)
        {
            rgb = src.xyz + ((colour * int3(level0)) >> int3(8));
            alpha = src.w;
        }
        else
        {
            if (_84.info.w == 2u)
            {
                int luma = (((src.x * 28) + (src.y * 151)) + (src.z * 77)) >> 9;
                int v_1 = min(((luma + 1) * level0), 32767);
                rgb = ((src.xyz * int3(inv)) >> int3(8)) + (((colour * int3(v_1)) >> int3(16)) << int3(1));
            }
            else
            {
                rgb = src.xyz + ((((src.xyz ^ colour) - src.xyz) * int3(level0 >> 1)) >> int3(7));
                alpha = src.w;
            }
        }
    }
    int4 param_3 = int4(clamp(rgb, int3(0), int3(255)), alpha);
    _115.pixels[i] = px_pack(param_3);
}
