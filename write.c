#include "ext2.h"

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
