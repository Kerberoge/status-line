#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <net/if.h>
#include <linux/nl80211.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>

#define FG_AC "7a72b5"
#define FG_UR "ff5050"
#define SEP "     "

#define WIFI_INTERFACE "wlo1"

#define MB 1048576
#define GB 1073741824

struct cpu_usage {
	unsigned int user, nice, system, idle, total;
};

int stop_program = 0;
struct cpu_usage prev = {0, 0, 0, 0, 0};

void handle_signals(int signal) {
	stop_program = 1;
}

int startswith(char *a, char *b) {
	return !strncmp(a, b, strlen(b));
}

void memory(char *buffer) {
	FILE *meminfo_f = fopen("/proc/meminfo", "r");
	unsigned long memtotal = 0, memavailable = 0;
	unsigned long used;
	char line[50];

	while (fgets(line, sizeof(line), meminfo_f) && (!memtotal || !memavailable)) {
		if (startswith(line, "MemTotal:"))
			memtotal = strtoul(line + strlen("MemTotal:"), NULL, 10);
		else if (startswith(line, "MemAvailable:"))
			memavailable = strtoul(line + strlen("MemAvailable:"), NULL, 10);
	}

	fclose(meminfo_f);

	memtotal *= 1024;
	memavailable *= 1024;
	used = memtotal - memavailable;

	if (used < GB)
		sprintf(buffer, "^fg(" FG_AC ")^fg() %.0fM", (float) used / MB);
	else
		sprintf(buffer, "^fg(" FG_AC ")^fg() %.1fG", (float) used / GB);
}

void cpu(char *buffer) {
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
	usage = (float) (diff_total - diff_idle) / diff_total * 100;

	if (usage >= 90)
		sprintf(buffer, "^fg(" FG_AC ")^fg() ^fg(" FG_UR ")%02.0f%%^fg()", usage);
	else
		sprintf(buffer, "^fg(" FG_AC ")^fg() %02.0f%%", usage);
}

void temperature(char *buffer) {
	FILE *temperature_f = fopen("/sys/class/hwmon/hwmon3/temp1_input", "r");
	int temperature;

	if (!temperature_f)
		return;

	fscanf(temperature_f, "%d", &temperature);
	fclose(temperature_f);

	temperature /= 1000;

	if (temperature >= 70)
		sprintf(buffer, "^fg(" FG_AC ")^fg() ^fg(" FG_UR ")%d°C^fg()", temperature);
	else
		sprintf(buffer, "^fg(" FG_AC ")^fg() %d°C", temperature);
}

void battery(char *buffer) {
	FILE *capacity_f = fopen("/sys/class/power_supply/BAT0/capacity", "r");
	FILE *status_f = fopen("/sys/class/power_supply/BAT0/status", "r");
	unsigned int capacity;
	char status[20];

	if (!capacity_f || !status_f)
		return;

	fscanf(capacity_f, "%u", &capacity);
	fscanf(status_f, "%s", status);
	fclose(capacity_f);
	fclose(status_f);

	if (strcmp(status, "Charging") == 0)
		sprintf(buffer, "^fg(" FG_AC ") ^fg() %u%%", capacity);
	else if (capacity <= 10)
		sprintf(buffer, "^fg(" FG_AC ")^fg() ^fg(" FG_UR ")%u%%^fg()", capacity);
	else
		sprintf(buffer, "^fg(" FG_AC ")^fg() %u%%", capacity);
}

int wifi_cb(struct nl_msg *msg, void *data) {
	char **ssid = data;
	struct nlattr *attrs[NL80211_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));

	if (nla_parse(attrs, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
			genlmsg_attrlen(gnlh, 0), NULL) < 0)
		return NL_SKIP;

	if (attrs[NL80211_ATTR_SSID])
		*ssid = nla_get_string(attrs[NL80211_ATTR_SSID]);
	else
		*ssid = "^fg(" FG_UR ")disconnected^fg()";

	return NL_STOP;
}

void wifi(char *buffer) {
	char *ssid;

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
	nla_put_u32(msg, NL80211_ATTR_IFINDEX, if_nametoindex(WIFI_INTERFACE));

	nl_socket_modify_cb(sk, NL_CB_VALID, NL_CB_CUSTOM, wifi_cb, &ssid);
	nl_send_sync(sk, msg);

	nl_socket_free(sk);

	if (!ssid)
		return;

	sprintf(buffer, "^fg(" FG_AC ")^fg() %s", ssid);
}

void date(char *buffer) {
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

	sprintf(buffer, "^fg(" FG_AC ")^fg() %s %02d-%02d" SEP \
			"^fg(" FG_AC ")^fg() %d:%02d",
			day, tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, tm.tm_min);
}

void print_status() {
	char line[300] = "";
	char vol_str[50] = "", mem_str[50] = "", cpu_str[50] = "", temp_str[50] = "",
			bat_str[50] = "", wifi_str[50] = "", date_str[50] = "";

	memory(mem_str);
	cpu(cpu_str);
	temperature(temp_str);
	battery(bat_str);
	wifi(wifi_str);
	date(date_str);

	sprintf(line, "%s" SEP "%s" SEP "%s" SEP "%s" SEP "%s" SEP "%s",
			mem_str, cpu_str, temp_str, bat_str, wifi_str, date_str);

	printf("%s\n", line);
	fflush(stdout);
}

int main() {
	struct timespec ts = {.tv_sec = 1, .tv_nsec = 0};

	signal(SIGINT, handle_signals);
	signal(SIGTERM, handle_signals);

	while (!stop_program) {
		print_status();
		nanosleep(&ts, NULL);
	}

	return 0;
}
