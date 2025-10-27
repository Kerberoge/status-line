#include <stdlib.h>
#include <string.h>
#include <sys/param.h>	/* MIN() */

#define PREFIX(str, prefix)		!strncmp(str, prefix, strlen(prefix))
#define WHITESPACE(c)			((c) == ' ' || (c) == '\t')

int get_var(char *dest, size_t dest_size, const char *varname,
		const char *file_contents, size_t file_size) {
	const char *pos = file_contents, *nlpos;
	char val[100];
	int val_len;
	int found = 0;

	while (pos < file_contents + file_size) {
		nlpos = strchr(pos, '\n');
		if (!nlpos) nlpos = file_contents + file_size;
		for(; WHITESPACE(*pos); pos++);

		if (PREFIX(pos, "set $")) {
			pos += strlen("set $");
			if (PREFIX(pos, varname) && WHITESPACE(*(pos + strlen(varname)))) {
				pos += strlen(varname);
				for(; WHITESPACE(*pos); pos++);
				val_len = MIN(nlpos - pos, sizeof(val) - 1);
				found = val_len > 0 ? 1 : 0;
				strncpy(val, pos, val_len);
				val[val_len] = '\0';
			}
		}

		pos = nlpos + 1;
	}

	if (val[0] == '$') {
		return get_var(dest, dest_size, &val[1], file_contents, file_size);
	} else if (found) {
		strncpy(dest, val, dest_size - 1);
		dest[dest_size - 1] = '\0';
	}

	/* 0 when found, -1 when not found */
	return found - 1;
}

int get_sway_colors(const char *sway_config_path) {
	FILE *sway_config_f = fopen(sway_config_path, "r");
	char *buf, *val;
	size_t buflen = 1; /* getdelim() will realloc() anyway */
	int ret = 0;

	if (!sway_config_f) return 1;

	buf = malloc(buflen);
	getdelim(&buf, &buflen, 0, sway_config_f); /* reads whole file */

	/* make sure to catch any non-zero return values */
	ret |= get_var(colors.warning, sizeof(colors.warning), "warning", buf, buflen);
	ret |= get_var(colors.urgent, sizeof(colors.urgent), "urgent", buf, buflen);
	ret |= get_var(colors.accent, sizeof(colors.accent), "bar_ac_fg", buf, buflen);
	free(buf);

	return ret;
}
