/* Functions for requesting specific structures / data from the filesystem. */
#include "ext2.h"
#include <stdlib.h>

static int ext2_inodepos(struct ext2 *fs, uint32_t inode);

static int
ext2_inodepos(struct ext2 *fs, uint32_t inode)
{
	struct ext2d_bgd *bgd;
	int ret = -1;
	uint32_t group = (inode - 1) / fs->inodes_per_group;
	uint32_t idx   = (inode - 1) % fs->inodes_per_group;
	if (group >= fs->groups) return -1;
	bgd = ext2_req_bgdt(fs, group);
	if (bgd) {
		ret = fs->block_size * bgd->inode_table + idx * fs->inode_size;
		ext2_dropreq(fs, bgd, false);
	}
	return ret;
}

struct ext2d_inode *
ext2_req_inode(struct ext2 *fs, uint32_t inode_n)
{
	int off = ext2_inodepos(fs, inode_n);
	if (off < 0) return NULL;
	return fs->req(fs->dev, sizeof(struct ext2d_inode), off);
}

void *
ext2_req_file(struct ext2 *fs, uint32_t inode_n, size_t *len, size_t off)
{
	uint64_t dev_off, dev_len;
	size_t size, og_len = *len;
	{
		struct ext2d_inode *inode = ext2_req_inode(fs, inode_n);
		if (!inode) return NULL;
		size = inode->size_lower;
		ext2_dropreq(fs, inode, false);
	}
	if (off >= size) {
		*len = 0;
		return NULL;
	}
	if (ext2_inode_ondisk(fs, inode_n, off, &dev_off, &dev_len) < 0) {
		return NULL;
	}
	*len = dev_len;
	if (*len > size - off)
		*len = size - off;
	if (og_len && *len > og_len)
		*len = og_len;
	return fs->req(fs->dev, *len, dev_off);
}

struct ext2d_bgd *
ext2_req_bgdt(struct ext2 *fs, uint32_t idx)
{
	size_t block;
	if (!(idx < fs->groups)) return NULL;
	block = fs->block_size == 1024 ? 2 : 1;
	return fs->req(fs->dev, sizeof(struct ext2d_bgd), block * fs->block_size);
}

struct ext2d_superblock *
ext2_req_sb(struct ext2 *fs)
{
	return fs->req(fs->dev, sizeof (struct ext2d_superblock), 1024);
}
