#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>

#include "util.h"
#include "eth.h"
#include "plat.h"


/// Return the Ethernet MAC address of network interface @p i.
///
/// @param[out] mac   A buffer of at least ETH_ADDR_LEN bytes.
/// @param[in]  i     A network interface.
///
void plat_get_mac(uint8_t * mac, const struct ifaddrs * i)
{
    memcpy(mac, LLADDR((struct sockaddr_dl *)(void *)i->ifa_addr),
           ETH_ADDR_LEN);
}


/// Return the MTU of network interface @p i.
///
/// @param[in]  i     A network interface.
///
/// @return     The MTU of @p i.
///
uint16_t plat_get_mtu(const struct ifaddrs * i)
{
    return (uint16_t)((struct if_data *)(i->ifa_data))->ifi_mtu;
}


/// Return the link speed in Mb/s of network interface @p i.
///
/// @param[in]  i     A network interface.
///
/// @return     Link speed of interface @p i.
///
uint32_t plat_get_mbps(const struct ifaddrs * i)
{
    return (uint32_t)(((struct if_data *)(i->ifa_data))->ifi_baudrate /
                      1000000);
}


/// Return the link status of network interface @p i.
///
/// @param[in]  i     A network interface.
///
/// @return     Link status of interface @p i. True means link is up.
///
bool plat_get_link(const struct ifaddrs * i)
{
    return (((uint8_t)((struct if_data *)(i->ifa_data))->ifi_link_state) ==
            LINK_STATE_UP);
}
