#include "plat.h"
#include "util.h"


/// On a generic platform, we don't know how to set the thread affinity.
///
void plat_setaffinity(void)
{
    warn(warn, "not supported");
}
