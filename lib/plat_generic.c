// Copyright (c) 2014-2016, NetApp, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "eth.h"
#include "plat.h" // IWYU pragma: keep
#include "util.h"

struct ifaddrs;


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


/// On a generic platform, we don't know how to set the thread affinity.
///
void plat_setaffinity(void)
{
    warn(warn, "setting thread affinity not supported");
}
