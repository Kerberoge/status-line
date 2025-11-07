#include <sys/param.h>  /* MIN() */
#define PREFIX(str, prefix)		!strncmp(str, prefix, strlen(prefix))
#define WHITESPACE(c)			((c) == ' ' || (c) == '\t')
