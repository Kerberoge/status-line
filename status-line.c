#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>				// for nanosleep()
#include <unistd.h>				// for getpid()
#include <pulse/pulseaudio.h>	// next 2 are for the volume module
#include <pthread.h>
#include <net/if.h>				// next 4 are for the wifi module
#include <linux/nl80211.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>

#define MB	1048576
#define GB	1073741824

#define PREFIX(str, prefix)		!strncmp(str, prefix, strlen(prefix))

struct element {
	void (*func)();
	char *fmt1, *fmt2, *fmt3;
	char *buf;
	int dontcall;
};

struct pa_connection {
	pa_mainloop *mainloop;
	pa_mainloop_api *mainloop_api;
	pa_context *context;
	pthread_t thread;
	int failed;
};

struct cpu_usage {
	unsigned int user, nice, system, idle, total;
};

void setup_pulse(void);
void cleanup_pulse(void);
void create_pulse_context(void);
void *pulse_worker(void *data);
void context_state_cb(pa_context *c, void *data);
void subscribe_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t i, void *data);
void volume(pa_context *c, const pa_sink_info *info, int eol, void *data);

void sleep_state(struct element *ctx);
void kb_layout(struct element *ctx);
void memory(struct element *ctx);
void cpu(struct element *ctx);
void temperature(struct element *ctx);
void power(struct element *ctx);
void battery(struct element *ctx);
int wifi_cb(struct nl_msg *msg, void *data);
void wifi(struct element *ctx);
void date(struct element *ctx);

void print_status(void);
void quit(int signal);

#include "config.h"

size_t nr_elems = sizeof(elements) / sizeof(struct element);
struct element *volume_ctx;
int stop_program = 0;
struct pa_connection pa_con;
struct cpu_usage prev = {0, 0, 0, 0, 0};

void setup_pulse(void) {
	pthread_create(&pa_con.thread, NULL, pulse_worker, NULL);
}

void cleanup_pulse(void) {
	pa_mainloop_quit(pa_con.mainloop, 0);
	pthread_join(pa_con.thread, NULL);
}

void create_pulse_context(void) {
	pa_con.context = pa_context_new(pa_con.mainloop_api, "status-line");
	pa_context_set_state_callback(pa_con.context, context_state_cb, NULL);
	pa_context_connect(pa_con.context, NULL, PA_CONTEXT_NOFLAGS, NULL);
}

void *pulse_worker(void *data) {
	struct timespec retry_interval = {.tv_sec = 0, .tv_nsec = 200000000};

	pa_con.mainloop = pa_mainloop_new();
	pa_con.mainloop_api = pa_mainloop_get_api(pa_con.mainloop);

	nanosleep(&retry_interval, NULL);
	create_pulse_context();
	while (pa_con.failed && !stop_program) {
		pa_con.failed = 0;
		pa_context_unref(pa_con.context);
		nanosleep(&retry_interval, NULL);
		create_pulse_context();
	}

	pa_mainloop_run(pa_con.mainloop, NULL);

	pa_context_disconnect(pa_con.context);
	pa_context_unref(pa_con.context);
	pa_mainloop_free(pa_con.mainloop);

	return NULL;
}

void context_state_cb(pa_context *c, void *data) {
	switch (pa_context_get_state(c)) {
		case PA_CONTEXT_READY:
			// correct volume should be displayed even when no event is received
			pa_context_get_sink_info_by_name(c, "@DEFAULT_SINK@", volume, NULL);

			pa_context_set_subscribe_callback(pa_con.context, subscribe_cb, NULL);
			pa_context_subscribe(pa_con.context, PA_SUBSCRIPTION_MASK_SINK, NULL, NULL);
			break;
		case PA_CONTEXT_FAILED:
			pa_con.failed = 1;
			break;
	}
}

void subscribe_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t i, void *data) {
	pa_context_get_sink_info_by_name(c, "@DEFAULT_SINK@", volume, NULL);
}

void volume(pa_context *c, const pa_sink_info *info, int eol, void *data) {
	if (eol > 0 || !info) return;

	if (info->mute)
		sprintf(volume_ctx->buf, volume_ctx->fmt2);
	else
		sprintf(volume_ctx->buf, volume_ctx->fmt1,
				(float) pa_cvolume_avg(&info->volume) / PA_VOLUME_NORM * 100);

	// force a refresh
	kill(getpid(), SIGUSR1);
}

void sleep_state(struct element *ctx) {
	FILE *inhibit_sleep_f = fopen("/tmp/inhibit_sleep", "r");

	if (inhibit_sleep_f) {
		fclose(inhibit_sleep_f);
		sprintf(ctx->buf, ctx->fmt2);
	} else {
		sprintf(ctx->buf, ctx->fmt1);
	}
}

void kb_layout(struct element *ctx) {
	FILE *kb_layout_f = fopen("/tmp/dwl_kblayout", "r");
	char line[100];

	if (!kb_layout_f) return;

	fscanf(kb_layout_f, "%s", line);
	fclose(kb_layout_f);
	sprintf(ctx->buf, ctx->fmt1, line);
}


void memory(struct element *ctx) {
	FILE *meminfo_f = fopen("/proc/meminfo", "r");
	unsigned long memtotal = 0, memavailable = 0;
	unsigned long used;
	char line[50];

	while (fgets(line, sizeof(line), meminfo_f) && (!memtotal || !memavailable)) {
		if (PREFIX(line, "MemTotal:"))
			memtotal = strtoul(line + strlen("MemTotal:"), NULL, 10);
		else if (PREFIX(line, "MemAvailable:"))
			memavailable = strtoul(line + strlen("MemAvailable:"), NULL, 10);
	}

	fclose(meminfo_f);

	memtotal *= 1024;
	memavailable *= 1024;
	used = memtotal - memavailable;

	if (used < GB)
		sprintf(ctx->buf, ctx->fmt1, (float) used / MB);
	else
		sprintf(ctx->buf, ctx->fmt2, (float) used / GB);
}

void cpu(struct element *ctx) {
	FILE *stat_f = fopen("/proc/stat", "r");
	struct cpu_usage curr;
	unsigned int diff_idle, diff_total;
	float usage;

	fscanf(stat_f, "cpu  %u %u %u %u",
			&curr.user, &curr.nice, &curr.system, &curr.idle);
	fclose(stat_f);

	curr.total = curr.user + curr.nice + curr.system + curr.idle;
	diff_idle = curr.idle - prev.idle;
	diff_total = curr.total - prev.total;
	prev = curr;
	usage = (float) (diff_total - diff_idle) / (diff_total + 1) * 100;

	if (usage >= 90)
		sprintf(ctx->buf, ctx->fmt2, usage);
	else
		sprintf(ctx->buf, ctx->fmt1, usage);
}

void temperature(struct element *ctx) {
	FILE *temperature_f = fopen(HWMON_PATH, "r");
	int temperature;

	if (!temperature_f) return;

	fscanf(temperature_f, "%d", &temperature);
	fclose(temperature_f);

	temperature /= 1000;

	if (temperature >= 70)
		sprintf(ctx->buf, ctx->fmt2, temperature);
	else
		sprintf(ctx->buf, ctx->fmt1, temperature);
}

void power(struct element *ctx) {
	FILE *power_now_f = fopen(BATTERY_PATH "/power_now", "r");
	unsigned int power_now;

	if (!power_now_f) return;

	fscanf(power_now_f, "%u", &power_now);
	fclose(power_now_f);

	sprintf(ctx->buf, ctx->fmt1, (float) power_now / 1e6);
}

void battery(struct element *ctx) {
	FILE *capacity_f = fopen(BATTERY_PATH "/capacity", "r");
	FILE *status_f = fopen(BATTERY_PATH "/status", "r");
	unsigned int capacity;
	char status[20];

	if (!capacity_f || !status_f) return;

	fscanf(capacity_f, "%u", &capacity);
	fscanf(status_f, "%s", status);
	fclose(capacity_f);
	fclose(status_f);

	if (strcmp(status, "Charging") == 0)
		sprintf(ctx->buf, ctx->fmt2, capacity);
	else if (capacity <= 10)
		sprintf(ctx->buf, ctx->fmt3, capacity);
	else
		sprintf(ctx->buf, ctx->fmt1, capacity);
}

int wifi_cb(struct nl_msg *msg, void *data) {
	struct element **ctx = data;
	char *ssid;
	struct nlattr *attrs[NL80211_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));

	if (nla_parse(attrs, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
			genlmsg_attrlen(gnlh, 0), NULL) < 0)
		return NL_SKIP;

	if (attrs[NL80211_ATTR_SSID]) {
		ssid = nla_get_string(attrs[NL80211_ATTR_SSID]);
		sprintf((*ctx)->buf, (*ctx)->fmt1, ssid);
	} else {
		sprintf((*ctx)->buf, (*ctx)->fmt2);
	}

	return NL_STOP;
}

void wifi(struct element *ctx) {
	struct nl_sock *sk = nl_socket_alloc();
	if (genl_connect(sk) < 0) {
		nl_socket_free(sk);
		return;
	}

	struct nl_msg *msg = nlmsg_alloc();
	if (!msg) {
		nl_socket_free(sk);
		return;
	}

	genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, genl_ctrl_resolve(sk, "nl80211"),
			0, 0, NL80211_CMD_GET_INTERFACE, 0);
	nla_put_u32(msg, NL80211_ATTR_IFINDEX, if_nametoindex(WIFI_DEVICE));

	nl_socket_modify_cb(sk, NL_CB_VALID, NL_CB_CUSTOM, wifi_cb, &ctx);
	nl_send_sync(sk, msg);

	nl_socket_free(sk);
}

void date(struct element *ctx) {
	time_t t = time(NULL);
	struct tm tm = *localtime(&t);
	char *day;

	switch (tm.tm_wday) {
		case 0:
			day = "Sun";
			break;
		case 1:
			day = "Mon";
			break;
		case 2:
			day = "Tue";
			break;
		case 3:
			day = "Wed";
			break;
		case 4:
			day = "Thu";
			break;
		case 5:
			day = "Fri";
			break;
		case 6:
			day = "Sat";
			break;
	}

	sprintf(ctx->buf, ctx->fmt1,
			day, tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, tm.tm_min);
}

void print_status(void) {
	char line[500] = "";

	for (struct element *e = elements; e < elements + nr_elems; e++) {
		if (e->func && !e->dontcall)
			e->func(e);

		if (*e->buf) {
			if (!line[0]) // first element
				strcat(line, " ");
			else
				strcat(line, SEP);

			strcat(line, e->buf);
		}
	}
	strcat(line, " ");

	printf("%s\n", line);
	fflush(stdout);
}

void refresh(int signal) {
	// run all functions that don't normally run during the main loop
	for (struct element *e = elements; e < elements + nr_elems; e++) {
		if (e->func && e->dontcall == 1)
			e->func(e);
	}
	// running print_status() is not needed, as nanosleep() gets interrupted
	// upon receiving a signal
}

void quit(int signal) {
	stop_program = 1;
}

int main() {
	struct timespec print_interval = {.tv_sec = 1, .tv_nsec = 0};

	signal(SIGINT, quit);
	signal(SIGTERM, quit);
	signal(SIGUSR1, refresh);

	for (struct element *e = elements; e < elements + nr_elems; e++) {
		e->buf = malloc(100);

		if (e->func == volume) {
			volume_ctx = e;
			e->dontcall = 2;
		} else if (e->func == sleep_state || e->func == kb_layout) {
			e->dontcall = 1;
		}
	}

	setup_pulse();

	while (!stop_program) {
		nanosleep(&print_interval, NULL);
		print_status();
	}

	cleanup_pulse();

	for (struct element *e = elements; e < elements + nr_elems; e++)
		free(e->buf);

	return 0;
}
