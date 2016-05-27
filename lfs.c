#include "lfs.h"


/* General TODO:
* -cleanup
* -error handling
* -memsetting
* -keep file size updated
*/

/** Get file attributes.
*
* Gets file attributes and saves them in the stbuff
* struct.
*/
int lfs_getattr(const char *path, struct stat *stbuf) {
	int res = 0;
	struct inode* node;
	printf("getattr: (path=%s)\n", path);

	memset(stbuf, 0, sizeof(struct stat));
	if(strncmp(path, "/.Trash", 7) == 0) {
		//TODO what to do here?
		return 0;
	}
	node = malloc(inode_size);
	if (!node) {
		perror("lfs_getattr, malloc");
		res = -ENOMEM;
	} else {
		if (strcmp(path, "/") == 0) {
			res = get_root_inode(log_system, node);
			if(!res) {
				stbuf->st_mode = S_IFDIR | 0755;
				stbuf->st_nlink = 2 + node->number_of_children;
			}
		} else {
			res = get_inode_from_path(log_system, path, node);
			if (res) {
				perror("lfs_getattr, get_inode_from_path");
				res = -ENOENT;
			} else {
				if (node->is_dir) {
					stbuf->st_mode = S_IFDIR | 0755;
				} else {
					stbuf->st_mode = S_IFREG | 0777;
				}
				stbuf->st_nlink = 1;
			}
		}
		if (!res) {
			stbuf->st_size = node->file_size;
			stbuf->st_ino = node->inode_number;
			stbuf->st_blksize = BLOCK_SIZE;
			stbuf->st_atim = node->last_access;
			stbuf->st_mtim = node->last_modified;
			stbuf->st_ctim = node->last_modified;
		}
	}
	free(node);
	return res;
}

int lfs_create(const char *path, mode_t mode,struct fuse_file_info *fi) {
	printf("lfs_create");
	return 0; //TODO
}

/** Reads a directory */
int lfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
	//TODO
	(void) offset;
	(void) fi;
	struct inode* node;
	int res = 0;
	printf("readdir: (path=%s)\n", path);

	if (strcmp(path, "/") == 0) {
		res = get_root_inode(log_system, node);
	} else {
		res = get_inode_from_path(log_system, path, node);
	}
	if (res) {
		return -ENOENT;
	} else {
		/*TODO ????*/
	}
	return res;
}

int lfs_open(const char *path, struct fuse_file_info *fi) {
	int res = 0;
	struct inode* node;
	printf("open: (path=%s)\n", path);
	res = get_inode_from_path(log_system, path, node);
	if (res) {
		perror("lfs_open");
	} else {
		fi->fh = node->inode_number;
		open_file = fi;
		//TODO flags and stuff
	}
	return 0;
}

int lfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
	int res, block_no, left_to_read = 0;
	struct inode* node;
	char* block_buff;

	printf("read: (path=%s)\n", path);

	node = malloc(BLOCK_SIZE);
	if(!node) {
		res = -ENOMEM;
	} else {
		block_buff = malloc(BLOCK_SIZE);
		if(!block_buff) {
			res = -ENOMEM;
		} else {
			left_to_read = size;
			res = get_inode_from_path(log_system, path, node);
			if (!res) {
				while (offset >= BLOCK_SIZE) {
					block_no++;
					offset -= BLOCK_SIZE;
				}
				int blocks_read = 0;
				int start = offset;
				while ((left_to_read > 0) && !res) {
					res = read_block(log_system, block_buff, node->block_placements[block_no]);
					if (!res) {
						if (left_to_read < BLOCK_SIZE) {
							memcpy(buf[blocks_read * BLOCK_SIZE], block_buff[start], left_to_read);
							left_to_read = 0;
						} else {
							res = copy_one_block(block_buff, buf, start, blocks_read * BLOCK_SIZE);
							if (!res) {
								blocks_read++;
								left_to_read -= (BLOCK_SIZE - start);
								start = 0;
							}
						}
					}
				}
			}
			free(block_buff);
		}
		free(node);
	}
	return res;
}


int lfs_release(const char *path, struct fuse_file_info *fi) {
	printf("release: (path=%s)\n", path); //TODO
	return 0;
}

int lfs_mkdir(const char *filename, mode_t mode) {
	int res = 0;
	struct inode* node;
	struct inode* parent;

	printf("%s\n", "lfs_mkdir");
	node = malloc(inode_size);
	parent = malloc(inode_size);
	if (!node) {
		res = -ENOMEM;
	} else if (!parent) {
		res = -ENOMEM;
		free(node);
	} else {
		res = read_inode(log_system, open_file->fh, parent);
		if (res) {
			res = get_root_inode(log_system, parent);
			if (res) {
				perror("mkdir");
			}
		} else {
			if (parent->number_of_children >= BLOCKS_PR_INODE - 1) {
				res = -ENOMEM;
			} else {
				node->inode_number = log_system->number_of_inodes;
				memcpy(node->file_name, filename, strlen(filename));
				node->is_dir = 1;
				node->number_of_children = 0;
				node->number_of_blocks = 1;
				node->parent_inode_number = parent->inode_number;
				res = add_child_to_dir(log_system, parent, node);
				if (res) {
					perror("mkdir");
				} else {
					log_system->number_of_inodes++;
				}
			}
		}
		free(node);
		free(parent);
	}
	return res;
}

int lfs_unlink(const char *filename) {
	int res = 0;
	struct inode* node;
	struct inode* parent;
	res = get_inode_from_path(log_system, filename, node);
	if (res) {
		perror("unlink");
	} else {
		if (node->is_dir) {
			res = -EBADF;
		} else {
			res = read_inode(log_system, node->parent_inode_number, parent);
			if (res) {
				perror("unlink");
			} else {
				int i, found = 0;
				char* block;
				res = read_block(log_system, block, parent->block_placements[0]);
				if (res) {
					perror("unlink");
				} else {
					for (i = 0; i < parent->number_of_children; i++) {
						if (found) {
							block[i - 1] = block[i];
						} else if (block[i] == node->inode_number) {
							found = 1;
						}
					}
					if (found) {
						block[parent->number_of_children] = 0;
						parent->number_of_children--;
						res = buff_write_inode_with_changes(log_system, parent, block);
						if (res) {
							perror("unlink");
						} else {
							char* inode_table;
							res = read_inode_table(log_system, inode_table);
							if (res) {
								perror("unlink");
							} else {
								inode_table[node->inode_number] = 0;
								i = 0;
								while (log_system->buffer_summary[i] > 0) {
									i++;
								}
								copy_one_block(inode_table, log_system->buffer, 0, i * BLOCK_SIZE + BLOCKS_PR_SEGMENT + log_system->next_segment * segment_size);
								log_system->number_of_inodes--;
							}
						}
					}
				}
			}
		}
	}
	return res;
}

int lfs_rmdir(const char *path) {
	int res = 0;
	int i;
	struct inode* node;
	struct inode* parent;
	char* block;

	node = malloc(inode_size);
	if(!node) {
		res = -ENOMEM;
	} else {
		res = get_inode_from_path(log_system, path, node);
		if (!res) {
			if (node->is_dir) {
				res = -ENOTDIR;
			} else {
				parent  = malloc(inode_size);
				if (!parent) {
					res = -ENOMEM;
				} else {
					res = read_inode(log_system, node->parent_inode_number, parent);
					if (!res) {
						if(node->number_of_children > 0) {
							res = -ENOTEMPTY;
						} else {
							block = malloc(BLOCK_SIZE);
							if (!block) {
								res = -ENOMEM;
							} else {
								res = read_block(log_system, block, parent->block_placements[0]);
								if(!res) {
									int found = 0;
									for(i = 0; i <= parent->number_of_children; i++) {
										if (found) {
											block[i-1] = block[i];
										} else if (block[i] == node->inode_number) {
											found = 1;
										}
									}
									parent->number_of_children--;
									parent->blocks_changed[0] = 1;
									res = update_inode_table(log_system, node->inode_number, 0);
									if (!res) {
										log_system->number_of_inodes--;
										res = buff_write_inode_with_changes(log_system, parent, block);
									}
								}
								free(block);
							}
						}
					}
				}
				free(parent);
			}
		}
	}
	free(node);
	return res;
}

int lfs_truncate(const char *path, off_t length) {
	int res = 0;
	struct inode* node;
	node = malloc(BLOCK_SIZE);
	if (!node) {
		res = -ENOMEM;
	} else {
		res = get_inode_from_path(log_system, path, node);
		if (!res) {
			res = node_trunc(log_system, node, length);
		}
		free (node);
	}
	return res;
}

ssize_t lfs_write(int filedes, const void *buf, size_t count) {
	int err;
	struct inode* node;
	char* block = malloc(BLOCK_SIZE);
	if (!block) {
		return -ENOMEM;
	}
	err = read_inode(log_system, filedes, node);
	if (err < 0) {
		return err;
	}
	if (node->file_size % BLOCK_SIZE == 0) {
		/*All blocks started on are filled*/
		int write = count;
		if (node->number_of_blocks >= BLOCKS_PR_INODE) {
			return -ENOMEM;
		}
		if (count > BLOCK_SIZE) {
			write = BLOCK_SIZE;
		}
		memcpy(block, buf, write);
		node->blocks_changed[node->number_of_blocks] = 1;
		node->number_of_blocks++;
		node->file_size += write;
		err = buff_write_inode_with_changes(log_system, node, block);
		if (err < 0) {
			return err;
		}
		return write;
	}
	int start = (node->file_size % BLOCK_SIZE);
	int write = BLOCK_SIZE - start;
	if (write > count) {
		write = count;
	}
	err = read_block(log_system, block, node->block_placements[node->number_of_blocks - 1]);
	node->blocks_changed[node->number_of_blocks - 1] = 1;
	if (err < 0) {
		return err;
	} else if (err > 0) {
		return -1;
	}
	memcpy(&block[start], buf, write);
	node->file_size += write;
	err = buff_write_inode_with_changes(log_system, node, block);
	if (err < 0) {
		return err;
	} else if (err > 0) {
		return -1;
	}
	return write;
}

int main(int argc, char *argv[]) {
	struct file_system* my_file_system;
	struct inode* root;
	char* block;
	int res = 0;

	printf("%s\n", "lfs_main");

	my_file_system = malloc(sizeof(struct file_system));
	if (!my_file_system) {
		perror("main malloc");
	} else {
		printf("%s\n", "lfs_main: 1");
		my_file_system->log_file_name = "semihugefile.file";
		if (!my_file_system->log_file_name) {
			perror("Filename not allocated/set");
		} else {
			printf("%s\n", "lfs_main: 2");
			root = malloc(inode_size);
			if (!root) {
				perror("main malloc");
			} else {
				printf("%s\n", "lfs_main: 3");
				log_system = my_file_system;
				log_system->buffer = malloc(segment_size);
				if(!log_system->buffer) {
					perror("main malloc");
				} else {
					memset(log_system->buffer_summary, 0, BLOCKS_PR_SEGMENT);
					memset(log_system->buffer, 0, segment_size);
					init_inode_table(log_system);
					printf("%s\n", "lfs_main: 4");
					open_file = malloc(sizeof(struct fuse_file_info));
					if (!open_file) {
						perror("main malloc");
					} else {
						printf("%s\n", "lfs_main: 5");
						block = malloc(BLOCK_SIZE);
						if(!block) {
							perror("main malloc");
						} else {
							printf("%s\n", "lfs_main: 6");
							log_system->next_segment = 0;
							log_system->used_segments = 0;
							memset(root->file_name, 0, FILE_NAME_LENGTH_MAX);
							memcpy(root->file_name, "/", sizeof("/"));
							root->inode_number = INODE_NUMBERS_MIN;
							res = fill_block_with_zero(block, 0);
							if (!res) {
								printf("%s\n", "lfs_main: 7");
								block[root->inode_number] = 0 + BLOCKS_PR_SEGMENT;
								memcpy(log_system->buffer, block, BLOCK_SIZE);
								log_system->buffer_summary[0] = 1;
								root->parent_inode_number = root->inode_number;
								root->is_dir = 1;
								res = fill_block_with_zero(block, 0);
								if(!res) {
									printf("%s\n", "lfs_main: 8");
									block[0] = root->inode_number;
									root->blocks_changed[0] = 1;
									res = buff_write_inode_with_changes(log_system, root, block);
									if (!res) {
										printf("%s\n", "lfs_main: 9");
										log_system->number_of_inodes = 1;
									}
								}
							}
							//free(block);
						}
						//free(open_file);
					}
					//free(log_system->buffer);
				}
				//free(root);
			}
		}
		//free(my_file_system);
	}
	printf("main: log_system= %p\n", log_system);
	printf("main: buffer= %p\n", log_system->buffer);
	printf("main: log file name = %s\n", log_system->log_file_name);
	printf("%s\n", "main: 10");
	res = read_inode_table(log_system, block);
	printf("main: inode_table = %s\n", block);
	if (res) {
		printf("main fail: %d\n", res);
		//TODO error handling
	}
	return fuse_main(argc, argv, &lfs_oper);
}



/**
* Helper function. Get folder or file at start of path.
*/
int get_dir(struct file_system* lfs, const char* path, struct inode* parent, struct inode* file, char* new_path) {
	int i, j, res, found = 0;
	char* block;
	char* name = malloc(FILE_NAME_LENGTH_MAX);
	struct inode* node;
	if(!name) {
		return -ENOMEM;
	}
	if (path[0] == '/') {
		i++;
	}
	while (path[i] != '/') {
		name[i] = path[i];
		i++;
	}
	j = i;
	while (path[i] != EOF) {
		new_path[i-j] = path[i];
		i++;
	}
	block = malloc(BLOCK_SIZE);
	if (!block) {
		free(name);
		return -ENOMEM;
	}
	res = read_block(lfs, block, parent->block_placements[0]);
	if (res) {
		free(block);
		free(name);
		return res;
	}
	i = 0;
	while (!found && i < BLOCK_SIZE) {
		res = read_inode(lfs, block[i], node);
		if (res) {
			free(block);
			free(name);
			free(node);
			return res;
		}
		if (strcmp(node->file_name, name) == 0) {
			found = 1;
		}
	}
	if(found) {
		file = node;
		free(node);
		free(name);
		free(block);
		return 0;
	}
	return -ENOENT;
}

int does_file_exist(struct file_system* lfs, const char* filename, struct inode* parent) {
	struct inode* node;
	int i, res;
	char* block;

	node = malloc(inode_size);
	if(!node) {
		res = -ENOMEM;
	} else {
		block = malloc(BLOCK_SIZE);
		if(!block) {
			free(node);
			res = -ENOMEM;
		}
	}
	if(!res) {
		res = read_block(log_system, block, parent->block_placements[0]);
		if (res) {
			free(block);
			free(node);
			if(res < 0) {
				return res;
			}
			return -1;
		}
		i = 0;
		while (i < BLOCK_SIZE) {
			res = read_inode(log_system, block[i], node);
			if (res) {
				free(block);
				free(node);
				return -1;
			}
			if (strcmp(node->file_name, filename) == 0) {
				int nr = node->inode_number;
				free(node);
				free(block);
				return nr;
			}
			i++;
		}
		free(node);
		free(block);
	}
	return res;
}

/**
* Reads an inode into memory as the inode structure.
*/
int read_inode(struct file_system* lfs, int inode_number, struct inode* inode_ptr) {
	char* block;
	int addr, i, res;
	res = 0;

	printf("read inode: inode number = %d\n", inode_number);
	block = malloc(BLOCK_SIZE);
	if(!block) {
		res = -ENOMEM;
	} else {
		printf("%s\n", "read_inode: 1");
		res = read_inode_table(lfs, block);
		if (!res) {
			printf("%s\n", "read_inode: 2");
			addr = block[inode_number];
			res = read_block(lfs, block, addr);
			if(!res) {
				printf("%s\n", "read_inode: 3");
				strncpy(inode_ptr, block, BLOCK_SIZE);
			}
		}
		free(block);
	}
	printf("read_inode, res: %d\n", res);
	return res;
}

/**
* Copies out the inode table from the buffer (where it always resides as the last
* block in use).
*/
int read_inode_table(struct file_system* lfs, char* put_table_here) {
	int res, addr = 0;

	printf("read_inode_table\n");
	addr = buff_first_free(lfs) - 1;
	printf("read_inode_table: address= %d\n", addr);
	addr = block_start_in_segment(addr);
	res = copy_one_block(lfs->buffer, put_table_here, addr, 0);
	printf("read_inode_table res= %d\n", res);
	printf("read_inode_table: table is = %s\n", put_table_here);
	return res;
}

int get_root_inode(struct file_system* lfs, struct inode* root) {
	/*The root inode will have the first inode number.*/
	printf("%s\n", "get root inode");
	int res = read_inode(lfs, INODE_NUMBERS_MIN, root);
	return res;
}

int get_inode_from_path(struct file_system* lfs, const char *path, struct inode* inode) {
	int res = 0;
	char* curr_path;
	struct inode* node;
	char* file_name;

	curr_path = malloc(sizeof(path));
	if(!curr_path) {
		res = -ENOMEM;
	} else {
		curr_path = path;
		node = malloc(inode_size);
		if (!node) {
			res = -ENOMEM;
		} else {
			file_name = malloc(FILE_NAME_LENGTH_MAX);
			if(!file_name) {
				res = -ENOMEM;
			} else {
				res = get_filename(path, file_name);
				if (!res) {
					int found = 0;
					res = get_root_inode(lfs, node);
					while (!res && !found) {
						if (strcmp(node->file_name, file_name) != 0) {
							found = 1;
						} else {
							res = get_dir(lfs, curr_path, node, node, curr_path);
						}
					}
					if (found) {
						inode = node;
						res = 0;
					} else {
						res = -ENOENT;
					}
				}
				free(file_name);
			}
			free(node);
		}
		free(curr_path);
	}
	return res;
}

int add_child_to_dir(struct file_system* lfs, struct inode* parent, struct inode* child) {
	int res, first_free_block = 0;
	char* inode_table;

	if (parent->number_of_children >= BLOCKS_PR_INODE) {
		perror("add_child_to_dir");
		res = -ENOSPC;
	} else if (!parent->is_dir) {
		perror("add_child_to_dir");
		res = -ENOTDIR;
	} else {
		inode_table = malloc(BLOCK_SIZE);
		if(!inode_table) {
			res = -ENOMEM;
		} else {
			res = read_inode_table(lfs, inode_table);
			if (!res) {
				child->parent_inode_number = parent->inode_number;
				res = buff_assure_space(lfs, 3);
				if(!res) {
					char* block = malloc(BLOCK_SIZE);
					if(!block) {
						res = -ENOMEM;
					} else {
						res = read_block(lfs, parent->block_placements[0], block);
						if (!res) {
							block[parent->number_of_children] = child->inode_number;
							parent->number_of_children++;
							parent->blocks_changed[0] = 1;
							res = buff_write_inode_with_changes(lfs, parent, block);
							if(!res) {
								res = buff_write_inode_with_changes(lfs, child, "");
							}
						}
						free(block);
					}
				}
			}
		}
	}
	return res;
}


int node_trunc(struct file_system* lfs, struct inode* node, int length) {
	int res = 0;
	if (length > node->file_size) {
		int blocks_to_add = 0;
		int needed = length - node->number_of_blocks * BLOCK_SIZE;
		while (needed > BLOCK_SIZE && node->number_of_blocks <= BLOCKS_PR_INODE) {
			node->blocks_changed[node->number_of_blocks] = 1;
			node->number_of_blocks++;
			blocks_to_add++;
			needed -= BLOCK_SIZE;
		}
		char* data = malloc(BLOCK_SIZE * blocks_to_add);
		if (!data) {
			res = -ENOMEM;
		} else {
			res = buff_assure_space(log_system, blocks_to_add);
			while (blocks_to_add > 0) {
				fill_block_with_zero(data, BLOCK_SIZE * (blocks_to_add - 1));
				blocks_to_add--;
			}
			res = buff_write_inode_with_changes(log_system, node, data);
		}
	} else if (length < node->file_size) {
		char* block = malloc(BLOCK_SIZE);
		if (!block) {
			res = -ENOMEM;
		} else {
			res = read_block(log_system, block, node->block_placements[node->number_of_blocks - 1]);
			if (!res) {
				int at = node->file_size - (BLOCK_SIZE * (node->number_of_blocks - 1));
				while (node->file_size > length) {
					block[at] = 0;
					node->file_size--;
					if (at == 0) {
						/*reached start of block*/
						node->number_of_blocks--;
						node->block_placements[node->number_of_blocks] = 0;
						at = BLOCK_SIZE;
					}
					at--;
				}
				res = buff_write_inode_with_changes(log_system, node, "");
			}
		}
	}/* If the file already has the desired legth, there is no need to do anything*/
	return res;
}

int buff_assure_space(struct file_system* lfs, int blocks_to_add) {
	int first_free, res = 0;

	first_free = buff_first_free(lfs);
	if((first_free + blocks_to_add) > BLOCKS_PR_SEGMENT) {
		res = log_write_buffer(lfs);
	}
	return res;
}


int read_block(struct file_system* lfs, char* read_into, int address) {
	int res = 0;
	if ((address >= lfs->next_segment * segment_size) && (address < (lfs->next_segment * segment_size + segment_size))) {
		/* The address points to something in the buffer */
		copy_one_block(lfs->buffer, read_into, (address - lfs->next_segment * segment_size), 0);
	} else {
		FILE* file_ptr = fopen(lfs->log_file_name, "rb");
		if (!file_ptr) {
			perror("read_block, fopen");
			res = -EIO;
		} else {
			res = fread(read_into, 1, BLOCK_SIZE, file_ptr);
			if (res != BLOCK_SIZE) {
				perror("read_block, fread");
				res = -EIO;
			} else {
				res = 0;
			}
		}
	}
	return res;
}

/**
* Fills a block worth with 0
*/
int fill_block_with_zero(char* array, int start_index) {
	int i;
	for (i = start_index; i < BLOCK_SIZE + start_index; i++) {
		array[i] = 0;
	}
	return 0;
}

/**
* Copies one blocks worth of content.
*/
int copy_one_block(char* from, char* to, int from_start_index, int to_start_index) {
	int i;

	printf("copy_one_block: copy %p at %d to %p at %d\n", from, from_start_index, to, to_start_index);
	for (i = 0; i < BLOCK_SIZE; i++) {
		to[i + to_start_index] = from[i + from_start_index];
	}
	return 0;
}

/**
* This method adds blocks to the buffer
* data must hold the contiguous data to be written.
* Each BLOCK_SIZE chunk of data is written to the first
* block marked as changed in the inode, then the next marked
* and so forth.
*/
int buff_write_inode_with_changes(struct file_system* lfs, struct inode* inode_ptr, char* data) {
	int res, i, data_at, block_no, block_address = 0;
	int blocks_needed = 2;
	int first_free;
	int offset = BLOCKS_PR_SEGMENT;
	char* inode_table;

	printf("%s\n", "buff_write_inode_with_changes");

	clock_gettime(CLOCK_REALTIME, &inode_ptr->last_modified);
	clock_gettime(CLOCK_REALTIME, &inode_ptr->last_access);
	for (i = 0; i < BLOCKS_PR_INODE; i++) {
		blocks_needed += inode_ptr->blocks_changed[i];
		printf("blocks_needed: %d\n", blocks_needed);
	}
	res = buff_assure_space(lfs, blocks_needed);
	if (res) {
		perror("buff_write_inode_with_changes, can't get buffer space");
		return res;
	}
	printf("%s\n", "buff_write_inode_with_changes: 1");
	first_free  = buff_first_free(lfs);

	inode_table = malloc(BLOCK_SIZE);
	if(!inode_table) {
		perror("buff_write, malloc");
		res = -ENOMEM;
	} else {
		printf("%s\n", "buff_write_inode_with_changes: 2");
		res = read_inode_table(lfs, inode_table);
		if(!res) {
			printf("%s\n", "buff_write_inode_with_changes: 3");
			for(block_no = 0; block_no < BLOCKS_PR_INODE; block_no++) {
				if ((inode_ptr->blocks_changed[block_no] == 1) && !res) {
					offset = block_start_in_segment(first_free);
					block_address = offset + (lfs->next_segment * segment_size);
					res = copy_one_block(data, lfs->buffer, data_at, offset);
					if (!res) {
						printf("%s\n", "buff_write_inode_with_changes: 4");
						data_at += offset;

						lfs->buffer_summary[first_free] = inode_ptr->inode_number;
						first_free++;

						inode_ptr->block_placements[block_no] = block_address;
						inode_ptr->blocks_changed[block_no] = 0;
					}
				}
			}
			printf("%s\n", "buff_write_inode_with_changes: 5");
			/*All blocks done, the inode is up to date and ready to be written to buffer*/
			offset = block_start_in_segment(first_free);
			res = copy_one_block(&inode_ptr, lfs->buffer, 0, offset);
			if(!res) {
				printf("%s\n", "buff_write_inode_with_changes: 6");
				lfs->buffer_summary[first_free] = 2;
				first_free++;

				inode_table[inode_ptr->inode_number] = offset + (lfs->next_segment * segment_size);
				offset += BLOCK_SIZE;

				res = copy_one_block(inode_table, lfs->buffer, 0, offset);
				if (!res) {
					printf("%s\n", "buff_write_inode_with_changes: 7");
					lfs->buffer_summary[first_free] = 1;
				}
			}
		}
		free(inode_table);
	}
	printf("%s\n", "buff_write_inode_with_changes: 8");
	return res;
}

int log_clear_segment(struct file_system* lfs, int segment) {
	char* zero_segment;
	int i, res = 0;
	FILE* file_ptr;
	for (i = 0; i < BLOCKS_PR_SEGMENT; i++) {
		fill_block_with_zero(zero_segment, i * BLOCK_SIZE);
	}
	file_ptr = fopen(lfs->log_file_name, "wb");
	if (!file_ptr) {
		perror("log_clear_segment, fopen");
		res = -EIO;
	} else {
		res = fseek(file_ptr, segment * segment_size, SEEK_SET);
		if (res) {
			perror("log_clear_segment, fseek");
		} else {
			res = fwrite(zero_segment, segment_size, 1, file_ptr);
			if (res != 1) {
				perror("log_clear_segment, fwrite");
				res = -EIO;
			} else {
				res = 0;
			}
		}
		res = fclose(file_ptr);
		if (res) {
			perror("log_clear_segment, fclose");
			res = -EIO;
		}
	}
	return res;
}

/** Cleaner method
* This method is to be called when space needs to be freed
* in the logfile. It cleans up one segment. Call it again if more space is still needed.
*/
int log_clean(struct file_system* to_be_cleaned) {
// 	FILE* file_ptr;
// 	int res, live_blocks_found, i, position = 0;
// 	char* summary;
// 	char* block_buffer;
// 	file_ptr = fopen(to_be_cleaned->log_file_name, "wb");
// 	if (!file_ptr) {
// 		perror("log_clean, fopen");
// 		res = -EIO;
// 	} else {
// 		/* File is open. Ready to start cleaning */
// 		summary = malloc(BLOCKS_PR_SEGMENT);
// 		block_buffer = malloc(BLOCK_SIZE);
// 		if (!summary) {
// 			perror("log_clean, malloc summary");
// 			res = -ENOMEM;
// 		} else if (!block_buffer) {
// 			perror("log_clean, malloc block_buffer");
// 			free(summary);
// 			res = -ENOMEM;
// 		} else {
// 			/* Memory allocated for summery. Comtinue. */
// 			position = segment_size * to_be_cleaned->oldest_segment;
// 			res = fseek(file_ptr, position, SEEK_SET);
// 			if (res) {
// 				perror("log_clean, fseek");
// 			} else {
// 				if (fread(summary, 1, BLOCKS_PR_SEGMENT, file_ptr) != BLOCKS_PR_SEGMENT) {
// 					perror("log_clean, fread");
// 					res = -EIO;
// 				} else {
// 					for (i = 0; i < BLOCKS_PR_SEGMENT; i++) {
// 						if (!res) {
// 							/* No error has been encountered and we can continue*/
// 							position += BLOCK_SIZE;
// 							if (summary[i]) {
// 								live_blocks_found++;
// 								res = read_block(to_be_cleaned, block_buffer, position);
// 								if (!res) {
// 									/* TODO ???res = buff_write_inode_with_changes(
// 									to_be_cleaned, ); */
// 								}
// 							}
// 							/* TODO: ???res = log_clear_block(position, file_ptr);*/
// 						}
// 					}
// 				}
// 				free(summary);
// 				free(block_buffer);
// 			}
// 			/** TODO:
// 			* reset segment summary
// 			*/
// 			to_be_cleaned->oldest_segment = (to_be_cleaned->oldest_segment + 1)
// 			% SEGMENTS_PR_LOG;
// 			to_be_cleaned->used_segments--;
// 		}
// 		res = fclose(file_ptr);
// 	}
	return 0;
}

/**
* Helper function. Get the filename from the path.
*/
int get_filename(const char* path, char* file_name)
{
	int i = 0;
	int j, l;
	while (path[i] != EOF) {
		i++;
	}
	j= i;
	while (path[i] != '/') {
		i--;
	}
	while (i <= j) {
		file_name[l] = path[i];
		l++;
		i++;
	}
	return 0;
}

/**
* Helper function. Update inode table for inode with new address and write inode
* table to buffer
*/
int update_inode_table(struct file_system* lfs, int inode_number, int new_address) {
int res = 0;
	char* table;

	table = malloc(BLOCK_SIZE);
	if(!table) {
		res = -ENOMEM;
	} else {
		res = read_inode_table(lfs, table);
		if(!res) {
			table[inode_number] = new_address;
			res = buff_assure_space(lfs, 1);
			if (!res) {
				int i = buff_first_free(lfs);
				res = copy_one_block(table, lfs->buffer, 0, (i* (BLOCK_SIZE + BLOCKS_PR_SEGMENT)));
			}
		}
		free(table);
	}
	return res;
}

/**
* Helper function. Gets the first free block of the buffer.
*/
int buff_first_free(struct file_system* lfs) {
	int i;

	printf("%s\n", "buff_first_free");
	for (i = 0; i < BLOCKS_PR_SEGMENT; i++) {
		if(lfs->buffer_summary[i] == 0) {
			break;
		}
	}
	return i;
}

int block_start_in_segment(int block_no) {
	printf("block_start_in_segment for %d\n", block_no);
	int res = (block_no * BLOCK_SIZE) + BLOCKS_PR_SEGMENT;
	printf("block_start_in_segment returns %d\n", res);
	return res;
}

int init_inode_table(struct file_system* lfs) {
	char* block;
	int res = 0;

	printf("%s\n", "init_inode_table");
	block = malloc(BLOCK_SIZE);
	block[0] = 1;
	strncpy(lfs->buffer, block, BLOCK_SIZE);
	return res;
}

/**
* This method clears the buffer of a file system.
* Only call this when absolutely sure.
* The inode table is put in the beginning of the empty
* buffer.
*/
int buff_clear(struct file_system* lfs) {
	int i, res;
	char* inode_table;

	inode_table = malloc(BLOCK_SIZE);
	if(!inode_table) {
		res = -ENOMEM;
	} else {
		res = read_inode_table(lfs, inode_table);
		/* We have the inode table. Write it in the first block*/
		res = fill_block_with_zero(lfs->buffer, BLOCKS_PR_SEGMENT);
		if (!res) {
			res = copy_one_block(inode_table, lfs->buffer, 0, BLOCKS_PR_SEGMENT);
			if (!res) {
				lfs->buffer_summary[0] = 1;
			}
			/* Now clear the rest*/
			for (i = 1; i < BLOCKS_PR_SEGMENT; i++) {
				fill_block_with_zero(lfs->buffer, (BLOCKS_PR_SEGMENT + i * BLOCK_SIZE));
				lfs->buffer_summary[i] = 0;
			}
		}
		free(inode_table);
	}
	return res;
}

/**
* Adds the buffer as a segment to the appropriate place in the log, then clears
* the buffer.
*/
int log_write_buffer(struct file_system* lfs) {
	int res = 0;
	int addr;
	FILE* file_ptr;

	printf("%s\n", "log_write");
	file_ptr = fopen(lfs->log_file_name, "wb");
	if (!file_ptr) {
		perror("log_write, fopen");
		res = -EIO;
	} else {
		addr = lfs->next_segment * segment_size;
		res = fseek(file_ptr, addr, SEEK_SET);
		if (!res) {
			res = fwrite(lfs->buffer_summary, BLOCKS_PR_SEGMENT, 1, file_ptr);
			if (res != 1) {
				perror("log_write, fwrite");
				res = -EIO;
			} else {
				res = 0;
				res = fwrite(lfs->buffer, BLOCK_SIZE, BLOCKS_PR_SEGMENT, file_ptr);
				if (!res) {
					lfs->next_segment = (lfs->next_segment + 1) % SEGMENTS_PR_LOG;
					lfs->used_segments++;
					buff_clear(lfs);
				}
			}
			res = fclose(file_ptr);
			if (res) {
				perror("log_write, fclose");
				res = -EIO;
			}
		}
	}
	return res;
}
