#include <sys/inotify.h>

#define NWATCHES 2

struct inotify_watch {
	int wd;
	uint32_t mask;
	char name[100];
	struct element *ctx;
};

struct inotify_data {
	int fd;
	uint8_t iev[sizeof(struct inotify_event) + NAME_MAX + 1];
	struct inotify_watch iw[NWATCHES];
};

int inotify_setup(struct inotify_data *idata);
void inotify_quit(struct inotify_data *idata);
int inotify_handle(struct inotify_data *idata);
void sleep_setup(struct element *ctx, struct inotify_data *idata);
void kblayout_setup(struct element *ctx, struct inotify_data *idata);
