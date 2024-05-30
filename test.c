#include <stdio.h>
#include <time.h>
#include <pulse/pulseaudio.h>

pa_threaded_mainloop *mainloop;
pa_mainloop_api *api;
pa_context *context;

void get_volume_cb(pa_context *c, const pa_sink_info *info, int eol, void *userdata) {
    if (eol > 0) return;

    pa_volume_t volume = pa_cvolume_avg(&info->volume);
    printf("Volume: %0.2f%%\n", (volume / (float)PA_VOLUME_NORM) * 100);
}

void subscribe_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t idx, void *userdata) {
    pa_context_get_sink_info_by_name(c, "@DEFAULT_SINK@", get_volume_cb, NULL);
	printf("Subscribe callback\n");
}

void context_state_cb(pa_context *c, void *userdata) {
	switch (pa_context_get_state(c)) {
		case PA_CONTEXT_READY:
			printf("Context is ready!\n");
    		pa_context_get_sink_info_by_name(c, "@DEFAULT_SINK@", get_volume_cb, NULL);
			pa_context_set_subscribe_callback(context, subscribe_cb, NULL);
			pa_context_subscribe(context, PA_SUBSCRIPTION_MASK_SINK, NULL, NULL);
			break;
		case PA_CONTEXT_TERMINATED:
			printf("Context has been terminated.\n");
			break;
		case PA_CONTEXT_FAILED:
			printf("Context connection has failed...\n");
			break;
		default:
			printf("Other case\n");
			break;
	}
}

int main() {
	mainloop = pa_threaded_mainloop_new();
	api = pa_threaded_mainloop_get_api(mainloop);
	context = pa_context_new(api, "status-line");

	pa_context_set_state_callback(context, context_state_cb, NULL);
	pa_context_connect(context, NULL, PA_CONTEXT_NOFLAGS, NULL);


	pa_threaded_mainloop_start(mainloop);

	struct timespec ts = {.tv_sec = 10, .tv_nsec = 0};
	nanosleep(&ts, NULL);

	pa_threaded_mainloop_stop(mainloop);
	pa_context_disconnect(context);
	pa_context_unref(context);
	pa_threaded_mainloop_free(mainloop);

	return 0;
}
