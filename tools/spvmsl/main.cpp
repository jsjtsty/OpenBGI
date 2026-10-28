// Build-time tool: obgi_spvmsl <in.spv> <out.metal>
// SPIR-V -> MSL with the resource slots SDL_GPU's Metal backend expects. SPIRV-Cross numbers buffers by its own
// order, so they are pinned here to SDL's layout (SDL_gpu_metal.m): uniform buffers [[buffer(0..U-1)]], then
// read-only storage buffers, then read-write storage buffers. In the SPIR-V the sets follow SDL's Vulkan rules:
// compute set 0 = read-only storage, set 1 = read-write storage, set 2 = uniform buffers; binding = slot in the set.
#include <spirv_msl.hpp>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: obgi_spvmsl <in.spv> <out.metal>\n");
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.empty() || bytes.size() % 4) {
        std::fprintf(stderr, "cannot read SPIR-V from %s\n", argv[1]);
        return 1;
    }
    std::vector<uint32_t> words(bytes.size() / 4);
    std::memcpy(words.data(), bytes.data(), bytes.size());

    try {
        spirv_cross::CompilerMSL msl(std::move(words));
        auto opts = msl.get_msl_options();
        opts.set_msl_version(2, 3);
        opts.platform = spirv_cross::CompilerMSL::Options::macOS;
        msl.set_msl_options(opts);

        auto res = msl.get_shader_resources();
        if (msl.get_execution_model() != spv::ExecutionModelGLCompute) {
            std::fprintf(stderr, "only compute shaders are supported so far (add the stage's slot rules here)\n");
            return 1;
        }
        if (!res.sampled_images.empty() || !res.storage_images.empty() || !res.separate_images.empty() ||
            !res.separate_samplers.empty()) {
            std::fprintf(stderr, "textures/samplers in compute shaders are not mapped yet\n");
            return 1;
        }

        const auto uniforms = static_cast<uint32_t>(res.uniform_buffers.size());
        uint32_t readonly = 0;
        for (auto& b : res.storage_buffers)
            if (msl.get_decoration(b.id, spv::DecorationNonWritable)) ++readonly;

        auto bind = [&](const spirv_cross::Resource& r, uint32_t msl_buffer) {
            spirv_cross::MSLResourceBinding rb{};
            rb.stage = spv::ExecutionModelGLCompute;
            rb.desc_set = msl.get_decoration(r.id, spv::DecorationDescriptorSet);
            rb.binding = msl.get_decoration(r.id, spv::DecorationBinding);
            rb.msl_buffer = msl_buffer;
            msl.add_msl_resource_binding(rb);
        };
        for (auto& b : res.uniform_buffers) bind(b, msl.get_decoration(b.id, spv::DecorationBinding));
        for (auto& b : res.storage_buffers) {
            uint32_t slot = msl.get_decoration(b.id, spv::DecorationBinding);
            bind(b, msl.get_decoration(b.id, spv::DecorationNonWritable) ? uniforms + slot : uniforms + readonly + slot);
        }

        std::string source = msl.compile();
        std::ofstream(argv[2], std::ios::binary) << source;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "SPIRV-Cross: %s\n", e.what());
        return 1;
    }
    return 0;
}
