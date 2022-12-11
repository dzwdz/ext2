/* A simple example caching req/drop implementation.
 * Not part of the library, the user is expected to make one that matches
 * their requirements. */

#include "ex_cache.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

struct span {
	size_t start, end; /* including start, not including end */
	void *buf;
};

#define SPANAMT 64 /* must be a power of 2 */

struct e2device {
	exc_read read_fn;
	exc_write write_fn;
	void *userdata;

	struct span *active;
	struct span spans[SPANAMT];
	int span_pos;

	void *lastptr; /* for debugging */

	struct {
		unsigned long full, partial, none;
	} stats;
};

struct e2device *
exc_init(exc_read read_fn, exc_write write_fn, void *userdata)
{
	struct e2device *dev;
	dev = calloc(1, sizeof *dev);
	if (!dev) {
		return NULL;
	}
	dev->read_fn = read_fn;
	dev->write_fn = write_fn;
	dev->userdata = userdata;
	return dev;
}

void
exc_free(struct e2device *dev)
{
	fprintf(stderr, "cache full    %7lu\n", dev->stats.full);
	fprintf(stderr, "cache partial %7lu\n", dev->stats.partial);
	fprintf(stderr, "cache none    %7lu\n", dev->stats.none);

	for (int i = 0; i < SPANAMT; i++) {
		struct span *s = &dev->spans[i];
		free(s->buf);
	}
	free(dev);
}

static struct span *
find_span(struct e2device *dev)
{
	struct span *s;
	for (int i = 0; i < SPANAMT; i++) {
		s = &dev->spans[i];
		if (s->buf == NULL) return s;
	}
	s = &dev->spans[dev->span_pos];
	dev->span_pos = (dev->span_pos + 1) & (SPANAMT - 1);
	return s;
}

void *
exc_req(struct e2device *dev, size_t len, size_t off)
{
	assert(dev->active == NULL);

	for (int i = 0; i < SPANAMT; i++) {
		struct span *s = &dev->spans[i];
		if (s->buf == NULL) continue;
		if (s->start <= off && off + len <= s->end) {
			// TODO move to front
			dev->active = s;
			dev->stats.full++;
			dev->lastptr = s->buf + off - s->start;
			return dev->lastptr;
		}
		/* remove partial overlaps */
		if ((s->start <= off && off < s->end) ||
			(s->start <= off+len && off+len < s->end))
		{
			dev->stats.partial++;
			s->buf = NULL;
		}
	}
	dev->stats.none++;

	struct span *s = find_span(dev);
	s->start = off;
	s->end = off + len;

	/* round to an assumed 4k block size */
	s->start = s->start & ~4095;
	s->end = (off + len + 4095) & ~4095;
	s->buf = malloc(s->end - s->start);
	if (!s->buf) {
		return NULL;
	}
	if (dev->read_fn(dev->userdata, s->buf, s->end - s->start, s->start) < 0) {
		free(s->buf);
		s->buf = NULL;
		return NULL;
	}
	dev->active = s;
	dev->lastptr = s->buf + off - s->start;
	return dev->lastptr;
}

int
exc_drop(struct e2device *dev, void *ptr, bool dirty)
{
	struct span *s = dev->active;
	int ret = 0;
	assert(s != NULL);
	assert(dev->lastptr == ptr);
	if (dirty && dev->write_fn(dev->userdata, s->buf, s->end - s->start, s->start) < 0) {
		ret = -1;
	}
	dev->active = NULL;
	return ret;
}
