PREFIX		= ~/.local

CFLAGS		= -I/usr/include/libnl3
LDLIBS		= -lnl-3 -lnl-genl-3 -lpulse -lpthread
HEADERS		= config.h pulse.h inotify.h modules.h
SRCFILES	= pulse.c inotify.c modules.c status-line.c

# Remove functions that are never used
# See: https://stackoverflow.com/a/6770305
CFLAGS		+= -fdata-sections -ffunction-sections
LDFLAGS		+= -Wl,--gc-sections

status-line: $(HEADERS) $(SRCFILES)
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRCFILES) $(LDLIBS) -o $@

clean:
	rm status-line

install:
	install -s -D -t $(PREFIX)/bin status-line

.PHONY: clean install
