#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>				// for setup_dwlb() and cleanup_dwlb()
#include <time.h>				// for nanosleep()
#include <sys/wait.h>			// for wait() in cleanup_dwlb()
#include <pulse/pulseaudio.h>	// next 2 are for the volume module
#include <pthread.h>
#include <net/if.h>				// next 4 are for the wifi module
#include <linux/nl80211.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>

#define FG_AC	"337a57"
#define FG_UR	"ff5050"
#define SEP		"     "

#define HWMON_PATH		"/sys/class/hwmon/hwmon2/temp1_input"
#define BATTERY_PATH	"/sys/class/power_supply/BAT0"
#define WIFI_DEVICE		"wlo1"

#define MB 1048576
#define GB 1073741824

struct pa_connection {
	pa_mainloop *mainloop;
	pa_mainloop_api *mainloop_api;
	pa_context *context;
	pthread_t thread;
	int failed;
	int try_count;
};

struct cpu_usage {
	unsigned int user, nice, system, idle, total;
};

void handle_signals(int signal);
void setup_dwlb(void);
void cleanup_dwlb(void);
void setup_pulse(void);
void cleanup_pulse(void);
void create_pulse_context(void);
void *pulse_worker(void *data);
void context_state_cb(pa_context *c, void *data);
void update_volume_cb(pa_context *c, const pa_sink_info *info, int eol, void *data);
void subscribe_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t i, void *data);
void volume(char *buffer);
void sleep_state(char *buffer);
int startswith(char *a, char *b);
void memory(char *buffer);
void cpu(char *buffer);
void temperature(char *buffer);
void battery(char *buffer);
int wifi_cb(struct nl_msg *msg, void *data);
void wifi(char *buffer);
void date(char *buffer);
void write_status(void);

int stop_program = 0;
int dwlb_pipe[2];
struct pa_connection pa_con = {.try_count = 0};
int audio_volume = 0, audio_muted = 0;
struct cpu_usage prev = {0, 0, 0, 0, 0};

void handle_signals(int signal) {
	stop_program = 1;
}

void setup_dwlb(void) {
	pid_t p;

	pipe(dwlb_pipe);
	p = fork();

	if (p > 0) {
		/* Parent */
		close(dwlb_pipe[0]);
	} else if (p == 0) {
		/* Child */
		close(dwlb_pipe[1]);
		dup2(dwlb_pipe[0], STDIN_FILENO);
		close(dwlb_pipe[0]);
		execlp("dwlb", "dwlb", "-status-stdin", "all", NULL);
	}
}

void cleanup_dwlb(void) {
	close(dwlb_pipe[1]);
	wait(NULL);
}

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
	
	pa_con.try_count++;
}

void *pulse_worker(void *data) {
	struct timespec retry_interval = {.tv_sec = 0, .tv_nsec = 100000000}; // 0.1s

	pa_con.mainloop = pa_mainloop_new();
	pa_con.mainloop_api = pa_mainloop_get_api(pa_con.mainloop);

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
			// Correct volume should be displayed even when no event is received
			pa_context_get_sink_info_by_name(c, "@DEFAULT_SINK@", update_volume_cb, NULL);

			pa_context_set_subscribe_callback(pa_con.context, subscribe_cb, NULL);
			pa_context_subscribe(pa_con.context, PA_SUBSCRIPTION_MASK_SINK, NULL, NULL);
			break;
		case PA_CONTEXT_FAILED:
			pa_con.failed = 1;
			break;
	}
}

void subscribe_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t i, void *data) {
	pa_context_get_sink_info_by_name(c, "@DEFAULT_SINK@", update_volume_cb, NULL);
}

void update_volume_cb(pa_context *c, const pa_sink_info *info, int eol, void *data) {
	if (eol > 0 || !info) return;

	audio_volume = pa_cvolume_avg(&info->volume);
	audio_muted = info->mute;

	write_status();
}

void volume(char *buffer) {
	if (audio_muted) {
		sprintf(buffer, "%d %s", pa_con.try_count, pa_con.try_count == 1 ? "try" : "tries");
		sprintf(buffer, "%s    ^fg(" FG_AC ")^fg() muted", buffer);
	} else
		sprintf(buffer, "^fg(" FG_AC ")^fg() %.0f%%",
				(float) audio_volume / PA_VOLUME_NORM * 100);
}

void sleep_state(char *buffer) {
	FILE *inhibit_sleep_f = fopen("/tmp/inhibit_sleep", "r");

	if (inhibit_sleep_f) {
		fclose(inhibit_sleep_f);
		sprintf(buffer, "^fg(" FG_AC ")^fg() on");
		return;
	}

	sprintf(buffer, "^fg(" FG_AC ")^fg() off");
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
	usage = (float) (diff_total - diff_idle) / (diff_total + 1) * 100;

	if (usage >= 90)
		sprintf(buffer, "^fg(" FG_AC ")^fg() ^fg(" FG_UR ")%02.0f%%^fg()", usage);
	else
		sprintf(buffer, "^fg(" FG_AC ")^fg() %02.0f%%", usage);
}

void temperature(char *buffer) {
	FILE *temperature_f = fopen(HWMON_PATH, "r");
	int temperature;

	if (!temperature_f) return;

	fscanf(temperature_f, "%d", &temperature);
	fclose(temperature_f);

	temperature /= 1000;

	if (temperature >= 70)
		sprintf(buffer, "^fg(" FG_AC ")^fg() ^fg(" FG_UR ")%d°C^fg()", temperature);
	else
		sprintf(buffer, "^fg(" FG_AC ")^fg() %d°C", temperature);
}

void battery(char *buffer) {
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
	nla_put_u32(msg, NL80211_ATTR_IFINDEX, if_nametoindex(WIFI_DEVICE));

	nl_socket_modify_cb(sk, NL_CB_VALID, NL_CB_CUSTOM, wifi_cb, &ssid);
	nl_send_sync(sk, msg);

	nl_socket_free(sk);

	if (!ssid) return;

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

void write_status(void) {
	char line[400] = "";
	char vol_str[50] = "", slp_str[50] = "", mem_str[50] = "",
			cpu_str[50] = "", temp_str[50] = "", bat_str[50] = "",
			wifi_str[50] = "", date_str[100] = "";

	volume(vol_str);
	sleep_state(slp_str);
	memory(mem_str);
	cpu(cpu_str);
	temperature(temp_str);
	battery(bat_str);
	wifi(wifi_str);
	date(date_str);

	sprintf(line, "%s" SEP "%s" SEP "%s" SEP "%s" SEP "%s" SEP "%s" SEP "%s" SEP "%s\n",
			vol_str, slp_str, mem_str, cpu_str, temp_str, bat_str, wifi_str, date_str);

	write(dwlb_pipe[1], line, strlen(line));
}

int main() {
	struct timespec write_interval = {.tv_sec = 1, .tv_nsec = 0};

	signal(SIGINT, handle_signals);
	signal(SIGTERM, handle_signals);

	setup_dwlb();
	setup_pulse();

	while (!stop_program) {
		write_status();
		nanosleep(&write_interval, NULL);
	}

	cleanup_pulse();
	cleanup_dwlb();

	return 0;
}
