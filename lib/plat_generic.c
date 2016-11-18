#include "eth.h"
#include "plat.h"
#include "util.h"


/// Return the Ethernet MAC address of network interface @p i.
///
/// Not supported on generic platforms.
///
/// @param[out] mac   A buffer of at least ETH_ADDR_LEN bytes.
/// @param[in]  i     A network interface.
///
void plat_get_mac(uint8_t * mac,
                  const struct ifaddrs * i __attribute__((unused)))
{
    warn(warn, "MAC address queries not supported");
    memcpy(mac, "\xde\xad\xde\xad\xde\xad", ETH_ADDR_LEN);
}


/// Return the MTU of network interface @p i.
///
/// Not supported on generic platforms.
///
/// @param[in]  i     A network interface.
///
/// @return     The MTU of @p i.
///
uint16_t plat_get_mtu(const struct ifaddrs * i __attribute__((unused)))
{
    warn(warn, "MTU queries not supported");
    return 1500;
}


/// Return the link speed in Mb/s of network interface @p i.
///
/// Not supported on generic platforms.
///
/// @param[in]  i     A network interface.
///
/// @return     Link speed of interface @p i.
///
uint32_t plat_get_mbps(const struct ifaddrs * i __attribute__((unused)))
{
    warn(warn, "link speed queries not supported");
    return 0;
}


/// Return the link status of network interface @p i.
///
/// Not supported on generic platforms.
///
/// @param[in]  i     A network interface.
///
/// @return     Link status of interface @p i. True means link is up.
///
bool plat_get_link(const struct ifaddrs * i __attribute__((unused)))
{
    warn(warn, "link state queries not supported");
    return true;
}
