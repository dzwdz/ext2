/* Functions for requesting specific structures / data from the filesystem. */
#include "ext2.h"
#include <assert.h>
#include <stddef.h>
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

void *
ext2_req_bitmap(struct ext2 *fs, uint32_t group, enum ext2_bitmap type)
{
	struct ext2d_bgd *bgd;
	uint32_t b_addr;
	bgd = ext2_req_bgdt(fs, group);
	if (!bgd) {
		return NULL;
	}
	if (type == Ext2Inode) {
		b_addr = bgd->inode_bitmap;
	} else { /* type == Ext2Block */
		b_addr = bgd->block_bitmap;
	}
	ext2_dropreq(fs, bgd, false);
	return fs->req(fs->dev, fs->block_size, fs->block_size * b_addr);
}

uint32_t *
ext2_req_blockmap(struct ext2 *fs, uint32_t inode_n, size_t *len, uint32_t off, bool alloc)
{
	if (alloc && !fs->rw) return NULL;
	if (off < 12) {
		int ioff = ext2_inodepos(fs, inode_n);
		if (ioff < 0) return NULL;

		*len = 12 - off;
		assert(*len > 0);
		return fs->req(fs->dev, *len * 4, ioff + offsetof(struct ext2d_inode, block) + 4 * off);
	} else if (off - 12 < fs->block_size / 4) {
		uint32_t indirect;

		struct ext2d_inode *inode;
		inode = ext2_req_inode(fs, inode_n);
		if (!inode) return NULL;
		indirect = inode->indirect_1;
		ext2_dropreq(fs, inode, false);

		if (indirect == 0) {
			if (!alloc) return NULL;
			indirect = ext2_alloc_block(fs);
			if (indirect == 0) return NULL;

			inode = ext2_req_inode(fs, inode_n);
			if (!inode) return NULL;
			inode->indirect_1 = indirect;
			if (ext2_dropreq(fs, inode, true) < 0) {
				return NULL;
			}
		}

		off -= 12;
		*len = fs->block_size / 4 - off;
		assert(*len > 0);
		return fs->req(fs->dev, *len * 4, indirect * fs->block_size + off * 4);
	} else {
		return NULL;
	}
}
