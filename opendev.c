#include "ext2.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static uint32_t ext2_default_gettime32(struct e2device *dev);

struct ext2 *
ext2_opendev(struct e2device *dev, e2device_req req_fn, e2device_drop drop_fn)
{
	struct ext2 *fs;
	struct ext2d_superblock *sb;
	uint32_t groups1, groups2;

	fs = malloc(sizeof *fs);
	if (!fs) return NULL;
	memset(fs, 0, sizeof *fs);
	fs->dev = dev;
	fs->req = req_fn;
	fs->drop = drop_fn;
	fs->gettime32 = ext2_default_gettime32;

	sb = ext2_req_sb(fs);
	if (!sb)
		goto err;
	if (sb->magic != EXT2D_SUPERBLOCK_MAGIC)
		goto err;
	if (sb->v_major < 1)
		goto err;
	if (sb->features_ro & ~(EXT2D_FEATURE_RO_DIRTYPE))
		goto err; /* unsupported mandatory feature */

	fs->rw = sb->features_ro == EXT2D_FEATURE_RO_DIRTYPE
		&& sb->features_rw == (EXT2D_FEATURE_RW_SPARSE_SUPER | EXT2D_FEATURE_RW_SIZE64);

	groups1 = (sb->blocks_total + (sb->blocks_per_group - 1)) / sb->blocks_per_group;
	groups2 = (sb->inodes_total + (sb->inodes_per_group - 1)) / sb->inodes_per_group;
	if (groups1 != groups2) goto err;
	fs->groups = groups1;

	if (sb->block_size_log > 63 - 10) goto err;
	fs->block_size = 1024 << sb->block_size_log;

	if (sb->frag_size_log > 63 - 10) goto err;
	fs->frag_size = 1024 << sb->frag_size_log;

	fs->inodes_per_group = sb->inodes_per_group;
	fs->blocks_per_group = sb->blocks_per_group;
	fs->inode_size = sb->inode_size;
	ext2_dropreq(fs, sb, false);

	return fs;
err:
	if (sb) {
		ext2_dropreq(fs, sb, false);
	}
	free(fs);
	return NULL;
}

void
ext2_free(struct ext2 *fs)
{
	if (!fs) return;
	free(fs);
}

static uint32_t
ext2_default_gettime32(struct e2device *dev)
{
	(void)dev;
	return ~0;
}
