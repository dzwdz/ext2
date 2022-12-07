#include "ext2.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

int
ext2_writeinode(struct ext2 *fs, uint32_t inode, const struct ext2d_inode *buf)
{
	int len = sizeof(*buf);
	int off;
	if (!fs->rw) return -1;

	off = ext2_inodepos(fs, inode);
	if (off < 0) {
		return -1;
	}
	if (fs->write(fs->dev, buf, len, off) != (int)len) {
		return -1;
	}
	return len;
}

int
ext2_write(struct ext2 *fs, struct ext2d_inode *inode, const void *buf, size_t len, size_t off)
{
	uint64_t dev_off, dev_len;
	if (!fs->rw) return -1;

	/* test run, see if we can access all the space */
	for (size_t pos = 0; pos < len; ) {
		if (ext2_inode_ondisk(fs, inode, off + pos, &dev_off, &dev_len) < 0) {
			return -1;
		}
		pos += dev_len;
	}

	/* do it for real */
	for (size_t pos = 0; pos < len; ) {
		if (ext2_inode_ondisk(fs, inode, off + pos, &dev_off, &dev_len) < 0) {
			return -1;
		}
		if (dev_len > len - pos) {
			dev_len = len - pos;
		}
		if (fs->write(fs->dev, buf + pos, dev_len, dev_off) != (int)dev_len) {
			return -1;
		}
		pos += dev_len;
	}

	// TODO size64
	if (inode->size_lower < len + off)
		inode->size_lower = len + off;

	return len;
}

#define DIRENT_SIZE(namelen) ((sizeof(struct ext2d_dirent) + namelen + 3) & ~3)

int
ext2_link(struct ext2 *fs, struct ext2d_inode *dir, const char *name, uint32_t inode_n, int flags)
{
	char buf[1024];
	size_t len = ext2_read(fs, dir, buf, sizeof buf, 0);
	size_t namelen = strlen(name);
	size_t entlen = DIRENT_SIZE(namelen);

	if ((uint8_t)namelen != namelen) {
		return -1;
	}

	for (size_t pos = 0; pos + entlen < len; ) {
		struct ext2d_dirent *ent = (void*)buf + pos;
		if (ent->inode == 0 && entlen <= ent->size) {
			/* found free dirent */
			ent->inode = inode_n;
			ent->namelen_lower = namelen;
			ent->type = flags & 7;
			memcpy(ent->name, name, namelen);
			if (ext2_write(fs, dir, buf, len, 0) != (int)len) {
				return -1;
			}
			return 0;
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
	return -1;
}

uint32_t
ext2_unlink(struct ext2 *fs, struct ext2d_inode *dir, const char *name)
{
	char buf[1024];
	size_t len = ext2_read(fs, dir, buf, sizeof buf, 0);
	size_t namelen = strlen(name);
	size_t entlen = DIRENT_SIZE(namelen);

	for (size_t pos = 0; pos + entlen < len; ) {
		struct ext2d_dirent *ent = (void*)buf + pos;
		if (ent->namelen_lower == namelen && memcmp(ent->name, name, namelen) == 0) {
			uint32_t n = ent->inode;
			// TODO merge free space
			ent->inode = 0;
			if (ext2_write(fs, dir, buf, len, 0) != (int)len) {
				return -1;
			}
			return n;
		}
		pos += ent->size;
	}
	return -1;
}
