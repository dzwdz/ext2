#include "ext2.h"
#include <limits.h>
#include <stddef.h>

int
ext2_readinode(struct ext2 *fs, uint32_t inode, void *buf, size_t len)
{
	uint32_t group = (inode - 1) / fs->super.inodes_per_group;
	uint32_t idx   = (inode - 1) % fs->super.inodes_per_group;
	uint32_t off;
	if (group >= fs->groups) return -1;
	off  = fs->block_size * fs->bgdt[group].inode_table;
	off += idx * fs->super.inode_size;

	if (len > fs->super.inode_size)
		len = fs->super.inode_size;
	if (fs->read(fs->dev, buf, len, off) != (int)len)
		return -1;
	return len;
}

int
ext2_read(struct ext2 *fs, struct ext2d_inode *inode, void *buf, size_t len, size_t off)
{
	if (len > INT_MAX)
		len = INT_MAX;
	if (len > inode->size_lower - off)
		len = inode->size_lower - off;
	if (off >= inode->size_lower)
		return 0;

	size_t pos = 0;
	while (pos < len) {
		// TODO unnecessary division by power of 2
		uint64_t block     = (off + pos) / fs->block_size;
		uint64_t block_off = (off + pos) % fs->block_size;
		uint64_t block_len = fs->block_size - block_off;
		if (block_len > len - pos)
			block_len = len - pos;

		// TODO indirect blocks
		if (block >= 12) return -1;
		block = inode->block[block];
		if (fs->read(fs->dev, buf + pos, block_len, block * fs->block_size + block_off) != (int)block_len)
			return -1;
		pos += block_len;
	}
	return pos;
}

bool
ext2_diriter(struct ext2_diriter *iter, struct ext2 *fs, struct ext2d_inode *inode)
{
#define iter_int iter->_internal
	if (inode == NULL) {
		iter_int.needs_reset = false;
		iter_int.pos = 0;
		return false;
	}
	if (iter_int.needs_reset)
		return false;

	for (;;) {
		size_t len = ext2_read(fs, inode, iter_int.buf, sizeof iter_int.buf, iter_int.pos);
		struct ext2d_dirent *ent = (void*)iter_int.buf;
		/* accessing possibly uninitialized fields of ent doesn't matter here */
		iter_int.pos += ent->size;
		if (len < sizeof(*ent) + ent->namelen_upper) {
			iter_int.needs_reset = true;
			return false;
		}
		if (ent->inode > 0) {
			iter->ent = ent;
			return true;
		}
	}
#undef iter_int
}
