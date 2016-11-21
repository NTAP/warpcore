#include <sys/cdefs.h>
// clang-format off
// because these includes need to be in-order
#include <sys/types.h>
#include <sys/cpuset.h>
// clang-format on

#include "plat.h"
#include "util.h"


/// Sets the affinity of the current thread to the highest existing CPU core.
///
void plat_setaffinity(void)
{
    int i;
    cpuset_t myset;
    if (cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID, -1, sizeof(cpuset_t),
                           &myset) == -1) {
        warn(crit, "cpuset_getaffinity failed");
        return;
    }

    // Find last available CPU
    for (i = CPU_SETSIZE - 1; i >= 0; i--)
        if (CPU_ISSET(i, &myset))
            break;
    assert(i != 0, "not allowed to run on any CPUs!?");

    // Set new CPU mask
    warn(info, "setting affinity to CPU %d", i);
    CPU_ZERO(&myset);
    CPU_SET(i, &myset);

    if (cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID, -1, sizeof(cpuset_t),
                           &myset) == -1)
        warn(crit, "cpuset_setaffinity failed");
}
