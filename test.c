#include "warpcore.h"
#include "eth.h"


int main(void)
{
	struct warpcore *w = w_init("em1");

	// warpcore runs in its own thread spawned by w_init()
	// let it run for some time and then let's exit
	D("main process ready");
	sleep(10);

	D("main process exiting");
	w_free(w);

	return 0;
}
