#ifndef FAST_COUCH_DIAGNOSTICS_H
#define FAST_COUCH_DIAGNOSTICS_H

#include <stdlib.h>

static const char* couch_diagnostics_path(void) {
    const char* path = getenv("COUCH_DIAGNOSTICS_PATH");
    return path != NULL && path[0] != '\0' ? path : "logs/couch_diagnostics.txt";
}

#endif
