#include "ex_cache.h"
#include "ext2.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define errx(ret, ...) do { \
	fprintf(stderr, __VA_ARGS__); \
	fprintf(stderr, "\n"); \
	exit(ret); \
} while(0)

static int my_read(void *userdata, void *buf, size_t len, size_t off);
static int my_write(void *userdata, const void *buf, size_t len, size_t off);
static void tree(struct ext2 *fs, uint32_t inode_n, const char *name, bool header);
static uint32_t splitdir(struct ext2 *fs, const char *path, char **name);

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

int
main(int argc, char **argv)
{
	if (argc < 2) errx(1, "no arguments");

	int fd = open(argv[1], O_RDWR);
	if (fd < 0) errx(1, "couldn't open %s", argv[1]);

	/* Not part of the main library - just initializing the example caching impl from
	 * ex_cache. */
	struct e2device *dev = exc_init(my_read, my_write, (void*)(intptr_t)fd);
	if (!dev) errx(1, "exc_init failed");

	struct ext2 *fs = ext2_opendev(dev, exc_req, exc_drop);
	if (!fs) errx(1, "ext2_opendev failed");

	/* IO is done using "requests" - to make caching easier, instead of using
	 * a pread/pwrite-style interface, the library asks the caching impl for a
	 * pointer to the data, which is managed by the caching impl itself.
	 * Most user-facing functions do this too. */
	struct ext2d_superblock *sb = ext2_req_sb(fs);
	if (!sb) errx(1, "couldn't get superblock");
	printf("ext2 v%d.%d\n", sb->v_major, sb->v_minor);
	printf("inodes: %u/%u free\n", sb->inodes_free, sb->inodes_total);
	printf("state: %u, error handling: %u\n", sb->state, sb->error_handling);
	printf("sizes: block %lu, frag %lu\n", fs->block_size, fs->frag_size);
	printf("features: opt %x, ro %x, rw %x\n", sb->features_optional, sb->features_ro, sb->features_rw);
	printf("%u block group(s)\n", fs->groups);
	/* The library is limited to only requesting a single area at a time. However,
	 * as the user, you can have as many requests active as your caching implementation
	 * allows.
	 * The example one only allows one request at a time to help catch errors in the
	 * library. */
	ext2_dropreq(fs, sb, false);

	if (argc < 3) {
		tree(fs, 2, "", true);
	} else if (strcmp(argv[2], "write") == 0) {
		if (argc < 3) errx(1, "usage: ./example write path [count]");
		const char *path = argv[3];
		int count = argv[4] ? atoi(argv[4]) : 0;

		uint32_t n = ext2c_walk(fs, path, strlen(path));
		if (!n) {
			n = ext2_alloc_inode(fs, 0100700);
			if (n == 0) {
				errx(1, "couldn't allocate inode");
			}
			printf("allocated inode %u\n", n);

			char *name;
			uint32_t dir_n = splitdir(fs, path, &name);
			if (!dir_n) {
				errx(1, "target directory doesn't exist");
			}
			if (ext2_link(fs, dir_n, name, n, 0) < 0) {
				errx(1, "couldn't create link");
			}
		}

		{
			struct ext2d_inode *inode;
			inode = ext2_req_inode(fs, n);
			if (!inode) {
				errx(1, "couldn't read inode %u", n);
			}
			inode->size_lower = 0;
			if (ext2_dropreq(fs, inode, true) < 0) {
				errx(1, "couldn't save inode %u", n);
			}
		}

		for (int i = 0; i < count - 1; i++) {
			const char *s = "I can eat glass and it doesn't hurt me.\n";
			if (ext2_write(fs, n, s, strlen(s), i * strlen(s)) <= 0) {
				errx(1, "write error");
			}
		}
	} else if (strcmp(argv[2], "link") == 0) {
		if (argc < 4) errx(1, "usage: ./example link src target");
		const char *src = argv[3];
		const char *target = argv[4];
		uint32_t src_n, target_n;
		char *name;

		target_n = splitdir(fs, target, &name);
		if (!target_n) {
			errx(1, "target directory doesn't exist\n");
		}

		src_n = ext2c_walk(fs, src, strlen(src));
		if (!src_n) {
			errx(1, "no such file\n");
		}

		if (ext2_link(fs, target_n, name, src_n, 0) < 0) {
			errx(1, "couldn't create link\n");
		}
	} else if (strcmp(argv[2], "unlink") == 0) {
		const char *path = argv[3];
		uint32_t dir_n;
		char *name;
		dir_n = splitdir(fs, path, &name);
		if (!dir_n) {
			errx(1, "directory doesn't exist\n");
		}
		if (ext2_unlink(fs, dir_n, name) == 0) {
			errx(1, "deletion failed\n");
		}
	} else if (strcmp(argv[2], "tree") == 0) {
		const char *path = argv[3] ? argv[3] : "/";
		uint32_t n = ext2c_walk(fs, path, strlen(path));
		if (!n) {
			errx(1, "%s not found\n", path);
		}
		tree(fs, n, path, true);
	} else {
		errx(1, "unknown command '%s'", argv[2]);
	}
	exc_free(dev);
}

static void
tree(struct ext2 *fs, uint32_t inode_n, const char *name, bool header)
{
	struct ext2d_inode *inode;
	int type;
	inode = ext2_req_inode(fs, inode_n);
	if (!inode) {
		fprintf(stderr, "couldn't read inode %u\n", inode_n);
		return;
	}
	if (header) {
		printf("inode  perms size    name\n");
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
			tree(fs, iter.ent->inode, namebuf, false);
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
