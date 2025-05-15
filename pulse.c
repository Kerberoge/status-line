#include <unistd.h>
#include <pthread.h>
#include <pulse/pulseaudio.h>

#include "pulse.h"
#include "element.h"
#include "modules.h"

extern int stop_program;

void sink_info_cb(pa_context *c, const pa_sink_info *info, int eol, void *data) {
	struct pulse_data *pdata = data;
	struct volume_data vdata;
	int response = 1;

	if (eol > 0 || !info) return;

	vdata.volume = (float) pa_cvolume_avg(&info->volume) / PA_VOLUME_NORM * 100;
	vdata.mute = info->mute;
	pdata->ctx->data = &vdata;

	volume(pdata->ctx);
	pdata->ctx->data = NULL;

	write(pdata->writefd, &response, sizeof(response));
}

void subscribe_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t i, void *data) {
	pa_context_get_sink_info_by_name(c, "@DEFAULT_SINK@", sink_info_cb, data);
}

void context_state_cb(pa_context *c, void *data) {
	struct pulse_data *pdata = data;

	switch (pa_context_get_state(c)) {
		case PA_CONTEXT_READY:
			// correct volume should be displayed even when no event is received
			pa_context_get_sink_info_by_name(c, "@DEFAULT_SINK@", sink_info_cb, pdata);

			pa_context_set_subscribe_callback(pdata->context, subscribe_cb, pdata);
			pa_context_subscribe(pdata->context, PA_SUBSCRIPTION_MASK_SINK, NULL, NULL);
			break;
		case PA_CONTEXT_FAILED:
			pdata->failed = 1;
			break;
	}
}

void create_pulse_context(struct pulse_data *pdata) {
	pdata->context = pa_context_new(pdata->mainloop_api, "status-line");
	pa_context_set_state_callback(pdata->context, context_state_cb, pdata);
	pa_context_connect(pdata->context, NULL, PA_CONTEXT_NOFLAGS, NULL);
}

void *pulse_worker(void *data) {
	struct pulse_data *pdata = data;
	struct timespec retry_interval = { .tv_sec = 0, .tv_nsec = 2e8 };

	pdata->mainloop = pa_mainloop_new();
	pdata->mainloop_api = pa_mainloop_get_api(pdata->mainloop);

	nanosleep(&retry_interval, NULL);
	create_pulse_context(pdata);
	while (pdata->failed && !stop_program) {
		pdata->failed = 0;
		pa_context_unref(pdata->context);
		nanosleep(&retry_interval, NULL);
		create_pulse_context(pdata);
	}

	pa_mainloop_run(pdata->mainloop, NULL);

	pa_context_disconnect(pdata->context);
	pa_context_unref(pdata->context);
	pa_mainloop_free(pdata->mainloop);

	return NULL;
}

int pulse_setup(struct element *ctx, struct pulse_data *pdata) {
	int dummy_pipe[2];

	pipe(dummy_pipe);
	pdata->readfd = dummy_pipe[0];
	pdata->writefd = dummy_pipe[1];
	pdata->ctx = ctx;
	pthread_create(&pdata->thread, NULL, pulse_worker, pdata);

	return pdata->readfd;
}

void pulse_quit(struct pulse_data *pdata) {
	pa_mainloop_quit(pdata->mainloop, 0);
	pthread_join(pdata->thread, NULL);
	close(pdata->readfd);
	close(pdata->writefd);
}

void pulse_handle(struct pulse_data *pdata) {
	int ret;

	read(pdata->readfd, &ret, sizeof(ret));
}
