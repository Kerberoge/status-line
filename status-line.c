#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <poll.h>

#include "pulse.h"
#include "inotify.h"
#include "modules.h"
#include "config.h"

int stop_program = 0;

void quit(int signal) {
	stop_program = 1;
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
				strcat(line, separator);

			strcat(line, e->buf);
		}
	}
	strcat(line, " ");

	printf("%s\n", line);
	fflush(stdout);
}

int main() {
	enum { PULSE, INOTIFY };
	struct pollfd pfds[2] = { [0 ... 1] = { .fd = -1, .events = POLLIN } };
	struct pulse_data pdata;
	struct inotify_data idata;
	struct cpu_data cdata;
	int ret;

	signal(SIGINT, quit);
	signal(SIGTERM, quit);

	pfds[INOTIFY].fd = inotify_setup(&idata);

	ELEMS_FOREACH(e) {
		if (e->func == volume) {
			pfds[PULSE].fd = pulse_setup(e, &pdata);
		} else if (e->func == kblayout) {
			kblayout_setup(e, &idata);
		} else if (e->func == sleep_state) {
			sleep_setup(e, &idata);
		} else if (e->func == cpu) {
			e->data = &cdata;
		}
	}

	while (!stop_program) {
		ret = poll(pfds, 2, 1000);

		if (ret > 0 && pfds[PULSE].revents & POLLIN) { /* volume was updated */
			pulse_handle(&pdata);
			print_status();
		} else if (ret > 0 && pfds[INOTIFY].revents & POLLIN) { /* watched files changed */
			inotify_handle(&idata);
			print_status();
		} else if (ret == 0) { /* timeout expired */
			print_status();
		}
	}

	pulse_quit(&pdata);
	inotify_quit(&idata);

	return 0;
}
