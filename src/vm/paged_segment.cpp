#include "vm/vm.hpp"

#include <algorithm>
#include <cstring>
#include <new>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace obgi::vm {

void PagedSegment::Unmap::operator()(std::uint8_t* data) const noexcept {
#ifdef _WIN32
    VirtualFree(data, 0, MEM_RELEASE);
#else
    munmap(data, kSize);
#endif
}

bool PagedSegment::read(Word off, std::size_t n, std::uint8_t* out) const {
    // Subtraction also rejects SIZE_MAX lengths without overflowing.
    if (off > kSize || n > kSize - off) return false;
    if (!n) return true;
    if (data_) std::memcpy(out, data_.get() + off, n);
    else std::memset(out, 0, n);
    return true;
}

bool PagedSegment::write(Word off, std::size_t n, const std::uint8_t* in) {
    if (off > kSize || n > kSize - off) return false;
    if (!n) return true;
    if (!data_) {
#ifdef _WIN32
        void* data = VirtualAlloc(nullptr, kSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!data) throw std::bad_alloc();
#else
        void* data = mmap(nullptr, kSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (data == MAP_FAILED) throw std::bad_alloc();
#endif
        data_.reset(static_cast<std::uint8_t*>(data));
    }
    std::memcpy(data_.get() + off, in, n);
    return true;
}

void PagedSegment::zero(Word off, std::size_t n) {
    if (!data_ || off >= kSize || !n) return;
    n = std::min(n, kSize - off);
    if (off == 0 && n == kSize) {
        clear();
        return;
    }

    // Use the host's page size, not the VM's former 4 KiB software pages.
    // Only discard pages wholly inside the range: adjacent heap blocks may
    // occupy the partial pages at either end (notably with 16 KiB macOS pages).
    static const std::size_t page = [] {
#ifdef _WIN32
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        return static_cast<std::size_t>(info.dwPageSize);
#else
        const long size = sysconf(_SC_PAGESIZE);
        return size > 0 ? static_cast<std::size_t>(size) : kSize;
#endif
    }();
    const std::size_t end = off + n;
    const std::size_t first = (off + page - 1) / page * page;
    const std::size_t last = end / page * page;
    auto* const data = data_.get();
    if (first >= last) {
        std::memset(data + off, 0, n);
        return;
    }
    std::memset(data + off, 0, first - off);
    std::memset(data + last, 0, end - last);
#ifdef __linux__
    // Linux guarantees demand-zero after discarding private anonymous pages.
    if (madvise(data + first, last - first, MADV_DONTNEED) == 0) return;
#endif
    // Darwin's MADV_DONTNEED and Windows' MEM_RESET do not guarantee zeroes.
    // Zero first, then allow reclamation: retained and discarded pages both
    // read as zero. Failure of this optional hint does not affect correctness.
    std::memset(data + first, 0, last - first);
    // Small frees are common. Retain their zeroed pages for reuse rather than
    // pay a syscall and fault them back in on the next allocation. Each segment
    // remains bounded at 64 MiB; clear() always releases the entire mapping.
    if (last - first < 256 * 1024) return;
#ifdef _WIN32
    VirtualAlloc(data + first, last - first, MEM_RESET, PAGE_READWRITE);
#elif defined(MADV_FREE)
    madvise(data + first, last - first, MADV_FREE);
#endif
}

} // namespace obgi::vm
