#define HWMON_PATH			"/sys/class/hwmon/hwmon2/temp1_input"
#define BATTERY_PATH		"/sys/class/power_supply/BAT0"
#define WIFI_DEVICE			"wlo1"
#define SLEEP_STATE_PATH	"/tmp/inhibit_sleep"

#define FG_AC	"37bf7c"
#define FG_WN	"ffff00"
#define FG_UR	"ff5050"

#define C(color)	"<span color='#" color "'>"
#define CEND		"</span>"

#define SEP		"     "

struct element elements[] = {
	{ volume,		0,	C(FG_AC) "V " CEND "%.0f%%",						/* normal */
						C(FG_AC) "V " CEND "muted" },						/* muted */
	{ sleep_state,	0,	"",													/* normal */
						C(FG_WN) "INSOMNIA" CEND },							/* sleep inhibited */
	{ memory,		1,	C(FG_AC) "M " CEND "%.0fM",							/* < 1GB */
						C(FG_AC) "M " CEND "%.1fG" },						/* > 1GB */
	{ cpu,			1,	C(FG_AC) "C " CEND "%02.0f%%",						/* normal */
						C(FG_AC) "C " CEND C(FG_UR) "%02.0f%%" CEND },		/* high */
	{ temperature,	1,	C(FG_AC) "T " CEND "%d°C",							/* normal */
						C(FG_AC) "T " CEND C(FG_UR) "%d°C" CEND },			/* high */
	{ battery,		1,	C(FG_AC) "B " CEND "%u%%",							/* normal */
						C(FG_AC) "Ch " CEND "%u%%",							/* charging */
						C(FG_AC) "B " CEND C(FG_UR) "%u%%" CEND },			/* low battery */
	{ wifi,			1,	C(FG_AC) "W " CEND "%s",							/* connected */
						C(FG_AC) "W " CEND C(FG_UR) "disconnected" CEND },	/* disconnected */
	{ date,			1,	C(FG_AC) "D " CEND "%s %02d-%02d  %d:%02d" }
};
