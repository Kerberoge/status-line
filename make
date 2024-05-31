#/bin/bash

gcc status-line.c -o status-line \
	-I/usr/include/libnl3 \
	-lnl-3 -lnl-genl-3 -lpulse -lpthread

strip ./status-line

if [[ $1 == "-i" ]]; then
	mv ./status-line ~/.local/bin/
fi
