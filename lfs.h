/*
 * lfs.h
 *
 *  Created on: May 13, 2016
 *      Author: christine
 */

#ifndef LFS_H_
#define LFS_H_
#include <fuse.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

/*Magic numbers*/
#define BLOCKS_PR_SEGMENT 20
#define SEGMENTS_PR_LOG 20
#define INODES_PR_LOG 100
#define CLEANING_THRESHOLD 10
#define BLOCK_SIZE 2000
#define BLOCKS_PR_INODE 10
#define FILE_NAME_LENGTH_MAX 40
#define BLOCK_TYPE_EMPTY 0
#define BLOCK_TYPE_ITBL 1
#define BLOCK_TYPE_INODE 2

int lfs_getattr(const char *, struct stat *);
int lfs_open(const char *, struct fuse_file_info *);
ssize_t lfs_read(int fd, void *buf, size_t count);
int lfs_readdir(const char *, void *, fuse_fill_dir_t, off_t,
		struct fuse_file_info *);
int lfs_release(const char *path, struct fuse_file_info *fi);
int lfs_create(const char *path, mode_t mode, struct fuse_file_info *fi);
int lfs_mkdir(const char *path, mode_t mode);
int lfs_unlink(const char *path);
int lfs_rmdir(const char *path);
int lfs_truncate(const char *path, off_t length);
ssize_t lfs_write(int fd, const void *buf, size_t count);

/* Structs*/
static struct fuse_operations lfs_oper = { .getattr = lfs_getattr, .readdir =
		lfs_readdir, .mknod = NULL, .mkdir = lfs_mkdir, .unlink = lfs_unlink,
		.rmdir = lfs_rmdir, .truncate = lfs_truncate, .open = lfs_open, .read =
				lfs_read, .release = lfs_release, .write = lfs_write, .create =
				lfs_create, .rename = NULL, //Maybe
		.utime = NULL //Maybe
		};

struct inode {
	int inode_number;
	int parent_inode_number;
	int is_dir;
	/* If the inode represents a directory, it
	 * has the inode numbers of it's children
	 * as the content of the first block.
	 */
	char file_name[FILE_NAME_LENGTH_MAX];
	int number_of_blocks;
	int file_size;
	int number_of_children;
	int block_placements[BLOCKS_PR_INODE];
	int blocks_changed[BLOCKS_PR_INODE];
	struct timespec* last_access;
	struct timespec* last_modified;
};

struct file_system {
	char* log_file_name;
	unsigned int* buffer;
	char buffer_summary[BLOCKS_PR_SEGMENT];
	int number_of_inodes;
	int next_segment;
	int used_segments;
	int oldest_segment;
};

struct file_system* log_system;
struct fuse_file_info* open_file;

/*Derived magic numbers*/
#define SEGMENT_SIZE (BLOCK_SIZE*BLOCKS_PR_SEGMENT + BLOCKS_PR_SEGMENT)
#define INODE_SIZE BLOCK_SIZE
#define INODE_NUMBERS_MIN BLOCK_TYPE_INODE+1
#define MAX_CHILDREN BLOCKS_PR_INODE

int log_clean(struct file_system* lfs);
int buff_write_inode_with_changes(struct file_system* lfs,
		struct inode* inode_ptr, unsigned int* data);
int copy_one_block(void* from, void* to,
		unsigned int from_start_index, unsigned int to_start_index);
int read_block(struct file_system* lfs, unsigned int* read_into, unsigned int address);
int log_write_buffer(struct file_system* lfs);
int buff_assure_space(struct file_system* lfs, int blocks_to_add);
int traverse_path(struct file_system* lfs, const char* path,
		struct inode* parent, struct inode* file, char* new_path);
int read_inode(struct file_system* lfs, int inode_number,
		struct inode* inode_ptr);
int read_inode_table(struct file_system* lfs, unsigned int* put_table_here);
int get_root_inode(struct file_system* lfs, struct inode* root);
int add_child_to_dir(struct file_system* lfs, struct inode* parent,
		struct inode* child);
int node_trunc(struct file_system* lfs, struct inode* node, unsigned int length);
int get_inode_from_path(struct file_system* lfs, const char *path,
		struct inode* inode);
int get_filename(const char* path, char* file_name);
int fill_block_with_zero(unsigned int* array, unsigned int start_index);
int update_inode_table(struct file_system* lfs, int inode_number,
		int new_address);
int buff_first_free(struct file_system* lfs);
int block_start_in_segment(int block_no);
int init_inode_table(struct file_system* lfs);
int complete_address(struct file_system* lfs, unsigned int addr_in_buff);

#endif /* LFS_H_ */
