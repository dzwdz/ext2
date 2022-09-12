#include "ext2.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

struct e2device { int fd; };

static int
e2b_read(struct e2device *dev, void *buf, size_t len, size_t off)
{
	return pread(dev->fd, buf, len, off);
}

int
main(int argc, char **argv)
{
	(void)argc;
	struct e2device dev;
	dev.fd = open(argv[1], O_RDWR);
	if (dev.fd < 0) {
		fprintf(stderr, "couldn't open %s, quitting\n", argv[1]);
		exit(1);
	}

	struct ext2 *fs = ext2_opendev(&dev, e2b_read);
	if (!fs) {
		fprintf(stderr, "ext2_opendev failed\n");
		exit(1);
	}

	printf("ext2 v%d.%d\n", fs->super.v_major, fs->super.v_minor);
	printf("inodes: %u/%u free\n", fs->super.inodes_free, fs->super.inodes_total);
	printf("state: %u, error handling: %u\n", fs->super.state, fs->super.error_handling);
	printf("sizes: block %lu, frag %lu\n", fs->block_size, fs->frag_size);
	printf("features: opt %x, ro %x, rw %x\n", fs->super.features_optional, fs->super.features_ro, fs->super.features_rw);
	printf("%u block group(s)\n", fs->groups);

	for (unsigned i = 0; i < fs->groups; i++) {
		printf("block group %u: bitmaps %u %u, inode table %u, free %u %u, dirs %u\n",
			i,
			fs->bgdt[i].block_bitmap, fs->bgdt[i].inode_bitmap, fs->bgdt[i].inode_table,
			fs->bgdt[i].blocks_free, fs->bgdt[i].inodes_free, fs->bgdt[i].directory_amt);
	}

	struct ext2d_inode root;
	if (ext2_readinode(fs, 2, &root, sizeof root) < 0) {
		fprintf(stderr, "couldn't read root inode\n");
		exit(1);
	}
	printf("root perms: %05o\n", root.perms);

	for (struct ext2_diriter iter = {0}; ext2_diriter(&iter, fs, &root); ) {
		printf("/%.*s\n", iter.ent->namelen_upper, iter.ent->name);
	}
}
