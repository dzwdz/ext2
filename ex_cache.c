/* A simple example caching req/drop implementation.
 * Not part of the library, the user is expected to make one that matches
 * their requirements. */

#include "ex_cache.h"
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

struct e2device {
	exc_read read_fn;
	exc_write write_fn;
	void *userdata;

	bool busy; /* for debugging */

	size_t lastlen, lastoff; /* temporary hack */
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
	free(dev);
}

void *
exc_req(struct e2device *dev, size_t len, size_t off)
{
	void *p = malloc(len);
	if (!p) return NULL;
	assert(!dev->busy);
	dev->busy = true;
	dev->lastlen = len;
	dev->lastoff = off;

	if (dev->read_fn(dev->userdata, p, len, off) < 0) {
		free(p);
		dev->busy = false;
		return NULL;
	}
	return p;
}

int
exc_drop(struct e2device *dev, void *ptr, bool dirty)
{
	int ret = 0;
	assert(dev->busy);
	dev->busy = false;
	if (dirty && dev->write_fn(dev->userdata, ptr, dev->lastlen, dev->lastoff) < 0) {
		ret = -1;
	}
	free(ptr);
	return ret;
}
