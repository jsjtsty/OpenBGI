#include <SDL3/SDL_main.h>

#include <cstdio>

#include "openbgi/openbgi.h"

extern "C" int SDL_main(int argc, char* argv[]) {
    char err[1024] = {};
    int rc = obgi_run(argc, argv, err, sizeof(err));
    if (rc != 0 && err[0] != '\0') {
        std::fprintf(stderr, "OpenBGI: %s\n", err);
    }
    return rc;
}
