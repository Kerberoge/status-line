#define HWMON_PATH			"/sys/class/hwmon/hwmon3/temp1_input"
#define BATTERY_PATH		"/sys/class/power_supply/BAT0"
#define WIFI_DEVICE			"wlo1"
#define SLEEP_STATE_PATH	"/tmp/inhibit_sleep"
#define SWAY_CONFIG_PATH	HOME "/.config/sway/config"

#define SEP		"     "

/* start and end of colored text */
#define CSTART(color)	"<span color='", color, "'>"
#define CEND			"</span>"

#define WN	CSTART(colors.warning)
#define UR	CSTART(colors.urgent)
#define AC	CSTART(colors.accent)

struct element elements[] = {
	{ volume,		0,	{ AC, "V ", CEND, "%.0f%%" },					/* normal */
						{ AC, "V ", CEND, "muted" } },					/* muted */
	{ kblayout,		0,	{ AC, "K ", CEND, "%s" } },
	{ sleep_state,	0,	{ },  											/* normal */
						{ WN, "INSOMNIA", CEND } },						/* sleep inhibited */
	{ memory,		1,	{ AC, "M ", CEND, "%.0fM" },					/* < 1GB */
						{ AC, "M ", CEND, "%.1fG" } },					/* > 1GB */
	{ cpu,			1,	{ AC, "C ", CEND, "%02.0f%%" },					/* normal */
						{ AC, "C ", CEND, UR, "%02.0f%%", CEND } },		/* high */
	{ temperature,	1,	{ AC, "T ", CEND, "%d°C" },						/* normal */
						{ AC, "T ", CEND, UR, "%d°C", CEND } },			/* high */
	{ battery,		1,	{ AC, "B ", CEND, "%u%%" },						/* normal */
						{ AC, "Ch ", CEND, "%u%%" },					/* charging */
						{ AC, "B ", CEND, UR, "%u%%", CEND } },			/* low battery */
	{ wifi,			1,	{ AC, "W ", CEND, "%s" },						/* connected */
						{ AC, "W ", CEND, UR, "disconnected", CEND } },	/* disconnected */
	{ date,			1,	{ AC, "D ", CEND, "%s %02d-%02d  %d:%02d" } }
};
