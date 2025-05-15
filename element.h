#ifndef PARSER_H
#define PARSER_H

struct element {
	void (*func)();
	int call;
	char *fmt1, *fmt2, *fmt3;
	char buf[100];
	void *data;
};

#define ELEMS_FOREACH(it)	for (struct element *it = elements; \
								it < elements + sizeof(elements) / sizeof(struct element); \
								it++)

#endif
