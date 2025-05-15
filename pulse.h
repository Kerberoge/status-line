#include <pulse/pulseaudio.h>

#include "element.h"

struct pulse_data {
	pthread_t thread;
	int readfd, writefd;
	struct element *ctx;
	pa_mainloop *mainloop;
	pa_mainloop_api *mainloop_api;
	pa_context *context;
	int failed;
};

struct volume_data {
	float volume;
	int mute;
};

int pulse_setup(struct element *ctx, struct pulse_data *pdata);
void pulse_quit(struct pulse_data *pdata);
void pulse_handle(struct pulse_data *pdata);
