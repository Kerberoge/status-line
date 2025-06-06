const char *hwmon_path			= "/sys/class/hwmon/hwmon2/temp1_input";
const char *battery_path		= "/sys/class/power_supply/BAT0";
const char *wifi_device			= "wlo1";
const char *sleep_state_path	= "/tmp/inhibit_sleep";
const char *kblayout_path		= "/tmp/dwl/kblayout";
const char *separator			= "     ";

#define FG_AC	"37bf7c"
#define FG_WN	"ffff00"
#define FG_UR	"ff5050"

struct element elements[] = {
	{ volume,		0,	"^fg(" FG_AC ")V^fg() %.0f%%",							/* normal */
						"^fg(" FG_AC ")V^fg() muted" },							/* muted */
	{ kblayout,		0,	"^fg(" FG_AC ")K^fg() %s" },
	{ sleep_state,	0,	"",														/* normal */
						"^fg(" FG_WN ")INSOMNIA^fg()" },						/* sleep inhibited */
	{ memory,		1,	"^fg(" FG_AC ")M^fg() %.0fM",							/* < 1GB */
						"^fg(" FG_AC ")M^fg() %.1fG" },							/* > 1GB */
	{ cpu,			1,	"^fg(" FG_AC ")C^fg() %02.0f%%",						/* normal */
						"^fg(" FG_AC ")C^fg() ^fg(" FG_UR ")%02.0f%%^fg()" },	/* high */
	{ temperature,	1,	"^fg(" FG_AC ")T^fg() %d°C",							/* normal */
						"^fg(" FG_AC ")T^fg() ^fg(" FG_UR ")%d°C^fg()" },		/* high */
	{ power,		1,	"^fg(" FG_AC ")P^fg() %3.1fW" },
	{ battery,		1,	"^fg(" FG_AC ")B^fg() %u%%",							/* normal */
						"^fg(" FG_AC ")Ch^fg() %u%%",							/* charging */
						"^fg(" FG_AC ")B^fg() ^fg(" FG_UR ")%u%%^fg()" },		/* low battery */
	{ wifi,			1,	"^fg(" FG_AC ")W^fg() %s",								/* connected */
						"^fg(" FG_AC ")W^fg(" FG_UR ") disconnected^fg()" },	/* disconnected */
	{ date,			1,	"^fg(" FG_AC ")D^fg() %s %02d-%02d  %d:%02d" }
};
