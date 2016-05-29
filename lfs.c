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
	if (strncmp(path, "/.Trash", 7) == 0) {
		//TODO what to do here?
		return 0;
	}
	node = malloc(INODE_SIZE);
	if (!node) {
		perror("lfs_getattr, malloc");
		res = -ENOMEM;
	} else {
		if (strcmp(path, "/") == 0) {
			res = get_root_inode(log_system, node);
			if (!res) {
				stbuf->st_mode = S_IFDIR | 0755;
				stbuf->st_nlink = 2 + node->number_of_children;
			}
		} else {
			res = get_inode_from_path(log_system, path, node);
			if (res) {
				perror("lfs_getattr, get_inode_from_path");
				printf("getattr: get inode from path returned %d\n", res);
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
	printf("get_attr done\n");
	return res;
}

int lfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
	//printf("lfs_create: path = %s", path);
	return 0; //TODO
}

/** Reads a directory */
int lfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		off_t offset, struct fuse_file_info *fi) {
	struct inode* node;
	struct inode* child;
	unsigned int* block;
	int res = 0;
	//printf("readdir: (path=%s)\n", path);

	node = malloc(INODE_SIZE);
	if (!node) {
		res = -ENOMEM;
	} else {
		if (strcmp(path, "/") == 0) {
			res = get_root_inode(log_system, node);
			if (!res) {
				struct stat* stbuf = malloc(sizeof(struct stat));
				if (!stbuf) {
					res = -ENOMEM;
				} else {
					stbuf->st_ino = node->inode_number;
					stbuf->st_mode = S_IFDIR | 755;
					filler(buf, ".", stbuf, 0);
					filler(buf, "..", NULL, 0);
					free(stbuf);
				}
			}
		} else {
			res = get_inode_from_path(log_system, path, node);
			if (res) {
				return -ENOENT;
			} else {
				if (!node->is_dir) {
					////printf("Well, that explains it\n");
					res = -ENOTDIR;
				} else {
					////printf("readdir: 1\n");
					block = malloc(BLOCK_SIZE);
					if (!block) {
						res = -ENOMEM;
					} else {
						struct stat* stbuf = malloc(sizeof(struct stat));
						if (!stbuf) {
							res = -ENOMEM;
						} else {
							int i;
							////printf("readdir: 3\n");
							stbuf->st_ino = node->inode_number;
							stbuf->st_mode = S_IFDIR;
							filler(buf, ".", NULL, 0);
							filler(buf, "..", NULL, 0);
							for (i = 0; (i < node->number_of_children) && !res; i++) {
								////printf("readdir: 4,%d\n", i);
								child = malloc(INODE_SIZE);
								if (!child) {
									res = -ENOMEM;
								} else {
									res = read_inode(log_system, node->block_placements[i],
											child);
									stbuf->st_ino = child->inode_number;
									if (child->is_dir) {
										stbuf->st_mode = S_IFDIR | 755;
									} else {
										stbuf->st_mode = S_IFREG | 755;
									}
									filler(buf, child->file_name, stbuf, 0);
									free(child);
								}

							}
							free(stbuf);
						}

						free(block);
					}
				}
			}
		}
		free(node);
	}
	return res;
}

int lfs_open(const char *path, struct fuse_file_info *fi) {
	int res = 0;
	struct inode* node;
	//printf("open: (path=%s)\n", path);
	node = malloc(INODE_SIZE);
	if (!node) {
		res = -ENOMEM;
	} else {
		res = get_inode_from_path(log_system, path, node);
		if (!res) {
			fi->fh = node->inode_number;
			open_file = fi;
			//TODO flags and stuff
		}
	}
	return res;
}

int lfs_read(const char *path, char *buf, size_t size, off_t offset,
		struct fuse_file_info *fi) {
	int res, block_no, left_to_read;
	struct inode* node;
	unsigned int* block_buff;

	res = block_no = left_to_read = 0;
	//printf("read: (path=%s)\n", path);

	node = malloc(BLOCK_SIZE);
	if (!node) {
		res = -ENOMEM;
	} else {
		block_buff = malloc(BLOCK_SIZE);
		if (!block_buff) {
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
					res = read_block(log_system, block_buff,
							node->block_placements[block_no]);
					if (!res) {
						if (left_to_read < BLOCK_SIZE) {
							memcpy(buf[blocks_read * BLOCK_SIZE], block_buff[start],
									left_to_read);
							left_to_read = 0;
						} else {
							res = copy_one_block(block_buff, buf, start,
									blocks_read * BLOCK_SIZE);
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
	//printf("release: (path=%s)\n", path); //TODO
	return 0;
}

int lfs_mkdir(const char *path, mode_t mode) {
	int res = 0;
	struct inode* node;
	struct inode* parent;
	char* name;

	printf("%s\n", "lfs_mkdir");

	node = malloc(INODE_SIZE);
	if (!node) {
		res = -ENOMEM;
	} else {
		if (get_inode_from_path(log_system, path, node) == 0) {
			free(node);
			printf("mkdir: dir exists\n");
			return -EEXIST;
		}
		name = malloc(FILE_NAME_LENGTH_MAX);
		if (!name) {
			res = -ENOMEM;
		} else {
			//printf("mkdir: 1\n");
			res = get_filename(path, name);
			if (!res) {
				//printf("mkdir: 2\n");
				parent = malloc(INODE_SIZE);
				if (!parent) {
					res = -ENOMEM;
				} else {
					//printf("mkdir: 3\n");
					res = get_root_inode(log_system, parent);
					if (!res) {
						//printf("mkdir: 4\n");
						char* temp = malloc(strlen(path));
						if (!temp) {
							res = -ENOMEM;
						} else {
							//printf("mkdir: 5\n");
							strcpy(temp, path);
							while ((strcmp(temp, name) != 0) && (strcmp(temp + 1, name) != 0)
									&& !res) {
								//printf("mkdir: temp now = %s\n", temp);
								res = traverse_path(log_system, temp, parent, node, temp);
								if (!res) {
									parent = node;
								}
							}
							if (!res) {
								printf("mkdir: 6\n");
								printf("mkdir: parent is %s\n", parent->file_name);
								node->inode_number = log_system->number_of_inodes
										+ INODE_NUMBERS_MIN;
								node->is_dir = 1;
								memcpy(node->file_name, name, strlen(name));
								node->number_of_blocks = 0;
								//printf("mkdir: child is %s\n", node->file_name);
								node->number_of_children = 0;
								node->file_size = strlen(name);
								memset(node->block_placements, 0, BLOCKS_PR_INODE);
								memset(node->blocks_changed, 0, BLOCKS_PR_INODE);
								//printf("mkdir: 9\n");
								res = add_child_to_dir(log_system, parent, node);
								//printf("mkdir: parent has %d children\n", parent->number_of_children);

							}
							free(temp);
						}
					}
				}
			}
			free(name);
		}
	}
	//printf("mkdir: returning %d\n", res);
	return res;
}

int lfs_unlink(const char *filename) {
	int res = 0;
	struct inode* node;
	struct inode* parent;

	node = malloc(INODE_SIZE);
	if (!node) {
		res = -ENOMEM;
	} else {
		res = get_inode_from_path(log_system, filename, node);
		if (res) {
			perror("unlink");
		} else {
			if (node->is_dir) {
				res = -EBADF;
			} else {
				parent = malloc(INODE_SIZE);
				if (!parent) {
					res = -ENOMEM;
				} else {
					res = read_inode(log_system, node->parent_inode_number, parent);
					if (res) {
						perror("unlink");
					} else {
						int i, found = 0;
						unsigned int block[BLOCK_SIZE];
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
									unsigned int inode_table[BLOCK_SIZE];
									res = read_inode_table(log_system, inode_table);
									if (res) {
										perror("unlink");
									} else {
										inode_table[node->inode_number] = 0;
										i = 0;
										while (log_system->buffer_summary[i] > 0) {
											i++;
										}
										copy_one_block(inode_table, log_system->buffer, 0,
												i * BLOCK_SIZE + BLOCKS_PR_SEGMENT
														+ log_system->next_segment * SEGMENT_SIZE);
										log_system->number_of_inodes--;
									}
								}
							}
						}
					}
					free(parent);
				}
			}
		}
		free(node);
	}
	return res;
}

int lfs_rmdir(const char *path) {
	int res = 0;
	int i;
	struct inode* node;
	struct inode* parent;
	unsigned int* block;

	node = malloc(INODE_SIZE);
	if (!node) {
		res = -ENOMEM;
	} else {
		res = get_inode_from_path(log_system, path, node);
		if (!res) {
			if (node->is_dir) {
				res = -ENOTDIR;
			} else {
				parent = malloc(INODE_SIZE);
				if (!parent) {
					res = -ENOMEM;
				} else {
					res = read_inode(log_system, node->parent_inode_number, parent);
					if (!res) {
						if (node->number_of_children > 0) {
							res = -ENOTEMPTY;
						} else {
							block = malloc(BLOCK_SIZE);
							if (!block) {
								res = -ENOMEM;
							} else {
								res = read_block(log_system, block,
										parent->block_placements[0]);
								if (!res) {
									int found = 0;
									for (i = 0; i <= parent->number_of_children; i++) {
										if (found) {
											block[i - 1] = block[i];
										} else if (block[i] == node->inode_number) {
											found = 1;
										}
									}
									parent->number_of_children--;
									parent->blocks_changed[0] = 1;
									res = update_inode_table(log_system, node->inode_number, 0);
									if (!res) {
										log_system->number_of_inodes--;
										res = buff_write_inode_with_changes(log_system, parent,
												block);
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
		free(node);
	}
	return res;
}

ssize_t lfs_write(int filedes, const void *buf, size_t count) {
	int err, write = 0;
	struct inode* node;

	unsigned int* block = malloc(BLOCK_SIZE);
	if (!block) {
		return -ENOMEM;
	} else {
		node = malloc(INODE_SIZE);
		if (!node) {
			err = -ENOMEM;
		} else {
			err = read_inode(log_system, filedes, node);
			if (node->file_size % BLOCK_SIZE == 0) {
				/*All blocks started on are filled*/
				write = count;
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
				if (!err) {
					int start = (node->file_size % BLOCK_SIZE);
					write = BLOCK_SIZE - start;
					if (write > count) {
						write = count;
					}
					err = read_block(log_system, block,
							node->block_placements[node->number_of_blocks - 1]);
					if (!err) {
						node->blocks_changed[node->number_of_blocks - 1] = 1;
						memcpy(&block[start], buf, write);
						node->file_size += write;
						err = buff_write_inode_with_changes(log_system, node, block);
					}
				}
			}
			free(node);
		}
		free(block);
	}
	if (err < 0) {
		return err;
	}
	return write;
}

/**
 * Helper function. Get folder or file at start of path.
 */
int get_inode_from_path(struct file_system* lfs, const char *path,
		struct inode* inode) {
	int res = 0;
	int found;
	char* curr_path;
	struct inode* node;
	char* file_name;

	printf("get_inode_from_path: path = %s\n", path);

	if (!inode) {
		inode = malloc(INODE_SIZE);
	}
	if (!inode) {
		return -ENOMEM;
	}
//printf("1\n");
	if (strcmp(path, "/") == 0) {
		res = get_root_inode(lfs, inode);
	} else {
		//printf("2\n");
		file_name = malloc(FILE_NAME_LENGTH_MAX);
		if (!file_name) {
			res = -ENOMEM;
		} else {
			printf("3\n");
			res = get_filename(path, file_name);
			if (!res) {
				//printf("get_inode_..: path = %s\n", path);
				//printf("get_inode_from path: file_name = %s\n", file_name);
				node = malloc(INODE_SIZE);
				if (!node) {
					res = -ENOMEM;
				} else {
					printf("4\n");
					res = get_root_inode(lfs, node);
					if (!res) {
						printf("5\n");
						curr_path = malloc(FILE_NAME_LENGTH_MAX * INODES_PR_LOG);
						if (!curr_path) {
							res = -ENOMEM;
						} else {
							printf("6\n");
							found = 0;
							curr_path = path;
							printf("7\n");
							res = get_root_inode(lfs, node);
							printf("get_inode_from_path: root inode has %d children\n",
									node->number_of_children);
							while (!res && !found) {
								if (strcmp(node->file_name, file_name) == 0) {
									found = 1;
								} else {
									printf("curr_path = %s, path  = %s\n", curr_path, path);
									res = traverse_path(lfs, curr_path, node, node, curr_path);
									if (res == -ENOENT) {
										printf("get_inode_from_path: no node with path: %s\n",
												path);
									}
								}
							}
							if (found) {
								inode = node;
								res = 0;
							} else {
								res = -ENOENT;
							}
							free(curr_path);
						}
					}
					free(node);
				}
			}
			free(file_name);
		}
	}
//printf("get_inode_from_path: returns %d, inode number was: %d\n", res, node->inode_number);
	return res;
}

int traverse_path(struct file_system* lfs, const char* path,
		struct inode* parent, struct inode* file, char* new_path) {
	unsigned int i;
	int res = 0;
	int found = 0;
	struct inode* node;
	char* name = malloc(FILE_NAME_LENGTH_MAX);

	printf("traverse_path: path = %s\n", path);

	if (parent->number_of_children <= 0) {
		free(name);
		return -ENOENT;
	}
	if (!name) {
		return -ENOMEM;
	}
	node = malloc(INODE_SIZE);
	if (!node) {
		res = -ENOMEM;
	}
	res = get_filename(path, name);
	printf("filename is = %s\n", name);

	i = 0;
	while (!found && i < parent->number_of_children) {
		unsigned int child_inode_number = parent->block_placements[i];
		printf("child: %d\n", child_inode_number);
		res = read_inode(lfs, child_inode_number, node);
		printf("read node: %s\n", node->file_name);
		if (res) {
			free(name);
			free(node);
			return res;
		}
		if (strcmp(node->file_name, name) == 0) {
			found = 1;
		}
	}
	if (found) {
		file = node;
		free(node);
		free(name);
		return 0;
	}
	return -ENOENT;
}

/**
 * Reads an inode into memory as the inode structure.
 */
int get_filename(char* path, char* file_name) {
	int max, i;
	unsigned char c = '/';

//printf("get_filename: path = %s)\n", path);

	max = strnlen(path, FILE_NAME_LENGTH_MAX * INODES_PR_LOG) - 1;
	memset(file_name, 0, max);
	for (i = max; i >= 0; i--) {
		c = path[i];
		if (c == '/') {
			i++;
			break;
		}
	}
	strcpy(file_name, path + i);
	return 0;
}

int read_inode(struct file_system* lfs, int inode_number,
		struct inode* inode_ptr) {
	unsigned int block[BLOCK_SIZE];
	int res;
	unsigned int addr;

	res = 0;
	printf("read inode: inode number = %d\n", inode_number);

	if (!inode_ptr) {
		res = -EFAULT;
	} else if (!res) {
		printf("read_inode: 1, block = %p\n", block);

		printf("%s\n", "read_inode: 2");
		res = read_inode_table(lfs, block);
		if (!res) {
			printf("inode table says address is: %d\n", block[inode_number]);
			addr = block[inode_number];
			printf("read_inode: address = %d\n", addr);
			res = read_block(lfs, inode_ptr, addr);
		}
	}
	if (inode_ptr->inode_number == INODE_NUMBERS_MIN) {
		inode_ptr->is_dir = 1;
	}
	printf("read_inode: node is %s\n", inode_ptr->file_name);
//printf("read_inode: returns %d\n", res);
	return res;
}

/**
 * Copies out the inode table from the buffer (where it always resides as the last
 * block in use).
 */
int read_inode_table(struct file_system* lfs, unsigned int* put_table_here) {
	int res = 0;
	int i;
	unsigned int addr = 0;

	printf("read_inode_table\n");
	addr = (buff_first_free(lfs) - 1);
	addr = block_start_in_segment(addr);
//printf("read_inode_table: address= %d\n", addr);
	copy_one_block(lfs->buffer, put_table_here, addr, 0);
//res = copy_one_block(lfs->buffer, put_table_here, addr, 0);
	for (i = 0; i < 20; i++) {
		//printf("i = %d --- table[i] = %d\n", i, put_table_here[i]);
	}
	for (i = 0; i < lfs->number_of_inodes; i++) {
		//printf("table: inode nr %d is at %d\n", i + INODE_NUMBERS_MIN, put_table_here[i + INODE_NUMBERS_MIN]);
	}
////printf("read_inode_table: table is = %p\n", put_table_here);
////printf("read_inode_table: table[3] is = %d\n", put_table_here[3]);
	return res;
}

int get_root_inode(struct file_system* lfs, struct inode* root) {
	/*The root inode will have the first inode number.*/
////printf("%s\n", "get root inode");
	int res = read_inode(lfs, INODE_NUMBERS_MIN, root);
	return res;
}

int add_child_to_dir(struct file_system* lfs, struct inode* parent,
		struct inode* child) {
	int res = 0;
	unsigned int inode_table[BLOCK_SIZE];

//printf("add_child: child name: %s\n", child->file_name);

	if (parent->number_of_children >= BLOCKS_PR_INODE) {
		perror("add_child_to_dir");
		res = -ENOSPC;
	} else if (!parent->is_dir) {
		perror("add_child_to_dir");
		res = -ENOTDIR;
	} else {
		//printf("add_child: 1\n");
		//printf("add_child: 2\n");
		res = read_inode_table(lfs, inode_table);
		if (!res) {
			//printf("add_child: 3\n");
			child->parent_inode_number = parent->inode_number;
			if (!res) {
				//printf("add_child: 4\n");
				if (parent->number_of_children <= 0) {
					lfs->number_of_inodes++;
					res = buff_write_inode_with_changes(lfs, child, NULL);
					if (!res) {
						parent->block_placements[parent->number_of_children] =
								child->inode_number;
						parent->number_of_children++;
						res = buff_write_inode_with_changes(lfs, parent, NULL);
					}
				}
			}
		}
	}
//printf("add_child: returning %d\n", res);
	return res;
}

int node_trunc(struct file_system* lfs, struct inode* node, unsigned int length) {
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
		unsigned int* data = malloc(BLOCK_SIZE * blocks_to_add);
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
		unsigned int* block = malloc(BLOCK_SIZE);
		if (!block) {
			res = -ENOMEM;
		} else {
			res = read_block(log_system, block,
					node->block_placements[node->number_of_blocks - 1]);
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
				res = buff_write_inode_with_changes(log_system, node, NULL);
			}
		}
	}/* If the file already has the desired legth, there is no need to do anything*/
	return res;
}

int buff_assure_space(struct file_system* lfs, int blocks_to_add) {
	int first_free, res = 0;

//printf("buff_assure_space: finding space for %d blocks\n", blocks_to_add);

	first_free = buff_first_free(lfs);
	if ((first_free + blocks_to_add) > BLOCKS_PR_SEGMENT) {
		//printf("buff_assure_space: buffer needs to be emptied\n");
		res = log_write_buffer(lfs);
	}
//printf("buff_assure_space returning: %d\n", res);
	return res;
}

int buff_write_inode_with_changes(struct file_system* lfs,
		struct inode* inode_ptr, unsigned int* data) {
	int res, i, data_at, block_no;
	int blocks_needed = 2;
	int first_free;
	int offset = BLOCKS_PR_SEGMENT;
	unsigned int inode_table[BLOCK_SIZE];

	int block_address = res = i = data_at = block_no = 0;

//printf("buff_write_inode_with_changes: inode nr = %d\n", inode_ptr->inode_number);

	clock_gettime(CLOCK_REALTIME, &inode_ptr->last_modified);
	clock_gettime(CLOCK_REALTIME, &inode_ptr->last_access);
	for (i = 0; i < BLOCKS_PR_INODE; i++) {
		blocks_needed += inode_ptr->blocks_changed[i];
	}
//printf("blocks_needed: %d\n", blocks_needed);
	res = buff_assure_space(lfs, blocks_needed);
	if (res) {
		perror("buff_write_inode_with_changes, can't get buffer space");
		return res;
	}
//printf("%s\n", "buff_write_inode_with_changes: 1");
	first_free = buff_first_free(lfs);

	if (!res) {
		//printf("%s\n", "buff_write_inode_with_changes: 2");
		res = read_inode_table(lfs, inode_table);
		if (!res) {
			if (inode_ptr->is_dir) {
				//DO nothing
			} else {
				int max = inode_ptr->number_of_blocks;
				for (block_no = 0; block_no < max; block_no++) {
					if ((inode_ptr->blocks_changed[block_no] == 1) && !res) {
						offset = block_start_in_segment(first_free);
						block_address = offset + (lfs->next_segment * SEGMENT_SIZE);
						res = copy_one_block(data, lfs->buffer, data_at, offset);
						if (!res) {
							//printf("%s\n", "buff_write_inode_with_changes: 4");
							data_at += offset;

							lfs->buffer_summary[first_free] = inode_ptr->inode_number;
							first_free++;

							inode_ptr->block_placements[block_no] = block_address;
							inode_ptr->blocks_changed[block_no] = 0;
						}
					}
				}
			}
			//printf("%s\n", "buff_write_inode_with_changes: 3");

			//printf("%s\n", "buff_write_inode_with_changes: 5");
			/*All blocks done, the inode is up to date and ready to be written to buffer*/
			offset = block_start_in_segment(first_free);
			res = copy_one_block(&inode_ptr, lfs->buffer, 0, offset);
			if (!res) {
				unsigned int addr;
				//printf("%s\n", "buff_write_inode_with_changes: 6");
				lfs->buffer_summary[buff_first_free(lfs)] = 2;

				addr = (offset + (lfs->next_segment * SEGMENT_SIZE));
				inode_table[inode_ptr->inode_number] = addr;
				//printf("inode table[inode_number]: %d, addr = %d\n", inode_table[inode_ptr->inode_number], addr);
				offset += BLOCK_SIZE;

				res = copy_one_block(inode_table, lfs->buffer, 0, offset);
				if (!res) {
					//printf("%s\n", "buff_write_inode_with_changes: 7");
					lfs->buffer_summary[buff_first_free(lfs)] = 1;
				}
			}
		}
	}
//printf("%s\n", "buff_write_inode_with_changes: 8 end");
	return res;
}

int buff_first_free(struct file_system* lfs) {
	int i;

//printf("%s\n", "buff_first_free");
	for (i = 0; i < BLOCKS_PR_SEGMENT; i++) {
		////printf("buff_first_free: summary of %d is %d\n", i, lfs->buffer_summary[i]);
		if (lfs->buffer_summary[i] == 0) {
			break;
		}
	}
////printf("buff_first_free: returning %d\n", i);
	return i;
}

int read_block(struct file_system* lfs, unsigned int* read_into,
		unsigned int address) {
	int res = 0;

//printf("read_block: address = %d\n", address);
	if ((address >= lfs->next_segment * SEGMENT_SIZE)
			&& (address < (lfs->next_segment * SEGMENT_SIZE + SEGMENT_SIZE))) {
		/* The address points to something in the buffer */
		////printf("read_block: address in buffer\n");
		copy_one_block(lfs->buffer, read_into,
				(address - lfs->next_segment * SEGMENT_SIZE), 0);
	} else {
		////printf("read_block: address in log\n");
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
int fill_block_with_zero(unsigned int* array, unsigned int start_index) {
	int i;
	for (i = start_index; i < BLOCK_SIZE + start_index; i++) {
		array[i] = 0;
	}
	return 0;
}

/**
 * Copies one blocks worth of content.
 */
int copy_one_block(unsigned int* from, unsigned int* to,
		unsigned int from_start_index, unsigned int to_start_index) {
	int i;

//printf("copy_one_block: copy %p at %d to %p at %d\n", from, from_start_index, to, to_start_index);
	for (i = 0; i < BLOCK_SIZE; i++) {
		to[i + to_start_index] = from[i + from_start_index];
		if (i < 10) {
			//printf("copy %d to destination %d\n", from[i + from_start_index], i + from_start_index);
			//printf("destination is: %d\n", to[i + to_start_index]);
		}
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
int log_clear_segment(struct file_system* lfs, int segment) {
	unsigned int zero_segment[BLOCK_SIZE];
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
		res = fseek(file_ptr, segment * SEGMENT_SIZE, SEEK_SET);
		if (res) {
			perror("log_clear_segment, fseek");
		} else {
			res = fwrite(zero_segment, SEGMENT_SIZE, 1, file_ptr);
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
// 	unsigned int* summary;
// 	unsigned int* block_buffer;
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
/**
 * Helper function. Update inode table for inode with new address and write inode
 * table to buffer
 */
int update_inode_table(struct file_system* lfs, int inode_number,
		int new_address) {
	int res = 0;
	unsigned int table[BLOCK_SIZE];

	res = read_inode_table(lfs, table);
	if (!res) {
		table[inode_number] = new_address;
		res = buff_assure_space(lfs, 1);
		if (!res) {
			int i = buff_first_free(lfs);
			res = copy_one_block(table, lfs->buffer, 0,
					(i * (BLOCK_SIZE + BLOCKS_PR_SEGMENT)));
		}
	}
	return res;
}

/**
 * Helper function. Gets the first free block of the buffer.
 */
int block_start_in_segment(int block_no) {
//printf("block_start_in_segment for %d\n", block_no);
	int res = (block_no * BLOCK_SIZE) + BLOCKS_PR_SEGMENT;
//printf("block_start_in_segment returns %d\n", res);
	return res;
}

int init_inode_table(struct file_system* lfs) {
	unsigned int inode_table[BLOCK_SIZE];
	struct inode* root;
	struct inode* hello;
	int res = 0;
	unsigned int addr;
	int at = 0;

	printf("inode_table: %p, %d\n", inode_table, sizeof(&inode_table));

	printf("%s\n", "init_inode_table");

	hello = malloc(INODE_SIZE);
	if (hello) {
		hello->inode_number = INODE_NUMBERS_MIN + 1;
		memset(hello->file_name, 0, FILE_NAME_LENGTH_MAX);
		strcpy(hello->file_name, "hello.txt");
		hello->is_dir = 0;
		memset(hello->block_placements, 0, BLOCKS_PR_INODE);
		memset(hello->blocks_changed, 0, BLOCKS_PR_INODE);
		hello->number_of_blocks = 0;
		hello->parent_inode_number = INODE_NUMBERS_MIN;
		hello->file_size = 0;

		root = malloc(INODE_SIZE);
		if (!root) {
			res = -ENOMEM;
		} else {
			memset(root->file_name, 0, FILE_NAME_LENGTH_MAX);
			memcpy(root->file_name, "/", sizeof("/"));
			root->inode_number = INODE_NUMBERS_MIN;
			root->parent_inode_number = root->inode_number;
			root->is_dir = 1;
			root->number_of_blocks = 0;
			root->number_of_children = 1;
			root->block_placements[0] = hello->inode_number;
			if (!res) {
				addr = block_start_in_segment(0);
				//printf("init table: addr = %d\n", addr);

				memset(inode_table, 0, BLOCK_SIZE);
				res = copy_one_block(hello, lfs->buffer, 0, addr);
				if (!res) {
					lfs->buffer_summary[at] = BLOCK_TYPE_INODE;
					inode_table[hello->inode_number] = complete_address(lfs, addr);
					addr = block_start_in_segment(1);
					at++;
					lfs->number_of_inodes = 1;
					addr = block_start_in_segment(at);
					res = copy_one_block(root, lfs->buffer, 0, addr);
					inode_table[root->inode_number] = complete_address(lfs, addr);
					lfs->buffer_summary[at] = BLOCK_TYPE_INODE;
					at++;
					lfs->number_of_inodes++;
					addr = block_start_in_segment(at);
					res = copy_one_block(inode_table, lfs->buffer, 0, addr);
					lfs->buffer_summary[at] = BLOCK_TYPE_ITBL;
					at++;
				}
			}
			//free(root);
		}
		//free(hello);
	} else {
		res = -ENOMEM;
	}
//printf("Storage size for int : %d \n", sizeof(int));
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
	unsigned int inode_table[BLOCK_SIZE];

	i = res = 0;
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
	return res;
}

/**
 * Adds the buffer as a segment to the appropriate place in the log, then clears
 * the buffer.
 */
int main(int argc, char *argv[]) {
	int res = 0;

//printf("%s\n", "lfs_main");

	log_system = malloc(sizeof(struct file_system));
	if (!log_system) {
		perror("main malloc");
	} else {
		////printf("%s\n", "lfs_main: 1");
		log_system->log_file_name = "/tmp/semihugefile.file";
		if (!log_system->log_file_name) {
			perror("Filename not allocated/set");
		} else {
			////printf("%s\n", "lfs_main: 2");
			log_system->buffer = malloc(SEGMENT_SIZE);
			if (!log_system->buffer) {
				res = -ENOMEM;
			} else {
				memset(log_system->buffer_summary, 0, BLOCKS_PR_SEGMENT);
				memset(log_system->buffer, 0, SEGMENT_SIZE);
				log_system->next_segment = 0;
				log_system->used_segments = 0;
				res = init_inode_table(log_system);
				////printf("%s\n", "lfs_main: 4");
			}
		}
	}
	if (res) {
		//printf("main fail: %d\n", res);
		//TODO error handling
	}
	return fuse_main(argc, argv, &lfs_oper);
}

int log_write_buffer(struct file_system* lfs) {
	int res = 0;
	unsigned int addr;
	FILE* file_ptr;

//printf("%s\n", "log_write");
	file_ptr = fopen(lfs->log_file_name, "wb");
	if (!file_ptr) {
		perror("log_write, fopen");
		res = -EIO;
	} else {
		addr = lfs->next_segment * SEGMENT_SIZE;
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

int complete_address(struct file_system* lfs, unsigned int addr_in_buff) {
	int res = (lfs->next_segment * SEGMENT_SIZE);
	res += addr_in_buff;
	return res;
}
