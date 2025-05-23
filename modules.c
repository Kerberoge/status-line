#include <stdio.h>
#include <time.h>
#include <net/if.h>
#include <linux/nl80211.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>

#include "modules.h"
#include "pulse.h"

#define MB	1048576
#define GB	1073741824

#define PREFIX(str, prefix)	!strncmp(str, prefix, strlen(prefix))

extern const char *hwmon_path;
extern const char *battery_path;
extern const char *wifi_device;
extern const char *sleep_state_path;
extern const char *kblayout_path;

void volume(struct element *ctx) {
	struct volume_data *vdata = ctx->data;

	if (!vdata)
		return;

	if (vdata->mute)
		sprintf(ctx->buf, ctx->fmt2);
	else
		sprintf(ctx->buf, ctx->fmt1, vdata->volume);
}

void sleep_state(struct element *ctx) {
	FILE *inhibit_sleep_f = fopen(sleep_state_path, "r");

	if (inhibit_sleep_f) {
		fclose(inhibit_sleep_f);
		sprintf(ctx->buf, ctx->fmt2);
	} else {
		sprintf(ctx->buf, ctx->fmt1);
	}
}

void kblayout(struct element *ctx) {
	FILE *kblayout_f = fopen(kblayout_path, "r");
	char line[100];

	if (!kblayout_f) return;

	fscanf(kblayout_f, "%s", line);
	fclose(kblayout_f);
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
	FILE *temperature_f = fopen(hwmon_path, "r");
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
	char power_now_path[100], volt_now_path[100], curr_now_path[100];
	FILE *power_now_f, *volt_now_f, *curr_now_f;
	float power_now, volt_now, curr_now;

	sprintf(power_now_path, "%s/power_now", battery_path);
	sprintf(volt_now_path, "%s/voltage_now", battery_path);
	sprintf(curr_now_path, "%s/current_now", battery_path);

	if (power_now_f = fopen(power_now_path, "r")) {
		fscanf(power_now_f, "%f", &power_now);
		fclose(power_now_f);

		power_now /= 1e6;
	} else if ((volt_now_f = fopen(volt_now_path, "r"))
			&& (curr_now_f = fopen(curr_now_path, "r"))) {
		fscanf(volt_now_f, "%f", &volt_now);
		fscanf(curr_now_f, "%f", &curr_now);
		fclose(volt_now_f);
		fclose(curr_now_f);

		power_now = volt_now * curr_now / 1e12;
	} else {
		return;
	}

	sprintf(ctx->buf, ctx->fmt1, power_now);
}

void battery(struct element *ctx) {
	char capacity_path[100], status_path[100];
	FILE *capacity_f, *status_f;
	unsigned int capacity;
	char status[20];

	sprintf(capacity_path, "%s/capacity", battery_path);
	sprintf(status_path, "%s/status", battery_path);

	capacity_f = fopen(capacity_path, "r");
	status_f = fopen(status_path, "r");

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
	nla_put_u32(msg, NL80211_ATTR_IFINDEX, if_nametoindex(wifi_device));

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
