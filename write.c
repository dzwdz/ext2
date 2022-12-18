#include "ext2.h"
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

int
ext2_write(struct ext2 *fs, uint32_t inode_n, const void *buf, size_t len, size_t off)
{
	struct ext2d_inode *inode;
	uint64_t dev_off, dev_len;
	if (!fs->rw) return -1;

	if (ext2_alloc_space(fs, inode_n, off + len) < 0) {
		return -1;
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

	inode = ext2_req_inode(fs, inode_n);
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

int
ext2i_bitmap_alloc(uint8_t *bitmap, size_t buflen, size_t bitlen, uint32_t *target)
{
	for (size_t byte = 0; byte < buflen && byte < bitlen / 8; byte++) {
		if (bitmap[byte] == 0xFF) continue;
		for (size_t bit = 0; bit < 7 && byte * 8 + bit < bitlen; bit++) {
			if ((bitmap[byte] & (1 << bit)) == 0) {
				bitmap[byte] |= 1 << bit;
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
	if (!fs->rw) return 0;
	{
		uint8_t *ib = ext2_req_bitmap(fs, group, Ext2Inode);
		if (!ib) {
			return 0;
		}
		if (ext2i_bitmap_alloc(ib, fs->block_size, fs->inodes_per_group, &idx) < 0) {
			ext2_dropreq(fs, ib, false);
			return 0;
		}
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

uint32_t
ext2_alloc_block(struct ext2 *fs)
{
	uint32_t group = 0;
	uint32_t idx = 0;
	uint32_t block;
	{
		uint8_t *bitmap = ext2_req_bitmap(fs, group, Ext2Block);
		if (!bitmap) {
			return 0;
		}
		if (ext2i_bitmap_alloc(bitmap, fs->block_size, fs->blocks_per_group, &idx) < 0) {
			ext2_dropreq(fs, bitmap, false);
			return 0;
		}
		if (ext2_dropreq(fs, bitmap, true) < 0) {
			return 0;
		}
		block = idx+1;
	}
	{
		struct ext2d_bgd *bgd;
		bgd = ext2_req_bgdt(fs, group);
		if (!bgd) {
			return 0;
		}
		bgd->blocks_free--;
		if (ext2_dropreq(fs, bgd, true) < 0) {
			return 0;
		}
	}
	{
		struct ext2d_superblock *sb;
		sb = ext2_req_sb(fs);
		if (!sb) {
			return 0;
		}
		sb->blocks_free--;
		if (ext2_dropreq(fs, sb, true) < 0) {
			return 0;
		}
	}
	char *b = fs->req(fs->dev, fs->block_size, block * fs->block_size);
	if (!b) {
		return 0;
	}
	memset(b, 0, fs->block_size);
	if (ext2_dropreq(fs, b, true) < 0) {
		return 0;
	}
	return block;
}

int
ext2_alloc_space(struct ext2 *fs, uint32_t inode_n, size_t len)
{
	struct ext2d_inode *inode = NULL;
	bool dirty = false;
	if (!fs->rw) return 0;

	for (uint64_t iblock = 0; iblock * fs->block_size < len; iblock++) {
		uint64_t dblock;
		if (!inode) {
			inode = ext2_req_inode(fs, inode_n);
			if (!inode) return -1;
		}
		if (iblock >= 12) {
			// TODO indirect blocks
			return -1;
		}
		dblock = inode->block[iblock];
		if (dblock != 0) continue;
		if (ext2_dropreq(fs, inode, dirty) < 0) {
			return -1;
		}
		inode = NULL;
		dirty = false;

		dblock = ext2_alloc_block(fs);
		if (dblock == 0) return -1;

		inode = ext2_req_inode(fs, inode_n);
		if (!inode) return -1;
		inode->block[iblock] = dblock;
		inode->sectors += fs->block_size / 512;
		dirty = true;
	}
	if (inode && ext2_dropreq(fs, inode, dirty) < 0) {
		return -1;
	}

	/* test run, see if we can access all the space */
	// TODO remove this hack
	for (size_t pos = 0; pos < len; ) {
		size_t dev_off, dev_len;
		if (ext2_inode_ondisk(fs, inode_n, pos, &dev_off, &dev_len) < 0) {
			return -1;
		}
		pos += dev_len;
	}
	return 0;
}
