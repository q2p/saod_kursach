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
	MAX_CLUSTERS = 8*1024,
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
	uint8_t meta[FILE_META];
	ClusterLocation current_cluster;
	ClusterLocation current_offset;
} DirEntry;

typedef struct {
	ClusterLocation current_cluster;
	uint8_t next_folder;
} DirIter;

typedef struct {
	ClusterOffset metaFileSize;
	ClusterOffset offset;
	ClusterLocation first;
	ClusterLocation current;
	FileCursor metaFileSizeLocation;
} FileIO;

uint8_t is_folder(DirEntry* entry);

FileCursor get_file_size(FileSystem* fs, DirEntry* entry);

// TODO: name должен быть padded
void init_meta(DirEntry* entry, uint8_t is_folder, uint8_t* name);

Result init_fs_file(FileSystem* fs, char* path, uint16_t clusters_count);

Result open_fs_file(FileSystem* fs, char* path);

void get_root(FileSystem* fs, DirCursor* out);

// TODO: restrict: а если target из dir?
OptionalResult resolve(FileSystem* fs, DirCursor* current, DirEntry* result, uint8_t* target);

OptionalResult create_file(FileSystem* fs, DirCursor* current, DirEntry* target);

Result open_file(FileSystem* fs, DirEntry* entry, FileIO* result);

// TODO: buffer?
OptionalResult set_length(FileSystem* fs, FileIO* file, FileCursor length);

// TODO: buffer?
OptionalResult seek(FileSystem* fs, FileIO* file, FileCursor location);

// TODO: buffer?
OptionalResult write_to_file(FileSystem* fs, FileIO* file, FileCursor location, uint8_t* buffer, size_t size);

// TODO: buffer?
OptionalResult read_from_file(FileSystem* fs, FileIO* file, FileCursor location, uint8_t* buffer, size_t size);

Result close_file(FileSystem* fs, FileIO* file);