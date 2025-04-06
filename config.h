#define HWMON_PATH		"/sys/class/hwmon/hwmon2/temp1_input"
#define BATTERY_PATH	"/sys/class/power_supply/BAT0"
#define WIFI_DEVICE		"wlo1"

#define FG_AC	"37bf7c"
#define FG_WN	"ffff00"
#define FG_UR	"ff5050"

#define SEP		"     "

struct element elements[] = {
	{ volume,	"^fg(" FG_AC ")V^fg() %.0f%%",					/* normal */
				"^fg(" FG_AC ")V^fg() muted" },					/* muted */
	{ kb_layout,	"^fg(" FG_AC ")K^fg() %s" },
	{ sleep_state,	"",											/* normal */
					"^fg(" FG_WN ")INSOMNIA^fg()" },			/* sleep inhibition */
	{ memory,	"^fg(" FG_AC ")M^fg() %.0fM",					/* < 1GB */
				"^fg(" FG_AC ")M^fg() %.1fG" },					/* > 1GB */
	{ cpu,		"^fg(" FG_AC ")C^fg() %02.0f%%",						/* normal */
				"^fg(" FG_AC ")C^fg() ^fg(" FG_UR ")%02.0f%%^fg()" },	/* high */
	{ temperature,	"^fg(" FG_AC ")T^fg() %d°C",						/* normal */
					"^fg(" FG_AC ")T^fg() ^fg(" FG_UR ")%d°C^fg()" },	/* high */
	{ power,	"^fg(" FG_AC ")P^fg() %3.1fW" },
	{ battery,	"^fg(" FG_AC ")B^fg() %u%%",						/* normal */
				"^fg(" FG_AC ")Ch^fg() %u%%",						/* charging */
				"^fg(" FG_AC ")B^fg() ^fg(" FG_UR ")%u%%^fg()" },	/* low battery */
	{ wifi,		"^fg(" FG_AC ")W^fg() %s",							/* connected */
				"^fg(" FG_UR ")disconnected^fg()" },				/* disconnected */
	{ date,		"^fg(" FG_AC ")D^fg() %s %02d-%02d  %d:%02d" }
};
