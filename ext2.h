#pragma once
#include "ext2d.h"
#include <stdbool.h>
#include <sys/types.h>

struct e2device; /* provided by the user */
typedef int (*e2device_read)(struct e2device *dev, void *buf, size_t len, size_t off);

struct ext2 {
	struct e2device *dev;
	e2device_read read;

	struct ext2d_superblock super;
	struct ext2d_block_group_desc *bgdt;

	/* all computed from the superblock - could just be macros instead */
	bool rw;
	uint32_t groups;
	uint64_t block_size, frag_size;
};

struct ext2_diriter {
	struct ext2d_dirent *ent;

	struct {
		bool needs_reset;
		size_t pos;
		/* if you're wondering what the fuck that is - i was trying to make this
		 * struct have no external allocations, so it wouldn't need to be manually freed.
		 * in hindsight, that might've not been too smart*/
		char buf[sizeof(struct ext2d_dirent) + 256];
	} _internal;
};

/* opendev.c */
struct ext2 *ext2_opendev(struct e2device *dev, e2device_read read_fn);
void ext2_free(struct ext2 *fs);

/* read.c */
int ext2_readinode(struct ext2 *fs, uint32_t inode, void *buf, size_t len);
int ext2_read(struct ext2 *fs, struct ext2d_inode *inode, void *buf, size_t len, size_t off);
bool ext2_diriter(struct ext2_diriter *iter, struct ext2 *fs, struct ext2d_inode *inode);

/** Returns the on-disk address and available length of the inode at pos. */
int ext2_inode_ondisk(struct ext2 *fs, struct ext2d_inode *inode, size_t pos, size_t *dev_off, size_t *dev_len);

uint32_t ext2c_walk(struct ext2 *fs, const char *path, size_t plen);
