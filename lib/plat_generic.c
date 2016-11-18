#include "eth.h"
#include "plat.h"
#include "util.h"


void plat_get_mac(uint8_t * mac,
                  const struct ifaddrs * i __attribute__((unused)))
{
    warn(warn, "not supported");
    memcpy(mac, "\xde\xad\xde\xad\xde\xad", ETH_ADDR_LEN);
}

uint16_t plat_get_mtu(const struct ifaddrs * i __attribute__((unused)))
{
    warn(warn, "not supported");
    return 1500;
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
