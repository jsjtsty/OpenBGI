static const uint3 gl_WorkGroupSize = uint3(8u, 8u, 1u);

cbuffer Params : register(b0, space2)
{
    uint4 _186_info : packoffset(c0);
};

RWByteAddressBuffer _215 : register(u0, space1);
ByteAddressBuffer _220 : register(t0, space0);
ByteAddressBuffer _225 : register(t1, space0);

static uint3 gl_GlobalInvocationID;
struct SPIRV_Cross_Input
{
    uint3 gl_GlobalInvocationID : SV_DispatchThreadID;
};

int4 px_unpack(uint v)
{
    return int4(int(v & 255u), int((v >> uint(8)) & 255u), int((v >> uint(16)) & 255u), int(v >> uint(24)));
}

uint px_pack(int4 c)
{
    return ((uint(c.x & 255) | (uint(c.y & 255) << uint(8))) | (uint(c.z & 255) << uint(16))) | (uint(c.w & 255) << uint(24));
}

uint crossfade_pixel(uint av, uint bv, uint ratio, bool has_alpha)
{
    uint param = av;
    int4 a = px_unpack(param);
    uint param_1 = bv;
    int4 b = px_unpack(param_1);
    if (!has_alpha)
    {
        int4 param_2 = clamp(a + (((b - a) * int(ratio).xxxx) >> int4(8, 8, 8, 8)), int4(0, 0, 0, 0), int4(255, 255, 255, 255));
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
    int3 rgb = clamp(b.xyz + (((a.xyz - b.xyz) * w.xxx) >> int3(7, 7, 7)), int3(0, 0, 0), int3(255, 255, 255));
    int4 param_3 = int4(rgb, int((sum >> uint(8)) & 255u));
    return px_pack(param_3);
}

void main_inner()
{
    uint2 p = gl_GlobalInvocationID.xy;
    bool _190 = p.x >= _186_info.x;
    bool _199;
    if (!_190)
    {
        _199 = p.y >= _186_info.y;
    }
    else
    {
        _199 = _190;
    }
    if (_199)
    {
        return;
    }
    uint i = (p.y * _186_info.x) + p.x;
    uint param = _220.Load(i * 4 + 0);
    uint param_1 = _225.Load(i * 4 + 0);
    uint param_2 = _186_info.z;
    bool param_3 = _186_info.w != 0u;
    _215.Store(i * 4 + 0, crossfade_pixel(param, param_1, param_2, param_3));
}

[numthreads(8, 8, 1)]
void main(SPIRV_Cross_Input stage_input)
{
    gl_GlobalInvocationID = stage_input.gl_GlobalInvocationID;
    main_inner();
}
