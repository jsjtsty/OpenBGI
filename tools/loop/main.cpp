// obgi_loop: desktop re-entrancy verification tool for libobgi.
// Runs obgi_run() N times in the same process to verify clean SDL/GPU/audio/VM teardown.
#include "openbgi/openbgi.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef __APPLE__
#include <mach/mach.h>
#endif

namespace {

double rss_mb() {
#ifdef __APPLE__
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
        return static_cast<double>(info.resident_size) / (1024.0 * 1024.0);
    }
#endif
    return -1.0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: obgi_loop <game_dir> [--count N] [obgi_run options...]\n");
        return 2;
    }

    int count = 3;
    std::vector<std::string> args_storage;
    args_storage.emplace_back(argv[0]);
    args_storage.emplace_back(argv[1]);

    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--count" && i + 1 < argc) {
            count = std::atoi(argv[++i]);
            if (count < 1) count = 1;
        } else {
            args_storage.push_back(std::move(a));
        }
    }

    std::printf("OpenBGI version: %s\n", obgi_version());

    obgi_probe_result probe{};
    obgi_probe(argv[1], &probe);
    std::printf("probe: ok=%d complete=%d arcs=%d patch=%d id=%s name=%s missing=[%s] msg=[%s]\n",
                probe.ok, probe.complete, probe.arcs_found, probe.has_sc_patch,
                probe.game_id, probe.game_name, probe.missing, probe.message);
    if (!probe.ok) return 1;

    std::vector<char*> c_args;
    c_args.reserve(args_storage.size());
    for (auto& s : args_storage) c_args.push_back(s.data());

    std::printf("initial RSS: %.1f MB\n", rss_mb());
    for (int run = 1; run <= count; ++run) {
        char err[512] = {};
        const int rc = obgi_run(static_cast<int>(c_args.size()), c_args.data(), err, sizeof(err));
        std::printf("run %d/%d -> rc=%d RSS=%.1f MB%s%s\n",
                    run, count, rc, rss_mb(),
                    err[0] ? " err=" : "", err[0] ? err : "");
        if (rc != 0) return rc;
    }
    return 0;
}
