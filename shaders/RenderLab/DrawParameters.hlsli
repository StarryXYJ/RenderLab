#ifndef RENDERLAB_DRAW_PARAMETERS_HLSLI
#define RENDERLAB_DRAW_PARAMETERS_HLSLI

// DXIL SV_InstanceID excludes StartInstanceLocation. DXC's default SPIR-V InstanceIndex
// includes it. Keep this adaptation here; callers consume one explicit object-table index.
struct DrawParameters
{
    uint instance : SV_InstanceID;
#ifndef __spirv__
    uint firstInstance : SV_StartInstanceLocation;
#endif
};
uint GetInstanceIndex(DrawParameters draw)
{
#ifdef __spirv__
    return draw.instance;
#else
    return draw.instance + draw.firstInstance;
#endif
}

#endif
