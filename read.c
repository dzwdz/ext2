#include "ext2.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>

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

uint32_t
ext2c_walk(struct ext2 *fs, const char *path, size_t plen)
{
	if (plen < 1 || path[0] != '/') return 0;
	path++; plen--;

	struct ext2d_inode inode;
	struct ext2_diriter iter;
	uint32_t inode_n = 2;

	while (plen) {
		char *slash = memchr(path, '/', plen);
		size_t seglen = slash ? (size_t)(slash - path) : plen;

		// TODO allow the user to supply caching functions
		if (ext2_readinode(fs, inode_n, &inode, sizeof inode) < 0)
			return 0;

		inode_n = 0;
		ext2_diriter(&iter, NULL, 0);
		while (inode_n == 0 && ext2_diriter(&iter, fs, &inode)) {
			if (iter.ent->namelen_upper == seglen &&
				memcmp(iter.ent->name, path, seglen) == 0)
			{
				inode_n = iter.ent->inode;
			}
		}
		if (inode_n == 0)
			return 0;
		if (seglen < plen) seglen++;
		path += seglen;
		plen -= seglen;
	}
	return inode_n;
}
