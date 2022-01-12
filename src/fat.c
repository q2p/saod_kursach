#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

typedef uint16_t ClusterLocation;
typedef uint16_t ClusterOffset;
typedef uint32_t FileCursor;

typedef uint8_t Result;
typedef uint8_t OptionalResult;

enum {
	CLUSTER_SIZE = 4*1024,
	MAX_CLUSTERS = 2048,
	TABLE_SIZE = MAX_CLUSTERS*sizeof(ClusterLocation),
	ROOT_OFFSET = TABLE_SIZE,
	
	FILE_META = 64,
	OFFSET_SIZE = 0,
	OFFSET_CLUSTER = sizeof(ClusterOffset),
	OFFSET_NAME = sizeof(ClusterOffset) + sizeof(ClusterLocation),
	MAX_FILE_NAME = FILE_META - OFFSET_NAME,
	FILES_PER_CLUSTER = CLUSTER_SIZE / FILE_META,

	TV_EMPTY = 0x0000,
	TV_FINAL = 0xFFFF,
	TV_CANT_ALLOC = 0x0000,

	FS_FOLDER = 0xFFFF,

	OPTIONAL_OK = 0,
	OPTIONAL_IO_ERROR = 1,
	OPTIONAL_STRUCTURE_ERROR = 2,
};

_STATIC_ASSERT(sizeof(uint8_t) == 1);
_STATIC_ASSERT(sizeof(ClusterLocation) % sizeof(uint8_t) == 0);
_STATIC_ASSERT(TABLE_SIZE % CLUSTER_SIZE == 0);
_STATIC_ASSERT(CLUSTER_SIZE % FILE_META == 0);
_STATIC_ASSERT(FILES_PER_CLUSTER < UINT8_MAX);
_STATIC_ASSERT(FS_FOLDER >= CLUSTER_SIZE);
_STATIC_ASSERT(MAX_CLUSTERS < UINT16_MAX);

typedef struct {
	FILE* file;
	uint16_t clusters_count;
	ClusterLocation table_cache[TABLE_SIZE];
} FileSystem;

typedef struct {
	ClusterLocation current_cluster;
} DirCursor;

typedef struct {
	ClusterLocation current_cluster;
	uint8_t meta[FILE_META];
} DirEntry;

typedef struct {
	ClusterLocation current_cluster;
	uint8_t next_folder;
} DirIter;

void read(FileSystem* restrict fs, ClusterLocation cluster, uint8_t* restrict buffer) {
	fseek(fs->file, ROOT_OFFSET + cluster * CLUSTER_SIZE, SEEK_SET);
	fread(buffer, 1, CLUSTER_SIZE, fs->file);
}

void write(FileSystem* restrict fs, ClusterLocation cluster, uint8_t* restrict buffer) {
	fseek(fs->file, ROOT_OFFSET + cluster * CLUSTER_SIZE, SEEK_SET);
	fwrite(buffer, 1, CLUSTER_SIZE, fs->file);
}

uint16_t read_u16(uint8_t* restrict ptr) {
	return ptr[0] | ptr[1] << 8;
}

void write_u16(uint8_t* restrict ptr, uint16_t value) {
	ptr[0] = value;
	ptr[1] = value >> 8;
}

ClusterLocation get_cluster(DirEntry* restrict entry) {
	return read_u16(entry->meta + OFFSET_CLUSTER);
}

ClusterOffset get_meta_size(DirEntry* restrict entry) {
	return read_u16(entry->meta + OFFSET_SIZE);
}

uint8_t is_folder(DirEntry* restrict entry) {
	return get_meta_size(entry) == FS_FOLDER;
}

FileCursor get_file_size(FileSystem* restrict fs, DirEntry* restrict entry) {
	FileCursor ret = (FileCursor) get_meta_size(entry);
	ClusterLocation cluster = entry->current_cluster;
	while(fs->table_cache[cluster] != TV_FINAL) {
		cluster = fs->table_cache[cluster];
		ret += cluster;
	}
	return ret;
}

// TODO: name должен быть padded
void init_meta(DirEntry* restrict entry, uint8_t is_folder, uint8_t* restrict name) {
	write_u16(entry->meta + OFFSET_SIZE, is_folder ? FS_FOLDER : 0);
	memcpy(entry->meta + OFFSET_NAME, name, MAX_FILE_NAME);
}

ClusterLocation allocate(FileSystem* restrict fs) {
	for(size_t i = 1; i != TABLE_SIZE; i++) {
		if (fs->table_cache[i] == 0) {
			fs->table_cache[i] = TV_FINAL;
			return i;
		}
	}
	return TV_CANT_ALLOC;
}

Result extend(FileSystem* restrict fs, ClusterLocation* restrict cursor) {
	assert(fs->table_cache[*cursor] == TV_FINAL);

	ClusterLocation nc = allocate(fs);
	if(nc == TV_CANT_ALLOC) {
		return 1;
	}
	fs->table_cache[*cursor] = nc;
	*cursor = nc;
	return 0;
}

Result init_fs_file(FileSystem* restrict fs, char* restrict path, uint16_t clusters_count) {
	if(clusters_count == 0) {
		return 1;
	}

	fs->clusters_count = clusters_count;

	memset(fs->table_cache, 0, TABLE_SIZE);
	fs->table_cache[0] = TV_FINAL;

	uint8_t root[CLUSTER_SIZE];
	memset(root, 0, CLUSTER_SIZE);

	fs->file = fopen(path, "wb+");

	if(fs->file == NULL) {
		return 1;
	}

	fwrite(fs->table_cache, 1, TABLE_SIZE, fs->file);
	fwrite(root, 1, CLUSTER_SIZE, fs->file);

	fseek(fs->file, ROOT_OFFSET + CLUSTER_SIZE * clusters_count - 1, SEEK_SET);
	fputc(0, fs->file);

	return ferror(fs->file);
}

Result open_fs_file(FileSystem* restrict fs, char* restrict path) {
	fs->file = fopen(path, "ab+");

	if(fs->file == NULL) {
		return 1;
	}

	fseek(fs->file, 0, SEEK_END);
	long file_length = ftell(fs->file);
	if (file_length < ROOT_OFFSET + CLUSTER_SIZE) {
		return 1;
	}
	fs->clusters_count = max(UINT16_MAX, (file_length - ROOT_OFFSET) / CLUSTER_SIZE);

	rewind(fs->file);
	fread(fs->table_cache, sizeof(ClusterLocation), MAX_CLUSTERS, fs->file);

	return ferror(fs->file);
}

void get_root(FileSystem* restrict fs, DirCursor* restrict out) {
	out->current_cluster = 0;
}

// TODO: restrict: а если target из dir?
OptionalResult resolve(FileSystem* restrict fs, DirCursor* restrict current, DirEntry* restrict result, uint8_t* restrict target) {
	uint8_t buffer[CLUSTER_SIZE];
	result->current_cluster = current->current_cluster;
	while(1) {
		read(fs, result->current_cluster, buffer);
		if(ferror(fs->file)) {
			return OPTIONAL_IO_ERROR;
		}
		size_t offset = 0;
		while(offset != CLUSTER_SIZE) {
			if (buffer[offset+OFFSET_NAME] == 0) { // Empty file name
				return OPTIONAL_STRUCTURE_ERROR;
			}
			if(memcmp(target, buffer+offset+OFFSET_NAME, MAX_FILE_NAME) == 0) {
				memcpy(result->meta, buffer+offset, FILE_META);
				return OPTIONAL_OK;
			}
			offset += FILE_META;
		}
		if(fs->table_cache[result->current_cluster] == TV_FINAL) {
			return OPTIONAL_STRUCTURE_ERROR;
		}
		result->current_cluster = fs->table_cache[result->current_cluster];
	}
}

OptionalResult create_file(FileSystem* restrict fs, DirCursor* restrict current, DirEntry* restrict target) {
	uint8_t buffer[CLUSTER_SIZE];
	target->current_cluster = current->current_cluster;
	while(1) {
		read(fs, target->current_cluster, buffer);
		if(ferror(fs->file)) {
			return OPTIONAL_IO_ERROR;
		}
		size_t offset = 0;
		while(1) {
			if (buffer[offset+OFFSET_NAME] == 0) { // Empty file name
				ClusterLocation first_cluster = allocate(fs);
				if(first_cluster == TV_CANT_ALLOC) {
					return OPTIONAL_STRUCTURE_ERROR;
				}

				write_u16(target->meta+OFFSET_CLUSTER, first_cluster);
				memcpy(buffer+offset, target->meta, FILE_META);
				write(fs, target->current_cluster, buffer);

				memset(buffer, 0, CLUSTER_SIZE);
				target->current_cluster = first_cluster;
				write(fs, target->current_cluster, buffer);
			}
			if(memcmp(target->meta+OFFSET_NAME, buffer+offset+OFFSET_NAME, MAX_FILE_NAME) == 0) {
				memcpy(target->meta, buffer+offset, FILE_META);
				return OPTIONAL_OK;
			}
			offset += FILE_META;
			if (offset == CLUSTER_SIZE) {
				if(target->current_cluster == TV_FINAL) {
					if(extend(fs, &target->current_cluster)) {
						return OPTIONAL_STRUCTURE_ERROR;
					}
					memset(buffer, 0, CLUSTER_SIZE);
					offset = 0;
					continue;
				} else {
					target->current_cluster = fs->table_cache[target->current_cluster];
					break;
				}
			}
		}
	}
}

typedef struct {
	ClusterOffset metaFileSize;
	ClusterOffset offset;
	ClusterLocation first;
	ClusterLocation current;
} FileIO;

// TODO: restrict buffer?
void write_to_file(FileSystem* restrict fs, FileIO* restrict file, FileCursor location, uint8_t* restrict buffer, size_t size) {
	while(size != 0) {
		ClusterOffset left = CLUSTER_SIZE - file->offset;
		ClusterOffset to_write = min(size, left);
		fseek(fs->file, ROOT_OFFSET + file->current * CLUSTER_SIZE + file->offset, SEEK_SET);
		fread(buffer, 1, to_write, fs->file);
		file->offset = (file->offset + to_write) % CLUSTER_SIZE;
		buffer += to_write;
		size -= to_write;
		if(to_write == left) {
			// Выделять память под следующий блок, даже если нечего записывать
			if(fs->table_cache[file->current] == TV_FINAL) {
				if(extend(fs, &file->current)) {
					return OPTIONAL_STRUCTURE_ERROR;
				}
			} else {
				file->current = fs->table_cache[file->current];
			}
		}
	}
	return OPTIONAL_OK;
}

// TODO: restrict buffer?
OptionalResult seek(FileSystem* restrict fs, FileIO* restrict file, FileCursor location) {
	file->current = file->first;
	for(ClusterLocation i = location / CLUSTER_SIZE; i != 0; i--) {
		file->current = fs->table_cache[file->current];
		if(file->current == TV_FINAL) {
			return OPTIONAL_STRUCTURE_ERROR;
		}
	}
	file->offset = location % CLUSTER_SIZE;
}

// TODO: restrict buffer?
OptionalResult read_from(FileSystem* restrict fs, FileIO* restrict file, FileCursor location, uint8_t* restrict buffer, size_t size) {
	while(1) {
		if(size == 0) {
			return OPTIONAL_OK;
		}
		ClusterLocation next = fs->table_cache[file->current];
		ClusterOffset length = CLUSTER_SIZE;
		if(next == TV_FINAL) {
			length = min(length, file->metaFileSize);
		}
		ClusterOffset left = length - file->offset;
		ClusterOffset to_read = min(size, left);
		fseek(fs->file, ROOT_OFFSET + file->current * CLUSTER_SIZE + file->offset, SEEK_SET);
		fread(buffer, 1, to_read, fs->file);
		file->offset = (file->offset + to_read) % CLUSTER_SIZE;
		buffer += to_read;
		size -= to_read;
		if(to_read == left) {
			if(file->current == TV_FINAL && size != 0) {
				return OPTIONAL_STRUCTURE_ERROR;
			}
			file->current = next;
		}
	}
}

/*
file
dir {
	file_iter
		[file_idx, first_cluster, current_cluster]
		next_file
	open_dir
	open_file
	prev_dir
	make_file
	delete_file
}
file {
	[parent_dir, length, first_cluster, current_cluster, current_position]
	seek
	read_bytes
	write_bytes
}
*/

void close_fs_file() {
	fclose(file);
}

void open_dir();
void rem_dir();
void make_dir();
void make_file();
void rem_file();
void open_file();