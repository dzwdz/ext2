#include "ex_cache.h"
#include "ext2.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int
my_read(void *userdata, void *buf, size_t len, size_t off)
{
	if (pread((int)(intptr_t)userdata, buf, len, off) == (ssize_t)len) {
		return 0;
	} else {
		return 1;
	}
}

static int
my_write(void *userdata, const void *buf, size_t len, size_t off)
{
	if (pwrite((int)(intptr_t)userdata, buf, len, off) == (ssize_t)len) {
		return 0;
	} else {
		return 1;
	}
}

#define TREE_HEADER "inode  perms size    name\n"

static void
tree(struct ext2 *fs, uint32_t inode_n, const char *name)
{
	struct ext2d_inode *inode;
	int type;
	inode = ext2_req_inode(fs, inode_n);
	if (!inode) {
		fprintf(stderr, "couldn't read inode %u\n", inode_n);
		return;
	}
	printf("%5u %6.3o %6u  %s\n", inode_n, inode->perms, inode->size_lower, *name ? name : "/");
	type = (inode->perms >> 12) & 0xF;
	ext2_dropreq(fs, inode, false);
	inode = NULL;

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

int
main(int argc, char **argv)
{
	(void)argc;
	int fd = open(argv[1], O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "couldn't open %s, quitting\n", argv[1]);
		exit(1);
	}
	struct e2device *dev = exc_init(my_read, my_write, (void*)(intptr_t)fd);
	if (!dev) {
		fprintf(stderr, "exc_init failed\n");
		exit(1);
	}

	/* using the cache implementation from ex_cache.c */
	struct ext2 *fs = ext2_opendev(dev, exc_req, exc_drop);
	if (!fs) {
		fprintf(stderr, "ext2_opendev failed\n");
		exit(1);
	}

	struct ext2d_superblock *sb = ext2_req_sb(fs);
	if (!sb) {
		fprintf(stderr, "couldn't get superblock\n");
		exit(1);
	}
	printf("ext2 v%d.%d\n", sb->v_major, sb->v_minor);
	printf("inodes: %u/%u free\n", sb->inodes_free, sb->inodes_total);
	printf("state: %u, error handling: %u\n", sb->state, sb->error_handling);
	printf("sizes: block %lu, frag %lu\n", fs->block_size, fs->frag_size);
	printf("features: opt %x, ro %x, rw %x\n", sb->features_optional, sb->features_ro, sb->features_rw);
	printf("%u block group(s)\n", fs->groups);
	ext2_dropreq(fs, sb, false);

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

			{
				struct ext2d_inode *inode;
				inode = ext2_req_inode(fs, n);
				if (!inode) {
					fprintf(stderr, "couldn't read inode %u\n", n);
					continue;
				}
				inode->size_lower = 0;
				if (ext2_dropreq(fs, inode, true) < 0) {
					fprintf(stderr, "couldn't save inode %u\n", n);
					// TODO mark fs as dirty
					continue;
				}
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

			if (ext2_link(fs, target_n, name, src_n, 0) < 0) {
				fprintf(stderr, "couldn't create link\n");
			} else {
				tree(fs, target_n, target);
			}
		} else if (strchr(path, '-')) { /* unlink */
			uint32_t dir_n;
			char *name;
			dir_n = splitdir(fs, path + 1, &name);
			if (!dir_n) {
				fprintf(stderr, "directory doesn't exist\n");
				continue;
			}
			if (ext2_unlink(fs, dir_n, name) == 0) {
				fprintf(stderr, "deletion failed\n");
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
	exc_free(dev);
}
