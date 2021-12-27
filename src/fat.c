#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

typedef uint16_t CursorLocation;

typedef uint8_t HasFailed;

enum {
	CLUSTER_SIZE = 4*1024,
	MAX_CLUSTERS = 2048,
	TABLE_SIZE = MAX_CLUSTERS*sizeof(CursorLocation),
	ROOT_OFFSET = TABLE_SIZE,
	
	FILE_META = 64,
	MAX_FILE_NAME = FILE_META-1,
	FOLDERS_PER_CLUSTER = CLUSTER_SIZE / FILE_META,

	TV_EMPTY = 0x00,
	TV_FINAL = 0xFF,
};

_STATIC_ASSERT(TABLE_SIZE % CLUSTER_SIZE == 0);
_STATIC_ASSERT(FOLDERS_PER_CLUSTER < UINT8_MAX);
_STATIC_ASSERT(MAX_CLUSTERS < UINT16_MAX);

typedef struct {
	FILE* file;
	uint8_t table_cache[TABLE_SIZE];
	uint16_t clusters_count;
} FileSystem;

typedef struct {
	CursorLocation current_cluster;
} FileCursor;

typedef struct {
	CursorLocation current_cluster;
	uint8_t next_folder;
} DirIter;

HasFailed init_fs_file(FileSystem* restrict fs, char* restrict path, uint16_t clusters_count) {
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

HasFailed open_fs_file(FileSystem* restrict fs, char* restrict path) {
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
	fread(fs->table_cache, 1, TABLE_SIZE, fs->file);

	return ferror(fs->file);
}

FileCursor get_root(FileSystem* restrict fs, FileCursor* restrict cluster) {
	fseek(fs->file, ROOT_OFFSET, SEEK_SET);
	fread()
}

/*
file
dir {
	file_iter
		next_file
	open_dir
	open_file
	prev_dir
	make_file
	delete_file
}
file {
	[length, first_cluster, current_cluster, current_position]
	seek
	read_bytes
	write_bytes
}
*/

void close_fs_file() {
	fclose(file);
}

void get_root();
void open_dir();
void rem_dir();
void make_dir();
void make_file();
void rem_file();
void open_file();