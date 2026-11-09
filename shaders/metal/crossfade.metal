#pragma clang diagnostic ignored "-Wmissing-prototypes"

#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct Params
{
    uint4 info;
};

struct Out
{
    uint out_px[1];
};

struct First
{
    uint a_px[1];
};

struct Second
{
    uint b_px[1];
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

static inline __attribute__((always_inline))
uint crossfade_pixel(thread const uint& av, thread const uint& bv, thread const uint& ratio, thread const bool& has_alpha)
{
    uint param = av;
    int4 a = px_unpack(param);
    uint param_1 = bv;
    int4 b = px_unpack(param_1);
    if (!has_alpha)
    {
        int4 param_2 = clamp(a + (((b - a) * int4(int(ratio))) >> int4(8)), int4(0), int4(255));
        return px_pack(param_2);
    }
    uint wa = uint(a.w) * (256u - ratio);
    uint wb = uint(b.w) * ratio;
    uint sum = (wa + wb) & 65535u;
    if (sum == 0u)
    {
        return 0u;
    }
    int w = int((wa << uint(7)) / sum);
    int3 rgb = clamp(b.xyz + (((a.xyz - b.xyz) * int3(w)) >> int3(7)), int3(0), int3(255));
    int4 param_3 = int4(rgb, int((sum >> uint(8)) & 255u));
    return px_pack(param_3);
}

kernel void crossfade(constant Params& _186 [[buffer(0)]], device First& _220 [[buffer(1)]], device Second& _225 [[buffer(2)]], device Out& _215 [[buffer(3)]], uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    uint2 p = gl_GlobalInvocationID.xy;
    bool _190 = p.x >= _186.info.x;
    bool _199;
    if (!_190)
    {
        _199 = p.y >= _186.info.y;
    }
    else
    {
        _199 = _190;
    }
    if (_199)
    {
        return;
    }
    uint i = (p.y * _186.info.x) + p.x;
    uint param = _220.a_px[i];
    uint param_1 = _225.b_px[i];
    uint param_2 = _186.info.z;
    bool param_3 = _186.info.w != 0u;
    _215.out_px[i] = crossfade_pixel(param, param_1, param_2, param_3);
}
