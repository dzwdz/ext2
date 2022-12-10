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
	if (*len > og_len)
		*len = og_len;
	return fs->req(fs->dev, *len, dev_off);
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
	inode = ext2_req_inode(fs, inode_n);
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
	size_t pos = 0;
	while (pos < len) {
		size_t part_len = len - pos;
		void *p = ext2_req_file(fs, inode_n, &part_len, off + pos);
		if (!p) {
			break;
		}
		memcpy(buf + pos, p, part_len);
		ext2_dropreq(fs, p, false);
		pos += part_len;
	}
	return pos;
}

bool
ext2_diriter(struct ext2_diriter *iter, struct ext2 *fs, uint32_t inode_n)
{
#define iter_int iter->_internal
	struct ext2d_dirent *ent = NULL;
	size_t len;
	if (inode_n == 0) {
		iter_int.needs_reset = false;
		iter_int.pos = 0;
		return false;
	}
	if (iter_int.needs_reset)
		return false;

	for (;;) {
		len = sizeof(*ent) + 256;
		ent = ext2_req_file(fs, inode_n, &len, iter_int.pos);
		if (!ent || len < sizeof(*ent)) {
			break;
		}
		iter_int.pos += ent->size;
		if (len < sizeof(*ent) + ent->namelen_lower) {
			break;
		}
		if (ent->inode > 0) {
			memcpy(iter_int.buf, ent, sizeof(*ent) + ent->namelen_lower);
			iter->ent = (void*)iter_int.buf;
			ext2_dropreq(fs, ent, false);
			return true;
		}
		ext2_dropreq(fs, ent, false);
		ent = NULL;
	}

	if (ent) {
		ext2_dropreq(fs, ent, false);
	}
	iter_int.needs_reset = true;
	return false;
#undef iter_int
}

uint32_t
ext2c_walk(struct ext2 *fs, const char *path, size_t plen)
{
	if (plen < 1 || path[0] != '/') return 0;
	path++; plen--;

	struct ext2_diriter iter;
	uint32_t inode_n = 2;

	while (plen) {
		char *slash = memchr(path, '/', plen);
		size_t seglen = slash ? (size_t)(slash - path) : plen;

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
