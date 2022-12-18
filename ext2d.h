#pragma once
#include <stdint.h>

#define EXT2D_SUPERBLOCK_MAGIC 0xef53
#define EXT2D_FEATURE_RO_DIRTYPE 2
#define EXT2D_FEATURE_RW_SPARSE_SUPER 1
#define EXT2D_FEATURE_RW_SIZE64 2

struct ext2d_superblock {
	uint32_t inodes_total;
	uint32_t blocks_total;
	uint32_t blocks_reserved;
	uint32_t blocks_free;
	uint32_t inodes_free;

	uint32_t block_first_data; /* superblock location (?), apparently can vary */
	uint32_t block_size_log; /* block_size = 1024 << block_size_log */
	uint32_t frag_size_log; /* frag_size = 1024 << frag_size_log */
	/* if not using bigalloc it's == block_size, otherwise it's bigger. */

	uint32_t blocks_per_group;
	uint32_t frags_per_group;
	uint32_t inodes_per_group;

	uint32_t last_mount;
	uint32_t last_written;

	uint16_t mounts_since_fsck;
	uint16_t mounts_since_fsck_max;

	uint16_t magic;

	uint16_t state; /* 1 - clean, 2 - has errors */
	uint16_t error_handling; /* 1 - ignore, 2 - remount as ro, 3 - kernel panic */
	uint16_t v_minor;

	uint32_t last_fsck;
	uint32_t fsck_interval;
	uint32_t creator_os;
	uint32_t v_major;

	uint16_t reserved_uid;
	uint16_t reserved_gid;

	/* extended, v_major >= 1 */
	uint32_t inode_first; /* first non-reserved */
	uint16_t inode_size;
	uint16_t blockgroup; /* that this superblock is part of */

	uint32_t features_optional;
	uint32_t features_ro; /* if all supported, can be mounted ro */
	uint32_t features_rw; /* if all supported, can be mounted rw */

	char blkid[16];
	char volname[16];
	char lastmount[64];
} __attribute__((__packed__));

struct ext2d_bgd {
	uint32_t block_bitmap;
	uint32_t inode_bitmap;
	uint32_t inode_table;
	uint16_t blocks_free;
	uint16_t inodes_free;
	uint16_t directory_amt;
	char _pad[14];
} __attribute__((__packed__));

struct ext2d_inode {
	uint16_t perms;
	uint16_t uid;
	uint32_t size_lower;
	uint32_t atime;
	uint32_t ctime;
	uint32_t mtime;
	uint32_t dtime; /* deletion time */
	uint16_t gid;
	uint16_t links; /* hard links pointing to it */
	uint32_t sectors; /* size of the already allocated blocks / 512 */
	uint32_t flags;
	uint32_t os_specific1;
	uint32_t block[12];
	uint32_t indirect_1;
	uint32_t indirect_2;
	uint32_t indirect_3;
	uint32_t generation;
	uint32_t acl;
	union {
		uint32_t size_upper, directory_acl;
	};
	uint32_t frag;
	uint32_t os_specific2;
} __attribute__((__packed__));

struct ext2d_dirent {
	uint32_t inode;
	uint16_t size;
	uint8_t namelen_lower;
	union {
		uint8_t namelen_upper, type;
	};
	char name[];
} __attribute__((__packed__));
