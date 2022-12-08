#include "ext2.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int ext2_readbgdt(struct ext2 *fs);
static int ext2_rawread(struct ext2 *fs, void *buf, size_t len, size_t off);

struct ext2 *
ext2_opendev(struct e2device *dev, e2device_req req_fn, e2device_drop drop_fn)
{
	struct ext2 *fs = malloc(sizeof *fs);
	if (!fs) return NULL;
	memset(fs, 0, sizeof *fs);
	fs->dev = dev;
	fs->req = req_fn;
	fs->drop = drop_fn;

	if (ext2_rawread(fs, &fs->super, sizeof fs->super, 1024) < 0)
		goto err;
	if (fs->super.magic != EXT2D_SUPERBLOCK_MAGIC)
		goto err;
	if (fs->super.v_major < 1)
		goto err;

	if (fs->super.features_ro & ~(EXT2D_FEATURE_RO_DIRTYPE))
		goto err; /* unsupported mandatory feature */

	fs->rw = fs->super.features_ro == EXT2D_FEATURE_RO_DIRTYPE
		&& fs->super.features_rw == (EXT2D_FEATURE_RW_SPARSE_SUPER | EXT2D_FEATURE_RW_SIZE64);

	uint32_t groups1, groups2;
	groups1 = (fs->super.blocks_total + (fs->super.blocks_per_group - 1)) / fs->super.blocks_per_group;
	groups2 = (fs->super.inodes_total + (fs->super.inodes_per_group - 1)) / fs->super.inodes_per_group;
	if (groups1 != groups2) goto err;
	fs->groups = groups1;

	if (fs->super.block_size_log > 63 - 10) goto err;
	fs->block_size = 1024 << fs->super.block_size_log;

	if (fs->super.frag_size_log > 63 - 10) goto err;
	fs->frag_size = 1024 << fs->super.frag_size_log;

	if (ext2_readbgdt(fs) < 0) goto err;

	return fs;
err:
	free(fs);
	return NULL;
}

void
ext2_free(struct ext2 *fs)
{
	if (!fs) return;
	free(fs->bgdt);
	free(fs);
}

static int
ext2_readbgdt(struct ext2 *fs)
{
	// TODO no overflow check, could result in memory corruption
	size_t len = sizeof(*fs->bgdt) * fs->groups;
	if (len > INT_MAX)
		len = INT_MAX;
	size_t block = fs->block_size == 1024 ? 2 : 1;

	if (fs->bgdt) free(fs->bgdt);
	fs->bgdt = malloc(len);
	if (!fs->bgdt)
		return -1;
	if (ext2_rawread(fs, fs->bgdt, len, block * fs->block_size) < 0)
		return -1;

	return 0;
}

static int
ext2_rawread(struct ext2 *fs, void *buf, size_t len, size_t off)
{
	void *p = fs->req(fs->dev, len, off);
	if (!p) return -1;
	memcpy(buf, p, len);
	fs->drop(fs->dev, p, false);
	return len;
}
