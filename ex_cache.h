#pragma once
#include <stdbool.h>
#include <sys/types.h>

/* -1 on failure, 0 on success */
typedef int (*exc_read)(void *userdata, void *buf, size_t len, size_t off);
typedef int (*exc_write)(void *userdata, const void *buf, size_t len, size_t off);

struct e2device;
struct e2device *exc_init(exc_read read_fn, exc_write write_fn, void *userdata);
void exc_free(struct e2device *dev);
void *exc_req(struct e2device *dev, size_t len, size_t off);
int exc_drop(struct e2device *dev, void *ptr, bool dirty);
