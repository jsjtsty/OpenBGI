#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include "bindings/arc_program_provider.hpp"

using namespace obgi;

namespace {
struct Directory {
    std::filesystem::path path;
    Directory() {
        static std::atomic<unsigned> serial{};
        path = std::filesystem::temp_directory_path() /
            ("obgi_resources_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" + std::to_string(serial++));
        std::filesystem::create_directory(path);
    }
    ~Directory() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};
void write(const std::filesystem::path& path, const std::string& data) {
    std::ofstream file(path, std::ios::binary);
    file.write(data.data(), static_cast<std::streamsize>(data.size()));
    REQUIRE(file.good());
}
void archive(const std::filesystem::path& path, const std::string& name, const std::string& data) {
    std::string file(16 + 128, '\0');
    std::memcpy(file.data(), "BURIKO ARC20", 12);
    file[12] = 1;
    std::memcpy(file.data() + 16, name.data(), name.size());
    const auto size = static_cast<std::uint32_t>(data.size());
    for (int i = 0; i < 4; ++i) file[16 + 0x64 + i] = static_cast<char>(size >> (i * 8));
    write(path, file + data);
}
std::string text(const Result<std::vector<std::uint8_t>>& bytes) {
    REQUIRE(bytes);
    return std::string(bytes->begin(), bytes->end());
}
} // namespace

TEST_CASE("resource search paths consistently override archives for media") {
    Directory dir;
    archive(dir.path / "data.arc", "same", "archive");
    write(dir.path / "same", "loose");
    std::filesystem::create_directory(dir.path / "override");
    write(dir.path / "override/same", "override");
    ArcResourceFinder resources(dir.path);
    CHECK(text(resources.read("data", "same")) == "loose");
    resources.add_search_path("override");
    CHECK(text(resources.read("data", "same")) == "override");
    CHECK(resources.exists("data", "same"));
    // Executable modules deliberately use exact archive lookup through the same store.
    ArcProgramProvider programs(resources);
    CHECK(text(programs.load("data.arc", "same")) == "archive");
    CHECK_FALSE(programs.load("missing.arc", "same"));
}

TEST_CASE("corrupt selected archives are reported instead of silently selecting another copy") {
    Directory dir;
    archive(dir.path / "other.arc", "same", "wrong copy");
    write(dir.path / "broken.arc", "bad");
    ArcResourceFinder resources(dir.path);
    auto result = resources.read("broken", "same");
    REQUIRE_FALSE(result);
    CHECK(result.error().code == Errc::Format);
}
