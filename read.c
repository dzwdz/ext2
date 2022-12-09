#include "ext2.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>

int
ext2_inodepos(struct ext2 *fs, uint32_t inode)
{
	uint32_t group = (inode - 1) / fs->super.inodes_per_group;
	uint32_t idx   = (inode - 1) % fs->super.inodes_per_group;
	if (group >= fs->groups) return -1;
	// TODO no overflow guard
	// also, sketchy return type
	return fs->block_size * fs->bgdt[group].inode_table + idx * fs->super.inode_size;
}

int
ext2_readinode(struct ext2 *fs, uint32_t inode, void *buf, size_t len)
{
	void *p;
	int off = ext2_inodepos(fs, inode);
	if (off < 0) {
		return -1;
	}
	if (len > fs->super.inode_size) {
		len = fs->super.inode_size;
	}
	p = fs->req(fs->dev, len, off);
	if (!p) {
		return -1;
	}
	memcpy(buf, p, len);
	fs->drop(fs->dev, p, false);
	return len;
}

struct ext2d_inode *
ext2_inode_req(struct ext2 *fs, uint32_t inode_n)
{
	int off = ext2_inodepos(fs, inode_n);
	if (off < 0) return NULL;
	return fs->req(fs->dev, sizeof(struct ext2d_inode), off);
}

int
ext2_inode_ondisk(struct ext2 *fs, uint32_t inode_n, size_t pos, size_t *dev_off, size_t *dev_len)
{
	// TODO unnecessary division by power of 2
	uint64_t block     = pos / fs->block_size;
	uint64_t block_off = pos % fs->block_size;
	struct ext2d_inode *inode;
	if (block >= 12) {
		// TODO indirect blocks
		return -1;
	}
	inode = ext2_inode_req(fs, inode_n);
	if (!inode) {
		return -1;
	}
	block = inode->block[block];
	fs->drop(fs->dev, inode, false);
	if (block == 0) {
		return -1;
	}
	*dev_off = block * fs->block_size + block_off;
	// TODO try to return as big of a block as possible
	*dev_len = fs->block_size - block_off;
	return 0;
}

int
ext2_read(struct ext2 *fs, uint32_t inode_n, void *buf, size_t len, size_t off)
{
	struct ext2d_inode _inode;
	if (ext2_readinode(fs, inode_n, &_inode, sizeof _inode) < 0) {
		return -1;
	}
	struct ext2d_inode *inode = &_inode;

	if (len > INT_MAX)
		len = INT_MAX;
	// TODO 64-bit size
	if (len > inode->size_lower - off)
		len = inode->size_lower - off;
	if (off >= inode->size_lower)
		return 0;

	size_t pos = 0;
	while (pos < len) {
		uint64_t dev_off, dev_len;
		void *p;
		if (ext2_inode_ondisk(fs, inode_n, off + pos, &dev_off, &dev_len) < 0) {
			return -1;
		}
		if (dev_len > len - pos) {
			dev_len = len - pos;
		}
		p = fs->req(fs->dev, dev_len, dev_off);
		if (!p) {
			return -1;
		}
		memcpy(buf + pos, p, dev_len);
		fs->drop(fs->dev, p, false);
		pos += dev_len;
	}
	return pos;
}

bool
ext2_diriter(struct ext2_diriter *iter, struct ext2 *fs, uint32_t inode_n)
{
#define iter_int iter->_internal
	if (inode_n == 0) {
		iter_int.needs_reset = false;
		iter_int.pos = 0;
		return false;
	}
	if (iter_int.needs_reset)
		return false;

	struct ext2d_inode inode;
	if (ext2_readinode(fs, inode_n, &inode, sizeof inode) < 0) {
		return false;
	}

	for (;;) {
		size_t len = ext2_read(fs, inode_n, iter_int.buf, sizeof iter_int.buf, iter_int.pos);
		struct ext2d_dirent *ent = (void*)iter_int.buf;
		/* accessing possibly uninitialized fields of ent doesn't matter here */
		iter_int.pos += ent->size;
		if (len < sizeof(*ent) + ent->namelen_lower) {
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

		uint32_t last_inode = inode_n;
		inode_n = 0;
		ext2_diriter(&iter, NULL, 0);
		while (inode_n == 0 && ext2_diriter(&iter, fs, last_inode)) {
			if (iter.ent->namelen_lower == seglen &&
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
