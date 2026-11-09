// Build in Release mode; compare this same harness against each revision.
#include "vm/vm.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

using obgi::vm::PagedSegment;
using obgi::vm::Word;

namespace {
constexpr Word size = Word{1} << obgi::vm::kTagShift;
using Clock = std::chrono::steady_clock;

void check(bool ok) {
    if (!ok) std::abort();
}

template <class F> void bench(const char* name, F run) {
    // Include cold faults/allocations in each sample, report the best of five
    // to reduce scheduler noise. Checksums keep reads observable without LTO.
    double best = 1e30;
    std::uint64_t checksum = 0;
    for (int trial = 0; trial < 5; ++trial) {
        const auto start = Clock::now();
        checksum = run();
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        if (ms < best) best = ms;
    }
    std::printf("%-24s %9.3f ms  checksum=%llu\n", name, best, static_cast<unsigned long long>(checksum));
}
} // namespace

int main() {
    for (const bool sparse : {false, true}) {
        bench(sparse ? "4-byte sparse R/W" : "4-byte hot R/W", [=] {
            PagedSegment segment;
            std::uint64_t sum = 0;
            Word state = 12345;
            for (unsigned i = 0; i < 2000000; ++i) {
                state = state * 1664525u + 1013904223u;
                const Word off = state & (sparse ? (size - 4) : 0x3ffcu);
                Word back = 0;
                check(segment.write(off, sizeof state, reinterpret_cast<const std::uint8_t*>(&state)));
                check(segment.read(off, sizeof back, reinterpret_cast<std::uint8_t*>(&back)));
                sum += back;
            }
            return sum;
        });
    }
    bench("64-KiB bulk R/W", [] {
        PagedSegment segment;
        std::vector<std::uint8_t> data(65536, 0xa5), back(data.size());
        std::uint64_t sum = 0;
        for (unsigned i = 0; i < 4096; ++i) {
            const Word off = (i * 65536u) % size;
            check(segment.write(off, data.size(), data.data()));
            check(segment.read(off, back.size(), back.data()));
            sum += back[i % back.size()];
        }
        return sum;
    });
    bench("64-KiB write/zero/reuse", [] {
        PagedSegment segment;
        std::vector<std::uint8_t> data(65536, 0xa5);
        std::uint64_t sum = 0;
        for (unsigned i = 0; i < 4096; ++i) {
            const Word off = (i * 65536u) % size;
            check(segment.write(off, data.size(), data.data()));
            segment.zero(off, data.size());
            Word back = 1;
            check(segment.read(off + 32768, sizeof back, reinterpret_cast<std::uint8_t*>(&back)));
            sum += back;
        }
        return sum;
    });
}
