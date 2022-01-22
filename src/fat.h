#pragma once

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

void read(FileSystem* restrict fs, ClusterLocation cluster, uint8_t* restrict buffer);
void write(FileSystem* restrict fs, ClusterLocation cluster, uint8_t* restrict buffer);
uint16_t read_u16(uint8_t* restrict ptr);
void write_u16(uint8_t* restrict ptr, uint16_t value);
ClusterLocation get_cluster(DirEntry* restrict entry);
ClusterOffset get_meta_size(DirEntry* restrict entry);
uint8_t is_folder(DirEntry* restrict entry);
FileCursor get_file_size(FileSystem* restrict fs, DirEntry* restrict entry);
void init_meta(DirEntry* restrict entry, uint8_t is_folder, uint8_t* restrict name);
ClusterLocation allocate(FileSystem* restrict fs);
Result extend(FileSystem* restrict fs, ClusterLocation* restrict cursor);
Result init_fs_file(FileSystem* restrict fs, char* restrict path, uint16_t clusters_count);
Result open_fs_file(FileSystem* restrict fs, char* restrict path);
void get_root(FileSystem* restrict fs, DirCursor* restrict out);
OptionalResult resolve(FileSystem* restrict fs, DirCursor* restrict current, DirEntry* restrict result, uint8_t* restrict target);
OptionalResult create_file(FileSystem* restrict fs, DirCursor* restrict current, DirEntry* restrict target);
OptionalResult write_to_file(FileSystem* restrict fs, FileIO* restrict file, FileCursor location, uint8_t* restrict buffer, size_t size);
void read_from(FileSystem* restrict fs, FileIO* restrict file, FileCursor location, uint8_t* restrict buffer, size_t size);

