// SPDX-License-Identifier: AGPL-3.0-or-later
#include "context.h"
#include "systemfonts.h"
#include <mutex>
namespace {
struct SharedLocks {
    std::mutex mutexes[FZ_LOCK_MAX];
    fz_locks_context api{this,
        [](void *user, int lock) { static_cast<SharedLocks *>(user)->mutexes[lock].lock(); },
        [](void *user, int lock) { static_cast<SharedLocks *>(user)->mutexes[lock].unlock(); }};
};
}
fz_context *MengsheePdfRuntime::createContext()
{
    // All runtime contexts, including independent preview workers, share locks.
    static SharedLocks locks;
    auto *context = fz_new_context(nullptr, &locks.api, FZ_STORE_DEFAULT);
    installSystemFonts(context);
    return context;
}
