#pragma once
#include "ext2d.h"
#include <stdbool.h>
#include <sys/types.h>

struct e2device; /* provided by the user */
typedef void *(*e2device_req)(struct e2device *dev, size_t len, size_t off);
/* 0 on success, -1 on failure to write */
typedef int (*e2device_drop)(struct e2device *dev, void *ptr, bool dirty);
/* mustn't return 0 */
typedef uint32_t (*e2device_gettime32)(struct e2device *dev);

struct ext2 {
	struct e2device *dev;
	e2device_req req;
	e2device_drop drop;
	e2device_gettime32 gettime32;

	bool rw;
	uint32_t groups;
	uint64_t block_size, frag_size, inode_size;
	uint64_t inodes_per_group, blocks_per_group;
};

struct ext2_diriter {
	struct ext2d_dirent *ent;

	struct {
		bool needs_reset;
		size_t pos;
		/* ent points here */
		char buf[sizeof(struct ext2d_dirent) + 256];
	} _internal;
};

/* opendev.c */
struct ext2 *ext2_opendev(struct e2device *dev, e2device_req req_fn, e2device_drop drop_fn);
void ext2_free(struct ext2 *fs);

/* read.c */
static inline int ext2_dropreq(struct ext2 *fs, void *ptr, bool dirty) {
	return fs->drop(fs->dev, ptr, dirty);
}
struct ext2d_inode *ext2_req_inode(struct ext2 *fs, uint32_t inode_n);
void *ext2_req_file(struct ext2 *fs, uint32_t inode_n, size_t *len, size_t off);
struct ext2d_bgd *ext2_req_bgdt(struct ext2 *fs, uint32_t idx);
struct ext2d_superblock *ext2_req_sb(struct ext2 *fs);

int ext2_inodepos(struct ext2 *fs, uint32_t inode);
int ext2_read(struct ext2 *fs, uint32_t inode_n, void *buf, size_t len, size_t off);
bool ext2_diriter(struct ext2_diriter *iter, struct ext2 *fs, uint32_t inode_n);

/** Returns the on-disk address and available length of the inode at pos.
 * On success, *dev_len > 0. */
int ext2_inode_ondisk(struct ext2 *fs, uint32_t inode_n, size_t pos, size_t *dev_off, size_t *dev_len);

uint32_t ext2c_walk(struct ext2 *fs, const char *path, size_t plen);

/* write.c */
int ext2_write(struct ext2 *fs, uint32_t inode_n, const void *buf, size_t len, size_t off);
int ext2_link(struct ext2 *fs, uint32_t dir_n, const char *name, uint32_t target_n, int flags);
/** @return the corresponding inode, 0 on failure */
uint32_t ext2_unlink(struct ext2 *fs, uint32_t dir_n, const char *name);
/** @return the allocated inode, 0 on failure */
uint32_t ext2_alloc_inode(struct ext2 *fs, uint16_t perms);
