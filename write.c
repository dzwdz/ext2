#include "ext2.h"
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

int
ext2_write(struct ext2 *fs, uint32_t inode_n, const void *buf, size_t len, size_t off)
{
	uint64_t dev_off, dev_len;
	if (!fs->rw) return -1;

	/* test run, see if we can access all the space */
	for (size_t pos = 0; pos < len; ) {
		if (ext2_inode_ondisk(fs, inode_n, off + pos, &dev_off, &dev_len) < 0) {
			return -1;
		}
		pos += dev_len;
	}

	/* do it for real */
	for (size_t pos = 0; pos < len; ) {
		void *p;
		if (ext2_inode_ondisk(fs, inode_n, off + pos, &dev_off, &dev_len) < 0) {
			return -1;
		}
		if (dev_len > len - pos) {
			dev_len = len - pos;
		}
		p = fs->req(fs->dev, dev_len, dev_off);
		if (!p) return -1;
		/* This memcpy is certainly not optimal, but hopefully it's drowned out
		 * by the IO cost. */
		memcpy(p, buf + pos, dev_len);
		if (fs->drop(fs->dev, p, true) < 0) {
			return -1;
		}
		pos += dev_len;
	}

	// TODO size64
	struct ext2d_inode *inode = ext2_req_inode(fs, inode_n);
	if (!inode) {
		return -1;
	}
	if (inode->size_lower < len + off) {
		inode->size_lower = len + off;
	}
	if (fs->drop(fs->dev, inode, true) < 0) {
		return -1;
	}

	return len;
}


// TODO integrate into other stuff
/** Requests the inode bitmap for a given group. *len is purely an output parameter. */
static void *
ext2_req_ib(struct ext2 *fs, uint32_t group, size_t *len)
{
	struct ext2d_bgd *bgd;
	uint32_t ib_addr;
	bgd = ext2_req_bgdt(fs, group);
	if (!bgd) {
		return NULL;
	}
	ib_addr = bgd->inode_bitmap;
	ext2_dropreq(fs, bgd, false);
	if (len) *len = fs->block_size;
	return fs->req(fs->dev, fs->block_size, fs->block_size * ib_addr);
}

static int
bitmap_findfree(uint8_t *bitmap, int len, uint32_t *target)
{
	for (int byte = 0; byte < len; byte++) {
		if (bitmap[byte] == 0xFF) continue;
		for (int bit = 0; bit < 7; bit++) {
			if ((bitmap[byte] & (1 << bit)) == 0) {
				*target = byte * 8 + bit;
				return 0;
			}
		}
	}
	return -1;
}

uint32_t
ext2_alloc_inode(struct ext2 *fs, uint16_t perms)
{
	uint32_t inode_n;
	uint32_t group = 0;
	uint32_t idx = 0;
	struct ext2d_inode *inode;
	if (!fs->rw) return -1;
	{
		size_t ib_len;
		uint8_t *ib = ext2_req_ib(fs, group, &ib_len);
		if (!ib) return 0;
		if (bitmap_findfree(ib, ib_len, &idx) < 0) return 0;
		if (!(idx < fs->inodes_per_group)) return 0;

		assert((ib[idx / 8] & 1 << (idx % 8)) == 0);
		ib[idx / 8] |= 1 << (idx % 8);

		if (ext2_dropreq(fs, ib, true) < 0) {
			return 0;
		}
		inode_n = idx+1;
	}
	{
		struct ext2d_bgd *bgd;
		bgd = ext2_req_bgdt(fs, group);
		if (!bgd) {
			return -1;
		}
		bgd->inodes_free--;
		if (ext2_dropreq(fs, bgd, true) < 0) {
			return -1;
		}
	}
	{
		struct ext2d_superblock *sb;
		sb = ext2_req_sb(fs);
		if (!sb) {
			return -1;
		}
		sb->inodes_free--;
		if (ext2_dropreq(fs, sb, true) < 0) {
			return -1;
		}
	}
	inode = ext2_req_inode(fs, inode_n);
	if (!inode) {
		return 0;
	}
	memset(inode, 0, sizeof *inode);
	inode->perms = perms;
	inode->ctime = fs->gettime32(fs->dev);
	if (ext2_dropreq(fs, inode, true) < 0) {
		return 0;
	}
	return inode_n;
}
