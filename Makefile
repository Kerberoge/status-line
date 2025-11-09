PREFIX		= ~/.local

CFLAGS		= -I/usr/include/libnl3 -DHOME=\"$(HOME)\"
LDLIBS		= -lnl-3 -lnl-genl-3 -lpulse -lpthread

# Remove functions that are never used
# See: https://stackoverflow.com/a/6770305
CFLAGS		+= -fdata-sections -ffunction-sections
LDFLAGS		+= -Wl,--gc-sections

status-line: config.h util.h colors.h status-line.c
	$(CC) $(CFLAGS) $(LDFLAGS) status-line.c $(LDLIBS) -o $@

clean:
	rm status-line

install:
	install -s -D -t $(PREFIX)/bin status-line

.PHONY: clean
