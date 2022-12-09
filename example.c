#include "ext2.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct e2device {
	int fd;
	bool busy;
	size_t lastlen;
	size_t lastoff;
};

static void *
e2b_req(struct e2device *dev, size_t len, size_t off)
{
	void *p = malloc(len);
	if (!p) return NULL;
	assert(!dev->busy);
	dev->busy = true;
	dev->lastlen = len;
	dev->lastoff = off;

	if (pread(dev->fd, p, len, off) < (ssize_t)len) {
		free(p);
		dev->busy = false;
		return NULL;
	}
	return p;
}

static int
e2b_drop(struct e2device *dev, void *ptr, bool dirty)
{
	assert(dev->busy);
	dev->busy = false;
	if (dirty) {
		if (pwrite(dev->fd, ptr, dev->lastlen, dev->lastoff) < (ssize_t)dev->lastlen) {
			fprintf(stderr, "e2b_drop: incomplete write\n");
			free(ptr);
			return -1;
		}
	}
	free(ptr);
	return 0;
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
		for (struct ext2_diriter iter = {0}; ext2_diriter(&iter, fs, inode_n); ) {
			memcpy(suffix, iter.ent->name, iter.ent->namelen_lower);
			suffix[iter.ent->namelen_lower] = '\0';
			if (strcmp(suffix, ".") == 0 || strcmp(suffix, "..") == 0) continue;
			tree(fs, iter.ent->inode, namebuf);
		}
		free(namebuf);
	} else if (type == 0x8) {
		char buf[512];
		int len = ext2_read(fs, inode_n, buf, sizeof buf, 0);
		printf("%.*s", len, buf);
	} else {
		printf("(unknown type)\n");
	}
}

static uint32_t
splitdir(struct ext2 *fs, const char *path, char **name)
{
	const char *lastslash = strrchr(path, '/');
	if (!lastslash) {
		return 0;
	}
	uint32_t n = ext2c_walk(fs, path, lastslash - path);
	*name = (void*)lastslash + 1;
	return n;
}

static void
e_link(struct ext2 *fs, uint32_t dir_n, uint32_t file_n, const char *name)
{
	struct ext2d_inode file;
	if (ext2_readinode(fs, file_n, &file, sizeof file) < 0) {
		fprintf(stderr, "couldn't read inode %u\n", file_n);
		return;
	}
	file.links++;
	if (ext2_writeinode(fs, file_n, &file) < 0) {
		fprintf(stderr, "couldn't save inode %u\n", file_n);
		return;
	}
	if (ext2_link(fs, dir_n, name, file_n, 0) < 0) {
		fprintf(stderr, "couldn't create link\n");
		return;
	}
}

int
main(int argc, char **argv)
{
	(void)argc;
	struct e2device dev;
	dev.busy = false;
	dev.fd = open(argv[1], O_RDWR);
	if (dev.fd < 0) {
		fprintf(stderr, "couldn't open %s, quitting\n", argv[1]);
		exit(1);
	}

	struct ext2 *fs = ext2_opendev(&dev, e2b_req, e2b_drop);
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
		uint32_t n;
		if (path[0] == '+') { /* write some stuff to the file */
			int count = 0;
			for (; *path == '+'; path++) count++;
			n = ext2c_walk(fs, path, strlen(path));
			if (!n) {
				printf("creating files not implemented yet\n");
				continue;
			}

			struct ext2d_inode inode;
			if (ext2_readinode(fs, n, &inode, sizeof inode) < 0) {
				fprintf(stderr, "couldn't read inode %u\n", n);
				continue;
			}
			inode.size_lower = 0;
			if (ext2_writeinode(fs, n, &inode) < 0) {
				fprintf(stderr, "couldn't save inode %u\n", n);
				// TODO mark fs as dirty
			}
			for (int i = 0; i < count; i++) {
				const char *s = "I can eat glass and it doesn't hurt me.\n";
				if (ext2_write(fs, n, s, strlen(s), i * strlen(s)) <= 0) {
					fprintf(stderr, "write error :(\n");
					break;
				}
			}
			tree(fs, n, path);
		} else if (strchr(path, ':')) { /* source:linktarget */
			uint32_t src_n, target_n;
			char *target, *name;
			target = strchr(path, ':') + 1;
			target[-1] = '\0';

			target_n = splitdir(fs, target, &name);
			if (!target_n) {
				fprintf(stderr, "target directory doesn't exist\n");
				continue;
			}

			src_n = ext2c_walk(fs, path, strlen(path));
			if (!src_n) {
				fprintf(stderr, "no such file\n");
				continue;
			}

			e_link(fs, target_n, src_n, name);
			tree(fs, target_n, target);
		} else if (strchr(path, '-')) { /* unlink */
			struct ext2d_inode inode;
			uint32_t dir_n, target_n;
			char *name;
			dir_n = splitdir(fs, path + 1, &name);
			if (!dir_n) {
				fprintf(stderr, "directory doesn't exist\n");
				continue;
			}
			target_n = ext2_unlink(fs, dir_n, name);
			if (!target_n) {
				fprintf(stderr, "deletion failed\n");
				continue;
			}
			if (ext2_readinode(fs, target_n, &inode, sizeof inode) < 0) {
				fprintf(stderr, "couldn't read inode\n");
				continue;
			}
			// TODO fix BGD
			inode.links--;
			if (ext2_writeinode(fs, target_n, &inode) < 0) {
				fprintf(stderr, "couldn't write inode\n");
				continue;
			}
		} else { /* default: show tree at path */
			n = ext2c_walk(fs, path, strlen(path));
			if (!n) {
				printf("%s not found\n", path);
				continue;
			}
			tree(fs, n, path);
		}
	}
}
