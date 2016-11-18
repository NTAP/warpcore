#include <sched.h>

#include "plat.h"
#include "util.h"


/// Sets the affinity of the current thread to the highest existing CPU core.
///
void plat_setaffinity(void)
{
    int i;
    cpu_set_t myset;
    assert(sched_getaffinity(0, sizeof(cpu_set_t), &myset) != -1,
           "sched_getaffinity");

    // Find last available CPU
    for (i = CPU_SETSIZE - 1; i >= -1; i--)
        if (CPU_ISSET(i, &myset))
            break;
    assert(i != -1, "not allowed to run on any CPUs!?");

    // Set new CPU mask
    warn(info, "setting affinity to CPU %d", i);
    CPU_ZERO(&myset);
    CPU_SET(i, &myset);

    assert(sched_setaffinity(0, sizeof(myset), &myset) != -1,
           "sched_setaffinity");
}
