#define HWMON_PATH		"/sys/class/hwmon/hwmon2/temp1_input"
#define BATTERY_PATH	"/sys/class/power_supply/BAT0"
#define WIFI_DEVICE		"wlp3s0"

#define FG_AC	"#82abff"
#define FG_WN	"#ffff00"
#define FG_UR	"#ff5050"

#define SEP		"        "

struct element elements[] = {
	{ volume,	"<span color='" FG_AC "'>V</span>  %.0f%%",			/* normal */
				"<span color='" FG_AC "'>V</span>  muted" },		/* muted */
	{ sleep_state,	"<span color='" FG_AC "'>S</span>  off",		/* normal */
					"<span color='" FG_AC "'>S</span>  on" },		/* sleep inhibition */
	{ memory,	"<span color='" FG_AC "'>M</span>  %.0fM",			/* < 1GB */
				"<span color='" FG_AC "'>M</span>  %.1fG" },		/* > 1GB */
	{ cpu,		"<span color='" FG_AC "'>C</span>  %02.0f%%",									/* normal */
				"<span color='" FG_AC "'>C</span>  <span color='" FG_UR "'>%02.0f%%</span>" },	/* high */
	{ temperature,	"<span color='" FG_AC "'>T</span>  %d°C",									/* normal */
					"<span color='" FG_AC "'>T</span>  <span color='" FG_UR "'>%d°C</span>" },	/* high */
	{ power,	"<span color='" FG_AC "'>P</span>  %3.1fW" },
	{ battery,	"<span color='" FG_AC "'>B</span>  %u%%",									/* normal */
				"<span color='" FG_AC "'>Ch</span>  %u%%",									/* charging */
				"<span color='" FG_AC "'>B</span>  <span color='" FG_UR "'>%u%%</span>" },	/* low battery */
	{ wifi,		"<span color='" FG_AC "'>W</span>  %s",												/* connected */
				"<span color='" FG_AC "'>W</span>  <span color='" FG_UR "'>disconnected</span>" },	/* disconnected */
	{ date,		"<span color='" FG_AC "'>D</span>  %s %02d-%02d  %d:%02d" }
};
