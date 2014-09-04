#ifndef _debug_h_
#define _debug_h_

#define NETMAP_WITH_LIBS
#include <net/netmap_user.h>

// for now, we use the netmap-defined debug primitives
extern void hexdump(const void * const ptr, const unsigned len);

#endif
