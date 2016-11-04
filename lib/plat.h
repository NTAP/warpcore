#ifndef _plat_h_
#define _plat_h_

#include <stdbool.h>
#include <stdint.h>

#ifdef __linux__
#define AF_LINK AF_PACKET
#define PLAT_MMFLAGS MAP_POPULATE | MAP_LOCKED
#else
#define PLAT_MMFLAGS MAP_PREFAULT_READ | MAP_NOSYNC | MAP_ALIGNED_SUPER
#endif

struct ifaddrs;

extern void plat_srandom(void);

extern void plat_setaffinity(void);

extern void plat_get_mac(uint8_t * mac, const struct ifaddrs * i);

extern uint16_t plat_get_mtu(const struct ifaddrs * i);

extern uint32_t plat_get_mbps(const struct ifaddrs * i);

extern bool plat_get_link(const struct ifaddrs * i);

#endif