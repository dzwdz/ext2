/* Functions related to linking/unlinking files. */

#include "ext2.h"
#include <assert.h>
#include <string.h>

#define DIRENT_SIZE(namelen) ((sizeof(struct ext2d_dirent) + namelen + 3) & ~3)

static int nuke_blocks(struct ext2 *fs, uint32_t inode_n);
static int nuke_inode(struct ext2 *fs, uint32_t inode_n);

int
ext2_link(struct ext2 *fs, uint32_t dir_n, const char *name, uint32_t target_n, int flags)
{
	size_t len = 0;
	void *dir;
	size_t namelen = strlen(name);
	size_t entlen = DIRENT_SIZE(namelen);
	if (!fs->rw) return -1;
	if ((uint8_t)namelen != namelen) {
		return -1;
	}
	if (ext2i_change_linkcnt(fs, target_n, 1) < 0) {
		return -1;
	}
	dir = ext2_req_file(fs, dir_n, &len, 0);
	if (!dir) {
		return -1;
	}

	for (size_t pos = 0; pos + entlen < len; ) {
		struct ext2d_dirent *ent = dir + pos;
		if (ent->inode == 0 && entlen <= ent->size) {
			/* found free dirent */
			ent->inode = target_n;
			ent->namelen_lower = namelen;
			ent->type = flags & 7;
			memcpy(ent->name, name, namelen);
			if (ext2_dropreq(fs, dir, true) < 0) {
				return -1;
			} else {
				return 0;
			}
		} else if (DIRENT_SIZE(ent->namelen_lower) + namelen <= ent->size) {
			/* enough unused space to split.
			 * will get used in the next iteration */
			size_t minlen = DIRENT_SIZE(ent->namelen_lower);
			struct ext2d_dirent *next = ((void*)ent) + minlen;
			next->inode = 0;
			next->size = ent->size - minlen;
			ent->size = minlen;
		}
		pos += ent->size;
	}
	ext2_dropreq(fs, dir, false);
	return -1;
}

uint32_t
ext2_unlink(struct ext2 *fs, uint32_t dir_n, const char *name)
{
	size_t len = 0;
	void *dir;
	size_t namelen = strlen(name);
	size_t entlen = DIRENT_SIZE(namelen);
	if (!fs->rw) return -1;
	if ((uint8_t)namelen != namelen) {
		return -1;
	}
	dir = ext2_req_file(fs, dir_n, &len, 0);
	if (!dir) {
		return -1;
	}

	for (size_t pos = 0; pos + entlen < len; ) {
		struct ext2d_dirent *ent = dir + pos;
		if (ent->namelen_lower == namelen && memcmp(ent->name, name, namelen) == 0) {
			uint32_t n = ent->inode;
			// TODO merge free space
			ent->inode = 0;
			if (ext2_dropreq(fs, dir, true) < 0) {
				return -1;
			} else if (ext2i_change_linkcnt(fs, n, -1) < 0) {
				return -1;
			} else {
				return n;
			}
		}
		pos += ent->size;
	}
	ext2_dropreq(fs, dir, false);
	return -1;
}

static int
nuke_blocks(struct ext2 *fs, uint32_t inode_n)
{
	struct ext2d_inode *inode = NULL;
	struct ext2d_bgd *bgd = NULL;
	uint32_t block;
	uint32_t freed = 0;

	// TODO indirect blocks
	for (int i = 0; i < 12; i++) {
		if (!inode) {
			inode = ext2_req_inode(fs, inode_n);
			if (!inode) return -1;
		}

		block = inode->block[i];
		if (block == 0) continue;
		freed++;
		ext2_dropreq(fs, inode, false);
		inode = NULL;

		// so much boilerplate...
		uint32_t group = (block - 1) / fs->blocks_per_group;
		uint32_t idx   = (block - 1) % fs->blocks_per_group;
		uint32_t ib_addr;
		{
			bgd = ext2_req_bgdt(fs, group);
			if (!bgd) {
				return -1;
			}
			bgd->blocks_free++;
			ib_addr = bgd->block_bitmap;
			if (ext2_dropreq(fs, bgd, true) < 0) {
				return -1;
			}
			bgd = NULL;
		}
		char *ib = fs->req(fs->dev, fs->block_size, fs->block_size * ib_addr);
		if (!ib) {
			return -1;
		}
		uint32_t byte = idx / 8;
		uint8_t mask = 1 << (idx % 8);
		// TODO don't overflow
		assert(byte < fs->block_size);

		if ((ib[byte] & mask) == 0) {
			ext2_dropreq(fs, ib, false);
			assert(false); // TODO handle inconsistent filesystems well
			return -1;
		}
		ib[byte] &= ~mask;
		if (ext2_dropreq(fs, ib, true) < 0) {
			return -1;
		}
	}

	if (inode && fs->drop(fs->dev, inode, true) < 0) {
		return -1;
	}

	struct ext2d_superblock *sb = ext2_req_sb(fs);
	if (!sb) {
		return -1;
	}
	sb->blocks_free += freed;
	if (ext2_dropreq(fs, sb, true) < 0) {
		return -1;
	}

	return 0;
}

static int
nuke_inode(struct ext2 *fs, uint32_t inode_n)
{
	{
		struct ext2d_inode *inode;
		inode = ext2_req_inode(fs, inode_n);
		if (!inode) {
			return -1;
		}
		// TODO check linkcnt
		inode->dtime = fs->gettime32(fs->dev);
		if (ext2_dropreq(fs, inode, true) < 0) {
			return -1;
		}
	}

	uint32_t group = (inode_n - 1) / fs->inodes_per_group;
	uint32_t idx   = (inode_n - 1) % fs->inodes_per_group;
	uint32_t ib_addr;
	{
		struct ext2d_bgd *bgd;
		bgd = ext2_req_bgdt(fs, group);
		if (!bgd) {
			return -1;
		}
		bgd->inodes_free++;
		ib_addr = bgd->inode_bitmap;
		if (ext2_dropreq(fs, bgd, true) < 0) {
			return -1;
		}
	}
	char *ib = fs->req(fs->dev, fs->block_size, fs->block_size * ib_addr);
	if (!ib) {
		return -1;
	}
	uint32_t byte = idx / 8;
	uint8_t mask = 1 << (idx % 8);

	// TODO don't overflow
	assert(byte < fs->block_size);

	if ((ib[byte] & mask) == 0) {
		ext2_dropreq(fs, ib, false);
		assert(false); // TODO handle inconsistent filesystems well
		return -1;
	}
	ib[byte] &= ~mask;
	if (ext2_dropreq(fs, ib, true) < 0) {
		return -1;
	}

	struct ext2d_superblock *sb = ext2_req_sb(fs);
	if (!sb) {
		return -1;
	}
	sb->inodes_free++;
	if (ext2_dropreq(fs, sb, true) < 0) {
		return -1;
	}

	return 0;
}

int
ext2i_change_linkcnt(struct ext2 *fs, uint32_t inode_n, int d)
{
	if (!fs->rw) return -1;
	struct ext2d_inode *inode = ext2_req_inode(fs, inode_n);
	bool gone;
	if (!inode) {
		return -1;
	}
	// TODO check overflow
	inode->links += d;
	gone = inode->links == 0;
	if (ext2_dropreq(fs, inode, true) < 0) {
		return -1;
	}
	if (!gone) {
		return 0;
	}
	if (nuke_blocks(fs, inode_n) < 0 ||
		nuke_inode(fs, inode_n) < 0)
	{
		// TODO unlinking and nuking an inode should be two separate things
		return -1;
	}
	return 0;
}
