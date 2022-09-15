#include "ext2.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct e2device { int fd; };

static int
e2b_read(struct e2device *dev, void *buf, size_t len, size_t off)
{
	return pread(dev->fd, buf, len, off);
}

#define TREE_HEADER "inode  perms size    name\n"

static void
tree(struct ext2 *fs, uint32_t inode_n, const char *name)
{
	struct ext2d_inode inode;
	if (ext2_readinode(fs, inode_n, &inode, sizeof inode) < 0) {
		fprintf(stderr, "couldn't read inode %u\n", inode_n);
		return;
	}
	printf("%5u %6.3o %6u  %s\n", inode_n, inode.perms, inode.size_lower, *name ? name : "/");
	int type = (inode.perms >> 12) & 0xF;
	if (type == 0x4) {
		size_t mynamelen = strlen(name) + 1;
		char *namebuf = malloc(mynamelen + 256 + 1);
		char *suffix = namebuf + mynamelen;
		memcpy(namebuf, name, mynamelen);
		namebuf[mynamelen - 1] = '/';
		for (struct ext2_diriter iter = {0}; ext2_diriter(&iter, fs, &inode); ) {
			memcpy(suffix, iter.ent->name, iter.ent->namelen_upper);
			suffix[iter.ent->namelen_upper] = '\0';
			if (strcmp(suffix, ".") == 0 || strcmp(suffix, "..") == 0) continue;
			tree(fs, iter.ent->inode, namebuf);
		}
		free(namebuf);
	} else if (type == 0x8) {
		char buf[512];
		int len = ext2_read(fs, &inode, buf, sizeof buf, 0);
		printf("%.*s", len, buf);
	} else {
		printf("(unknown type)\n");
	}
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

	printf(TREE_HEADER);
	if (argc < 3) {
		tree(fs, 2, "");
	}
	for (int arg = 2; arg < argc; arg++) {
		const char *path = argv[arg];
		uint32_t n = ext2c_walk(fs, path, strlen(path));
		if (!n) {
			printf("%s not found\n", path);
			continue;
		}
		tree(fs, n, path);
	}
}
