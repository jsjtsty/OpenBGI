#include <doctest/doctest.h>

#include <cstring>

#include "sha256.hpp"

TEST_CASE("sha256 known answers") {
    auto h = [](const char* s) {
        return obgi_test::sha256_hex({reinterpret_cast<const std::uint8_t*>(s), std::strlen(s)});
    };
    CHECK(h("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(h("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(h("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}
