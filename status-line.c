#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <libgen.h>
#include <sys/inotify.h>
#include <pulse/pulseaudio.h>	// next 2 are for the volume module
#include <pthread.h>
#include <net/if.h>				// next 4 are for the wifi module
#include <linux/nl80211.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>

#define MB	1048576
#define GB	1073741824

#define ELEMS_FOREACH(it)	for (struct element *it = elements; \
								it < elements + sizeof(elements) / sizeof(struct element); \
								it++)
#define PREFIX(str, prefix)	!strncmp(str, prefix, strlen(prefix))

struct element {
	void (*func)();
	int call;
	char *fmt1, *fmt2, *fmt3;
	char buf[100];
	void *data;
};

struct pulse_data {
	pthread_t thread;
	int readfd, writefd;
	struct element *ctx;
	pa_mainloop *mainloop;
	pa_mainloop_api *mainloop_api;
	pa_context *context;
	int failed;
};

struct inotify_data {
	int fd;
	char name[100]; /* filename to watch */
	struct element *ctx;
	uint8_t iev[sizeof(struct inotify_event) + NAME_MAX + 1];
};

struct cpu_data {
	unsigned int user, nice, system, idle, total;
};

void create_pulse_context(struct pulse_data *pd);
void *pulse_worker(void *data);
void context_state_cb(pa_context *c, void *data);
void subscribe_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t i, void *data);
void volume(pa_context *c, const pa_sink_info *info, int eol, void *data);

int inotify_setup(struct element *ctx, struct inotify_data *idata);
void inotify_quit(struct inotify_data *idata);
int inotify_handle(struct inotify_data *idata);
void sleep_state(struct element *ctx);

void memory(struct element *ctx);
void cpu(struct element *ctx);
void temperature(struct element *ctx);
void battery(struct element *ctx);
int wifi_cb(struct nl_msg *msg, void *data);
void wifi(struct element *ctx);
void date(struct element *ctx);

void print_status(void);
void quit(int signal);

#include "config.h"

int stop_program = 0;

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

void context_state_cb(pa_context *c, void *data) {
	struct pulse_data *pdata = data;

	switch (pa_context_get_state(c)) {
		case PA_CONTEXT_READY:
			// correct volume should be displayed even when no event is received
			pa_context_get_sink_info_by_name(c, "@DEFAULT_SINK@", volume, pdata);

			pa_context_set_subscribe_callback(pdata->context, subscribe_cb, pdata);
			pa_context_subscribe(pdata->context, PA_SUBSCRIPTION_MASK_SINK, NULL, NULL);
			break;
		case PA_CONTEXT_FAILED:
			pdata->failed = 1;
			break;
	}
}

void subscribe_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t i, void *data) {
	pa_context_get_sink_info_by_name(c, "@DEFAULT_SINK@", volume, data);
}

void volume(pa_context *c, const pa_sink_info *info, int eol, void *data) {
	struct pulse_data *pdata = data;
	int response = 1;

	if (eol > 0 || !info) return;

	if (info->mute)
		sprintf(pdata->ctx->buf, pdata->ctx->fmt2);
	else
		sprintf(pdata->ctx->buf, pdata->ctx->fmt1,
				(float) pa_cvolume_avg(&info->volume) / PA_VOLUME_NORM * 100);

	write(pdata->writefd, &response, sizeof(response));
}

int inotify_setup(struct element *ctx, struct inotify_data *idata) {
	char path[100];
	char *dir, *fname;

	strcpy(path, SLEEP_STATE_PATH);
	fname = basename(path);
	dir = dirname(path);

	idata->fd = inotify_init();
	inotify_add_watch(idata->fd, dir, IN_CREATE | IN_DELETE);
	strcpy(idata->name, fname);
	idata->ctx = ctx;

	return idata->fd;
}

void inotify_quit(struct inotify_data *idata) {
	close(idata->fd);
}

int inotify_handle(struct inotify_data *idata) {
	struct inotify_event *e = (struct inotify_event *) idata->iev;

	read(idata->fd, idata->iev, sizeof(idata->iev));

	if (strncmp(idata->name, e->name, e->len) == 0) {
		idata->ctx->func(idata->ctx);
		return 1;
	}

	return 0;
}

void sleep_state(struct element *ctx) {
	FILE *inhibit_sleep_f = fopen(SLEEP_STATE_PATH, "r");

	if (inhibit_sleep_f) {
		fclose(inhibit_sleep_f);
		sprintf(ctx->buf, ctx->fmt2);
	} else {
		sprintf(ctx->buf, ctx->fmt1);
	}
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
	struct cpu_data *prev = ctx->data, curr;
	unsigned int diff_idle, diff_total;
	float usage;

	fscanf(stat_f, "cpu  %u %u %u %u",
			&curr.user, &curr.nice, &curr.system, &curr.idle);
	fclose(stat_f);

	curr.total = curr.user + curr.nice + curr.system + curr.idle;
	diff_idle = curr.idle - prev->idle;
	diff_total = curr.total - prev->total;
	*prev = curr;
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
	struct nlattr *attrs[NL80211_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	char *ssid = data;
	int len;

	if (nla_parse(attrs, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
			genlmsg_attrlen(gnlh, 0), NULL) < 0)
		return NL_SKIP;

	if (attrs[NL80211_ATTR_SSID]) {
		len = nla_len(attrs[NL80211_ATTR_SSID]);
		memcpy(ssid, nla_data(attrs[NL80211_ATTR_SSID]), len);
		ssid[len] = '\0';
	}

	return NL_STOP;
}

void wifi(struct element *ctx) {
	struct nl_sock *sk = nl_socket_alloc();
	char ssid[100] = {0};

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

	nl_socket_modify_cb(sk, NL_CB_VALID, NL_CB_CUSTOM, wifi_cb, ssid);
	nl_send_sync(sk, msg);

	if (ssid[0])
		sprintf(ctx->buf, ctx->fmt1, ssid);
	else
		sprintf(ctx->buf, ctx->fmt2);

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

	ELEMS_FOREACH(e) {
		if (e->call)
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

void quit(int signal) {
	stop_program = 1;
}

void noop(int signal) { }

uint32_t calc_diff_ms(struct timespec *start, struct timespec *end) {
	struct timespec diff;
	uint32_t diff_ms;

	diff.tv_sec = end->tv_sec - start->tv_sec;
	diff.tv_nsec = end->tv_nsec - start->tv_nsec;
	diff_ms = diff.tv_sec * 1000 + diff.tv_nsec / 1e6;

	return diff_ms;
}

int main() {
	enum { PULSE, INOTIFY };
	struct pollfd pfds[2] = { [0 ... 1] = { .fd = -1, .events = POLLIN } };
	struct timespec start, end;
	const int interval = 1000; /* ms */
	int time_remaining = interval;
	struct pulse_data pdata;
	struct inotify_data idata;
	struct cpu_data cdata;
	int ret;

	signal(SIGINT, quit);
	signal(SIGTERM, quit);
	signal(SIGUSR1, noop);

	ELEMS_FOREACH(e) {
		if (e->func == volume) {
			pfds[PULSE].fd = pulse_setup(e, &pdata);
		} else if (e->func == sleep_state) {
			pfds[INOTIFY].fd = inotify_setup(e, &idata);
		} else if (e->func == cpu) {
			e->data = &cdata;
		}
	}

	while (!stop_program) {
		clock_gettime(CLOCK_MONOTONIC, &start);
		ret = poll(pfds, 2, time_remaining);
		clock_gettime(CLOCK_MONOTONIC, &end);

		time_remaining -= calc_diff_ms(&start, &end);
		if (time_remaining <= 0) time_remaining = interval;

		if (ret > 0 && pfds[PULSE].revents & POLLIN) { /* volume was updated */
			pulse_handle(&pdata);
			print_status();
		} else if (ret > 0 && pfds[INOTIFY].revents & POLLIN) { /* watched files changed */
			if (inotify_handle(&idata))
				print_status();
		} else if (ret == 0) { /* timeout expired */
			print_status();
		}
	}

	pulse_quit(&pdata);
	inotify_quit(&idata);

	return 0;
}
