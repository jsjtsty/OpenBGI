#include <doctest/doctest.h>

#include "openbgi/openbgi.h"

#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;

struct TempDir {
    fs::path path;
    explicit TempDir(const char* name) : path(fs::temp_directory_path() / name) {
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};
} // namespace

TEST_CASE("C API version, detect, and probe handle missing or empty folders") {
    CHECK(std::strlen(obgi_version()) > 0);

    obgi_game_info info{};
    CHECK(obgi_detect("/nonexistent/openbgi/path/12345", &info) == 0);

    obgi_probe_result probe{};
    CHECK(obgi_probe("/nonexistent/openbgi/path/12345", &probe) == 0);
    CHECK(probe.ok == 0);
    CHECK(std::strlen(probe.message) > 0);

    TempDir empty("openbgi_test_api_empty");
    CHECK(obgi_detect(empty.path.string().c_str(), &info) == 0);
    CHECK(obgi_probe(empty.path.string().c_str(), &probe) == 0);
    CHECK(probe.ok == 0);
    CHECK(probe.complete == 0);
    CHECK(probe.arcs_found == 0);
    CHECK(std::string(probe.missing).find("autoload.arc") != std::string::npos);
    CHECK(std::string(probe.missing).find("system.arc") != std::string::npos);
}

#ifdef OPENBGI_HAS_APP_API
TEST_CASE("C API obgi_run reports startup errors without crashing") {
    char err[256] = {};
    char arg0[] = "obgi_test";
    char arg1[] = "--launcher";
    char arg2[] = "/nonexistent/openbgi/path/12345";
    char* argv[] = {arg0, arg1, arg2};
    CHECK(obgi_run(3, argv, err, sizeof(err)) != 0);
    CHECK(std::strlen(err) > 0);

    std::memset(err, 0, sizeof(err));
    char bad[] = "--no-such-flag";
    char* argv_bad[] = {arg0, bad};
    CHECK(obgi_run(2, argv_bad, err, sizeof(err)) != 0);
    CHECK(std::string(err).find("unknown") != std::string::npos);
}
#endif

TEST_SUITE("game") {
#ifdef OPENBGI_HAS_APP_API
TEST_CASE("C API probe and 3x in-process re-entrancy on configured game") {
    const std::string game_dir = OPENBGI_GAME_DIR;
    REQUIRE(!game_dir.empty());

    obgi_game_info info{};
    CHECK(obgi_detect(game_dir.c_str(), &info) == 1);
    CHECK(std::strlen(info.name) > 0);

    obgi_probe_result probe{};
    CHECK(obgi_probe(game_dir.c_str(), &probe) == 1);
    CHECK(probe.ok == 1);
    CHECK(probe.arcs_found >= 2);
    CHECK(std::strlen(probe.game_name) > 0);

    TempDir data_dir("openbgi_test_api_reentry");
    std::string data_str = data_dir.path.string();
    std::vector<std::string> args = {
        "obgi_test",
        game_dir,
        "--frames", "30",
        "--render-headless",
        "--data-dir", data_str
    };
    std::vector<char*> argv;
    for (auto& s : args) argv.push_back(s.data());

    for (int i = 0; i < 3; ++i) {
        char err[256] = {};
        const int rc = obgi_run(static_cast<int>(argv.size()), argv.data(), err, sizeof(err));
        INFO("run ", i, " error: ", err);
        CHECK(rc == 0);
        CHECK(std::strlen(err) == 0);
    }
}
#endif
}

TEST_CASE("probe accepts legacy system entry and identifiers with underscores") {
    TempDir dir("openbgi_test_api_packfile");
    std::string code("debug.inf\0AiyokunoEustia_Product\0", 34);
    std::string block(8, '\0');
    block[0] = 8;
    block[4] = static_cast<char>(code.size());
    block += code;
    std::string archive(48, '\0');
    std::memcpy(archive.data(), "PackFile    ", 12);
    archive[12] = 1;
    std::memcpy(archive.data() + 16, "ipl._bp", 7);
    archive[36] = static_cast<char>(block.size());
    { std::ofstream f(dir.path / "system.arc", std::ios::binary); f << archive << block; }
    obgi_probe_result probe{};
    REQUIRE(obgi_probe(dir.path.string().c_str(), &probe) == 1);
    CHECK(probe.complete == 1);
    CHECK(std::string(probe.game_name) == "AiyokunoEustia_Product");
    CHECK(std::string(probe.game_id) == "aiyokunoeustia_product");
    obgi_game_info info{};
    REQUIRE(obgi_detect(dir.path.string().c_str(), &info) == 1);
    CHECK(std::string(info.name) == "AiyokunoEustia_Product");
    { std::ofstream f(dir.path / "autoload.arc", std::ios::binary); f << "invalid"; }
    CHECK(obgi_probe(dir.path.string().c_str(), &probe) == 0);
    CHECK(std::string(probe.missing).find("autoload.arc") != std::string::npos);
}

TEST_CASE("probe checks a legacy game directory when configured") {
    const char* path = std::getenv("OPENBGI_LEGACY_GAME_DIR");
    if (!path) return;
    obgi_probe_result probe{};
    REQUIRE_MESSAGE(obgi_probe(path, &probe) == 1, probe.message);
    CHECK_MESSAGE(probe.complete == 1, probe.missing);
    CHECK(std::string(probe.game_name) == "AiyokunoEustia_Product");
}
