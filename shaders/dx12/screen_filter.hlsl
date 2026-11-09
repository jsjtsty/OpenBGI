static const uint3 gl_WorkGroupSize = uint3(8u, 8u, 1u);

cbuffer Params : register(b0, space2)
{
    uint4 _84_info : packoffset(c0);
    int4 _84_effect : packoffset(c1);
};

RWByteAddressBuffer _115 : register(u0, space1);
ByteAddressBuffer _150 : register(t0, space0);

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

void main_inner()
{
    uint2 p = gl_GlobalInvocationID.xy;
    bool _89 = p.x >= _84_info.x;
    bool _98;
    if (!_89)
    {
        _98 = p.y >= _84_info.y;
    }
    else
    {
        _98 = _89;
    }
    if (_98)
    {
        return;
    }
    uint i = (p.y * _84_info.x) + p.x;
    uint param = _115.Load(i * 4 + 0);
    int4 src = px_unpack(param);
    uint param_1 = _84_info.z;
    int3 colour = px_unpack(param_1).xyz;
    int level = clamp(_84_effect.x, 0, 256);
    int inv = 256 - level;
    if (_84_info.w == 4u)
    {
        int v = ((int(_150.Load(i * 4 + 0) & 255u) << _84_effect.y) + 256) - (((1 << _84_effect.y) + 1) * level);
        if (v <= 0)
        {
            _115.Store(i * 4 + 0, _84_info.z);
        }
        else
        {
            if (v < 256)
            {
                int4 param_2 = int4(clamp(colour + (((src.xyz - colour) * (v >> 1).xxx) >> int3(7, 7, 7)), int3(0, 0, 0), int3(255, 255, 255)), 0);
                _115.Store(i * 4 + 0, px_pack(param_2));
            }
        }
        return;
    }
    if (level == 0)
    {
        return;
    }
    int alpha = 0;
    bool _213 = _84_info.w == 0u;
    bool _226;
    if (!_213)
    {
        bool _219 = _84_info.w == 1u;
        bool _225;
        if (_219)
        {
            _225 = _84_info.z == 0u;
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
        rgb = ((src.xyz * inv.xxx) >> int3(8, 8, 8)) + ((colour * level.xxx) >> int3(8, 8, 8));
    }
    else
    {
        if (_84_info.w == 1u)
        {
            rgb = src.xyz + ((colour * level.xxx) >> int3(8, 8, 8));
            alpha = src.w;
        }
        else
        {
            if (_84_info.w == 2u)
            {
                int luma = (((src.x * 28) + (src.y * 151)) + (src.z * 77)) >> 9;
                int v_1 = min(((luma + 1) * level), 32767);
                rgb = ((src.xyz * inv.xxx) >> int3(8, 8, 8)) + (((colour * v_1.xxx) >> int3(16, 16, 16)) << int3(1, 1, 1));
            }
            else
            {
                rgb = src.xyz + ((((src.xyz ^ colour) - src.xyz) * (level >> 1).xxx) >> int3(7, 7, 7));
                alpha = src.w;
            }
        }
    }
    int4 param_3 = int4(clamp(rgb, int3(0, 0, 0), int3(255, 255, 255)), alpha);
    _115.Store(i * 4 + 0, px_pack(param_3));
}

[numthreads(8, 8, 1)]
void main(SPIRV_Cross_Input stage_input)
{
    gl_GlobalInvocationID = stage_input.gl_GlobalInvocationID;
    main_inner();
}
