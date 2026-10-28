#include "vfs/mapped_file.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace obgi {

#ifdef _WIN32

Result<MappedFile> MappedFile::open(const std::filesystem::path& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return fail(Errc::Io, "cannot open " + path.string());
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size)) {
        CloseHandle(file);
        return fail(Errc::Io, "cannot stat " + path.string());
    }
    MappedFile m;
    if (size.QuadPart > 0) {
        HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!mapping) {
            CloseHandle(file);
            return fail(Errc::Io, "cannot map " + path.string());
        }
        void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
        if (!view) {
            CloseHandle(mapping);
            CloseHandle(file);
            return fail(Errc::Io, "cannot map view of " + path.string());
        }
        m.data_ = static_cast<const std::uint8_t*>(view);
        m.size_ = static_cast<std::size_t>(size.QuadPart);
        m.handle_ = mapping;
    }
    CloseHandle(file); // the mapping keeps the file alive
    return m;
}

MappedFile::~MappedFile() {
    if (data_) UnmapViewOfFile(data_);
    if (handle_) CloseHandle(handle_);
}

#else

Result<MappedFile> MappedFile::open(const std::filesystem::path& path) {
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return fail(Errc::Io, "cannot open " + path.string());
    struct stat st{};
    if (fstat(fd, &st) != 0) {
        ::close(fd);
        return fail(Errc::Io, "cannot stat " + path.string());
    }
    MappedFile m;
    if (st.st_size > 0) {
        void* view = mmap(nullptr, static_cast<std::size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
        if (view == MAP_FAILED) {
            ::close(fd);
            return fail(Errc::Io, "cannot map " + path.string());
        }
        m.data_ = static_cast<const std::uint8_t*>(view);
        m.size_ = static_cast<std::size_t>(st.st_size);
    }
    ::close(fd);
    return m;
}

MappedFile::~MappedFile() {
    if (data_) munmap(const_cast<std::uint8_t*>(data_), size_);
}

#endif

} // namespace obgi
