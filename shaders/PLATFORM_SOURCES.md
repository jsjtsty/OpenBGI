# Platform shader sources

`*.vert`, `*.frag`, and `*.comp` in this directory are the canonical Vulkan GLSL sources. The matching
HLSL and Metal sources in `dx12/` and `metal/` are generated snapshots archived in the repository. Normal
builds do not invoke SDL_shadercross or SPIRV-Cross.

## Regeneration provenance

The archived HLSL and MSL files were generated on macOS arm64 from the GLSL sources with:

- glslang 16.6.0 (`glslangValidator -V --target-env vulkan1.1`)
- SDL_shadercross 3.0.0, macOS universal artifact downloaded from the upstream GitHub Actions workflow
- MSL target version 2.3.0

The downloaded archive SHA-256 was `fb6d8f7cc4e3dff4a62803a58e91a3ca43c95e52ba4f755d0c2a41b41231384d`.
SDL_shadercross emitted `main0` for each MSL entry point. The archived sources rename that entry point and its
associated stage I/O structs to the shader's unique name, so all Metal shaders can share one metallib. HLSL
uses `main`.

When changing a canonical GLSL shader or `blend.glsl`, regenerate both matching platform source files and
review the resource bindings before committing. DXC and Apple's Metal compiler then compile those archived
sources during their respective native builds.

| GLSL source | HLSL source | Metal source |
| --- | --- | --- |
| `blend_cases.comp` | `dx12/blend_cases.hlsl` | `metal/blend_cases.metal` |
| `blit.vert` | `dx12/blit_vert.hlsl` | `metal/blit_vert.metal` |
| `blit.frag` | `dx12/blit_frag.hlsl` | `metal/blit_frag.metal` |
| `blit_rect.comp` | `dx12/blit_rect.hlsl` | `metal/blit_rect.metal` |
| `crossfade.comp` | `dx12/crossfade.hlsl` | `metal/crossfade.metal` |
| `screen_filter.comp` | `dx12/screen_filter.hlsl` | `metal/screen_filter.metal` |
| `slot_present.comp` | `dx12/slot_present.hlsl` | `metal/slot_present.metal` |
