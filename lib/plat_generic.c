#include "plat.h"
#include "util.h"

void plat_srandom(void)
{
    warn(warn, "not supported");
}


void plat_setaffinity(void)
{
    warn(warn, "not supported");
}

void plat_get_mac(uint8_t * mac __attribute__((unused)),
                  const struct ifaddrs * i __attribute__((unused)))
{
    warn(warn, "not supported");
}


uint16_t plat_get_mtu(const struct ifaddrs * i __attribute__((unused)))
{
    warn(warn, "not supported");
    return 0;
}


uint32_t plat_get_mbps(const struct ifaddrs * i __attribute__((unused)))
{
    warn(warn, "not supported");
    return 0;
}


bool plat_get_link(const struct ifaddrs * i __attribute__((unused)))
{
    warn(warn, "not supported");
    return true;
}
