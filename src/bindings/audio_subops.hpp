#pragma once
#include "audio/mixer.hpp"
#include "bindings/arc_resource_finder.hpp"
#include "vm/vm.hpp"

namespace obgi {

struct SoundHost {
    Mixer* mixer = nullptr;
    ArcResourceFinder* resources = nullptr;
};

// Sound sub-opcodes of main opcode 0xA0 (analysis/音频与子opcode_A0.md): bank A = 16 streams, bank B = 128
// point sounds. Loading is synchronous here (the original may suspend the script thread until the file is
// read); volumes are linear 0..0x80. Ops not listed in the analysis are left unregistered.
void register_audio_subops(vm::SubopRegistry& registry, SoundHost& host);

} // namespace obgi
