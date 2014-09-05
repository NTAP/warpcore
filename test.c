#include "warpcore.h"
#include "ip.h"


int main(void)
{
	struct warpcore *w = w_init("em1");

	// warpcore runs in its own thread spawned by w_init()
	D("main process ready");

	w_bind(w, IP_P_UDP, 53);
	// struct w_iovec *io = w_rx(w, s);

	// let it run for some time and then let's exit
	sleep(10);

	w_close(w, IP_P_UDP, 53);
	D("main process exiting");
	w_free(w);

	return 0;
}
