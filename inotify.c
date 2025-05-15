#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>
#include <sys/inotify.h>

#include "inotify.h"
#include "element.h"

extern const char *sleep_state_path;
extern const char *kblayout_path;
enum { IN_SLEEP, IN_KBLAYOUT };

int inotify_setup(struct inotify_data *idata) {
	return idata->fd = inotify_init();
}

void inotify_quit(struct inotify_data *idata) {
	close(idata->fd);
}

int inotify_watch_match(struct inotify_watch *w, struct inotify_event *e) {
	if (w->wd != e->wd || ! w->mask & e->mask)
		return 0;
	else if (w->name[0] == '\0' && e->len == 0)
		return 1;
	else if (strncmp(w->name, e->name, e->len) == 0)
		return 1;
	else
		return 0;
}

void inotify_handle(struct inotify_data *idata) {
	struct inotify_watch *w;

	read(idata->fd, idata->iev, sizeof(idata->iev));

	for (w = idata->iw; w < idata->iw + NWATCHES; w++) {
		if (inotify_watch_match(w, (struct inotify_event *) idata->iev))
			w->ctx->func(w->ctx);
	}
}

void sleep_setup(struct element *ctx, struct inotify_data *idata) {
	char path[100];
	char *dir, *fname;

	struct inotify_watch iw = {
		.mask = IN_CREATE | IN_DELETE,
		.ctx = ctx
	};

	strcpy(path, sleep_state_path);
	fname = basename(path);
	dir = dirname(path);
	strcpy(iw.name, fname);

	iw.wd = inotify_add_watch(idata->fd, dir, iw.mask);

	idata->iw[IN_SLEEP] = iw;
}

void kblayout_setup(struct element *ctx, struct inotify_data *idata) {
	struct inotify_watch iw = {
		.mask = IN_CLOSE_WRITE,
		.name[0] = '\0',
		.ctx = ctx
	};

	iw.wd = inotify_add_watch(idata->fd, kblayout_path, iw.mask);

	idata->iw[IN_KBLAYOUT] = iw;
}
