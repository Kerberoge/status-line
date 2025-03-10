PREFIX		= ~/.local

CFLAGS		= -I/usr/include/libnl3
LDLIBS		= -lnl-3 -lnl-genl-3 -lpulse -lpthread

# Remove functions that are never used
# See: https://stackoverflow.com/a/6770305
CFLAGS		+= -fdata-sections -ffunction-sections
LDFLAGS		+= -Wl,--gc-sections

status-line: status-line.c

clean:
	rm status-line

install:
	install -s -D -t $(PREFIX)/bin status-line

.PHONY: clean
