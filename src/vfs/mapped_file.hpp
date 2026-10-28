#pragma once
#include <filesystem>

#include "core/bytes.hpp"
#include "core/error.hpp"

namespace obgi {

// Read-only memory mapping of a whole file.
class MappedFile {
  public:
    static Result<MappedFile> open(const std::filesystem::path& path);

    MappedFile() = default;
    MappedFile(MappedFile&& other) noexcept { swap(other); }
    MappedFile& operator=(MappedFile&& other) noexcept {
        MappedFile tmp(std::move(other));
        swap(tmp);
        return *this;
    }
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    ~MappedFile();

    ByteView bytes() const { return {data_, size_}; }

  private:
    void swap(MappedFile& o) noexcept {
        std::swap(data_, o.data_);
        std::swap(size_, o.size_);
        std::swap(handle_, o.handle_);
    }

    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    void* handle_ = nullptr; // Windows: file-mapping handle; POSIX: unused
};

} // namespace obgi
