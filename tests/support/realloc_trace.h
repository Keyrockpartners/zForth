/* Force-included by tests/limits.sh: log every dictionary allocation */
#include <stdio.h>
#include <stdlib.h>

static inline void *test_realloc(void *p, size_t n)
{
	fprintf(stderr, "[realloc %zu]\n", n);
	return realloc(p, n);
}

#define ZF_REALLOC(p, n) test_realloc((p), (n))
