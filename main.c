#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

typedef uint16_t ClusterLocation;
typedef uint16_t ClusterOffset;
typedef uint32_t FileCursor;

typedef uint8_t Result;
typedef uint8_t OptionalResult;

enum {
	CLUSTER_SIZE = 4*1024,
	MAX_CLUSTERS = 8*1024,
	ROOT_OFFSET = MAX_CLUSTERS*sizeof(ClusterLocation),
	
	FILE_META = 64,
	OFFSET_SIZE = 0,
	OFFSET_CLUSTER = sizeof(ClusterOffset),
	OFFSET_NAME = sizeof(ClusterOffset) + sizeof(ClusterLocation),
	FILE_NAME_BUFFER = FILE_META - OFFSET_NAME,
	MAX_FILE_NAME = FILE_NAME_BUFFER - 1,
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
_STATIC_ASSERT(MAX_CLUSTERS*sizeof(ClusterLocation) % CLUSTER_SIZE == 0);
_STATIC_ASSERT(CLUSTER_SIZE % FILE_META == 0);
_STATIC_ASSERT(FILES_PER_CLUSTER < UINT8_MAX);
_STATIC_ASSERT(FS_FOLDER >= CLUSTER_SIZE);
_STATIC_ASSERT(MAX_CLUSTERS < UINT16_MAX);

typedef struct {
	FILE* file;
	uint16_t clusters_count;
	ClusterLocation table_cache[MAX_CLUSTERS];
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
	ClusterLocation current_offset;
	uint8_t buffer[CLUSTER_SIZE];
} DirIter;

typedef struct {
	ClusterOffset metaFileSize;
	ClusterOffset offset;
	ClusterLocation first;
	ClusterLocation current;
	FileCursor metaFileSizeLocation;
} FileIO;


// TODO: поставить ferror, где забыл

void read(FileSystem* fs, ClusterLocation cluster, uint8_t* buffer) {
	fseek(fs->file, ROOT_OFFSET + cluster * CLUSTER_SIZE, SEEK_SET);
	fread(buffer, 1, CLUSTER_SIZE, fs->file);
}

void write(FileSystem* fs, ClusterLocation cluster, uint8_t* buffer) {
	fseek(fs->file, ROOT_OFFSET + cluster * CLUSTER_SIZE, SEEK_SET);
	fwrite(buffer, 1, CLUSTER_SIZE, fs->file);
}

uint16_t read_u16(uint8_t* ptr) {
	return ptr[0] | ptr[1] << 8;
}

void write_u16(uint8_t* ptr, uint16_t value) {
	ptr[0] = value;
	ptr[1] = value >> 8;
}

ClusterLocation get_cluster(DirEntry* entry) {
	return read_u16(entry->meta + OFFSET_CLUSTER);
}

ClusterOffset get_meta_size(DirEntry* entry) {
	return read_u16(entry->meta + OFFSET_SIZE);
}

uint8_t is_folder(DirEntry* entry) {
	return get_meta_size(entry) == FS_FOLDER;
}

FileCursor get_file_size(FileSystem* fs, DirEntry* entry) {
	FileCursor ret = (FileCursor) get_meta_size(entry);
	ClusterLocation cluster = get_cluster(entry);
	while(fs->table_cache[cluster] != TV_FINAL) {
		cluster = fs->table_cache[cluster];
		ret += CLUSTER_SIZE;
	}
	return ret;
}

uint8_t* get_file_name(DirEntry* entry) {
	return entry->meta+OFFSET_NAME;
}

void init_meta(DirEntry* entry, uint8_t is_folder, uint8_t* name) {
	write_u16(entry->meta + OFFSET_SIZE, is_folder ? FS_FOLDER : 0);
	memset(entry->meta + OFFSET_NAME, 0, FILE_NAME_BUFFER);
	strcpy(entry->meta + OFFSET_NAME, name);
}

ClusterLocation allocate(FileSystem* fs) {
	for(size_t i = 1; i != MAX_CLUSTERS; i++) {
		if (fs->table_cache[i] == 0) {
			fs->table_cache[i] = TV_FINAL;
			return i;
		}
	}
	return TV_CANT_ALLOC;
}

Result extend(FileSystem* fs, ClusterLocation* cursor) {
	assert(fs->table_cache[*cursor] == TV_FINAL);

	ClusterLocation nc = allocate(fs);
	if(nc == TV_CANT_ALLOC) {
		return 1;
	}
	fs->table_cache[*cursor] = nc;
	*cursor = nc;
	return 0;
}

Result init_fs_file(FileSystem* fs, char* path, uint16_t clusters_count) {
	if(clusters_count == 0) {
		return 1;
	}
	fs->clusters_count = clusters_count;

	memset(fs->table_cache, 0, sizeof(fs->table_cache));
	fs->table_cache[0] = TV_FINAL;

	uint8_t root[CLUSTER_SIZE];
	memset(root, 0, CLUSTER_SIZE);

	fs->file = fopen("new_fs", "wb+");

	if(fs->file == NULL) {
		return 1;
	}

	fwrite(fs->table_cache, 1, sizeof(fs->table_cache), fs->file);
	fwrite(root, 1, CLUSTER_SIZE, fs->file);

	fseek(fs->file, ROOT_OFFSET + CLUSTER_SIZE * clusters_count - 1, SEEK_SET);
	fputc(0, fs->file);

	return ferror(fs->file);
}

Result open_fs_file(FileSystem* fs, char* path) {
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

	fseek(fs->file, 0, SEEK_SET);
	fread(fs->table_cache, sizeof(ClusterLocation), MAX_CLUSTERS, fs->file);

	return ferror(fs->file);
}

void get_root(FileSystem* fs, DirCursor* out) {
	out->current_cluster = 0;
}

// TODO: restrict: а если target из dir?
OptionalResult resolve(FileSystem* fs, DirCursor* current, DirEntry* result, uint8_t* target) {
	uint8_t buffer[CLUSTER_SIZE];
	result->current_cluster = current->current_cluster;
	while(1) {
		read(fs, result->current_cluster, buffer);
		if(ferror(fs->file)) {
			return OPTIONAL_IO_ERROR;
		}
		result->current_offset = 0;
		while(result->current_offset != CLUSTER_SIZE) {
			if (buffer[result->current_offset+OFFSET_NAME] == 0) { // Empty file name
				return OPTIONAL_STRUCTURE_ERROR;
			}
			if(memcmp(target, buffer+result->current_offset+OFFSET_NAME, FILE_NAME_BUFFER) == 0) {
				memcpy(result->meta, buffer+result->current_offset, FILE_META);
				return OPTIONAL_OK;
			}
			result->current_offset += FILE_META;
		}
		if(fs->table_cache[result->current_cluster] == TV_FINAL) {
			return OPTIONAL_STRUCTURE_ERROR;
		}
		result->current_cluster = fs->table_cache[result->current_cluster];
	}
}

OptionalResult create_file(FileSystem* fs, DirCursor* current, DirEntry* target) {
	uint8_t buffer[CLUSTER_SIZE];
	target->current_cluster = current->current_cluster;
	while(1) {
		read(fs, target->current_cluster, buffer);
		if(ferror(fs->file)) {
			return OPTIONAL_IO_ERROR;
		}
		target->current_offset = 0;
		while(1) {
			if (buffer[target->current_offset+OFFSET_NAME] == 0) { // Empty file name
				ClusterLocation first_cluster = allocate(fs);
				if(first_cluster == TV_CANT_ALLOC) {
					return OPTIONAL_STRUCTURE_ERROR;
				}

				write_u16(target->meta+OFFSET_CLUSTER, first_cluster);
				memcpy(buffer+target->current_offset, target->meta, FILE_META);
				write(fs, target->current_cluster, buffer);

				memset(buffer, 0, CLUSTER_SIZE);
				write(fs, first_cluster, buffer);
				return OPTIONAL_OK;
			}
			if(memcmp(target->meta+OFFSET_NAME, buffer+target->current_offset+OFFSET_NAME, FILE_NAME_BUFFER) == 0) {
				return OPTIONAL_STRUCTURE_ERROR;
			}
			target->current_offset += FILE_META;
			if (target->current_offset == CLUSTER_SIZE) {
				if(target->current_cluster == TV_FINAL) {
					if(extend(fs, &target->current_cluster)) {
						return OPTIONAL_STRUCTURE_ERROR;
					}
					memset(buffer, 0, CLUSTER_SIZE);
					target->current_offset = 0;
					continue;
				} else {
					target->current_cluster = fs->table_cache[target->current_cluster];
					break;
				}
			}
		}
	}
}

OptionalResult delete_file(FileSystem* fs, DirCursor* parent, DirEntry* target) {
	// TODO: проверить на баги
	ClusterLocation current = get_cluster(target);
	while(1) {
		ClusterLocation next = fs->table_cache[current];
		fs->table_cache[current] = TV_EMPTY;
		if(next == TV_FINAL) {
			break;
		}
		current = next;
	}

	uint8_t buffer[CLUSTER_SIZE];
	size_t offset = 0;
	ClusterLocation prev = TV_EMPTY;
	current = parent->current_cluster;
	read(fs, current, buffer);
	if(ferror(fs->file)) {
		return OPTIONAL_IO_ERROR;
	}
	while(1) {
		if (buffer[offset+OFFSET_NAME] == 0) { // Empty file name
			offset -= FILE_META;
			fseek(fs->file, ROOT_OFFSET + target->current_cluster * CLUSTER_SIZE + target->current_offset, SEEK_SET);
			fwrite(buffer+offset, 1, FILE_META, fs->file);
			if(ferror(fs->file)) {
				return OPTIONAL_IO_ERROR;
			}
			if(offset == 0) {
				if (prev == TV_EMPTY) {
					buffer[offset+OFFSET_NAME] = 0;
					write(fs, current, buffer);
				} else {
					fs->table_cache[prev] = TV_FINAL;
					fs->table_cache[current] = TV_EMPTY;
				}
			} else {
				buffer[offset+OFFSET_NAME] = 0;
				write(fs, current, buffer);
			}
			return OPTIONAL_OK;
		}
		offset += FILE_META;
		if (offset == CLUSTER_SIZE) {
			if(fs->table_cache[current] == TV_FINAL) {
				offset -= FILE_META;
				fseek(fs->file, ROOT_OFFSET + target->current_cluster * CLUSTER_SIZE + target->current_offset, SEEK_SET);
				fwrite(buffer+offset, 1, FILE_META, fs->file);
				if(ferror(fs->file)) {
					return OPTIONAL_IO_ERROR;
				}
				buffer[offset+OFFSET_NAME] = 0;
				write(fs, current, buffer);
				memset(buffer, 0, CLUSTER_SIZE);
				return OPTIONAL_OK;
			} else {
				prev = current;
				current = fs->table_cache[current];
				read(fs, current, buffer);
				if(ferror(fs->file)) {
					return OPTIONAL_IO_ERROR;
				}
			}
		}
	}
}

void open_dir(FileSystem* fs, DirEntry* entry, DirCursor* result) {
	assert(is_folder(entry));
	result->current_cluster = get_cluster(entry);
}

void open_file(FileSystem* fs, DirEntry* entry, FileIO* result) {
	assert(!is_folder(entry));
	result->offset = 0;
	result->current = result->first = get_cluster(entry);
	result->metaFileSize = get_meta_size(entry);
	result->metaFileSizeLocation = ROOT_OFFSET + entry->current_cluster*CLUSTER_SIZE + entry->current_offset + OFFSET_SIZE;
}

// TODO: buffer?
OptionalResult set_length(FileSystem* fs, FileIO* file, FileCursor length) {
	ClusterLocation current = file->first;
	ClusterLocation remaining = length / CLUSTER_SIZE;
	file->metaFileSize = length % CLUSTER_SIZE;
	file->current = file->first;
	file->offset = 0;
	while(1) {
		if(fs->table_cache[current] == TV_FINAL) {
			if(extend(fs, &current)) {
				return OPTIONAL_STRUCTURE_ERROR;
			}
		} else {
			current = fs->table_cache[current];
		}
		if (remaining == 0) {
			break;
		}
		remaining--;
	}
	ClusterLocation next = fs->table_cache[current];
	fs->table_cache[current] = TV_FINAL;
	current = next;
	while(current != TV_FINAL) {
		next = fs->table_cache[current];
		fs->table_cache[current] = TV_EMPTY;
		current = next;
	}
	return OPTIONAL_OK;
}

// TODO: buffer?
OptionalResult seek(FileSystem* fs, FileIO* file, FileCursor location) {
	file->current = file->first;
	for(ClusterLocation i = location / CLUSTER_SIZE; i != 0; i--) {
		file->current = fs->table_cache[file->current];
		if(file->current == TV_FINAL) {
			return OPTIONAL_STRUCTURE_ERROR;
		}
	}
	file->offset = location % CLUSTER_SIZE;
	return OPTIONAL_OK;
}

// TODO: buffer?
OptionalResult write_to_file(FileSystem* fs, FileIO* file, uint8_t* buffer, size_t size) {
	while(size != 0) {
		ClusterOffset left = CLUSTER_SIZE - file->offset;
		ClusterOffset to_write = min(size, left);
		fseek(fs->file, ROOT_OFFSET + file->current * CLUSTER_SIZE + file->offset, SEEK_SET);
		fwrite(buffer, 1, to_write, fs->file);
		if(ferror(fs->file)) {
			return OPTIONAL_IO_ERROR;
		}
		file->offset = (file->offset + to_write) % CLUSTER_SIZE;
		buffer += to_write;
		size -= to_write;
		if(fs->table_cache[file->current] == TV_FINAL && file->offset > file->metaFileSize) {
			file->metaFileSize = file->offset;
		}
		if(to_write == left) {
			// Выделять память под следующий блок, даже если нечего записывать
			if(fs->table_cache[file->current] == TV_FINAL) {
				file->metaFileSize = 0;
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

// TODO: buffer?
Result read_from_file(FileSystem* fs, FileIO* file, uint8_t* buffer, size_t size) {
	while(1) {
		if(size == 0) {
			return 0;
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
		if(ferror(fs->file)) {
			return 1;
		}
		file->offset = (file->offset + to_read) % CLUSTER_SIZE;
		buffer += to_read;
		size -= to_read;
		if(to_read == left) {
			if(next == TV_FINAL) {
				return 0;
			}
			file->current = next;
		}
	}
}

Result close_file(FileSystem* fs, FileIO* file) {
	uint8_t buffer[sizeof(ClusterOffset)];
	write_u16(buffer, file->metaFileSize);
	fseek(fs->file, file->metaFileSizeLocation, SEEK_SET);
	fwrite(buffer, 1, sizeof(ClusterOffset), fs->file);
	return ferror(fs->file);
}

void dir_iter(FileSystem* fs, DirCursor* current, DirIter* iter) {
	iter->current_cluster = current->current_cluster;
	iter->current_offset = 0;
	read(fs, iter->current_cluster, iter->buffer);
}

OptionalResult dir_iter_next(FileSystem* fs, DirIter* iter, DirEntry* next) {
	if(iter->current_offset == CLUSTER_SIZE) {
		if(fs->table_cache[iter->current_cluster] == TV_FINAL) {
			return OPTIONAL_STRUCTURE_ERROR;
		}
		iter->current_cluster = fs->table_cache[iter->current_cluster];
		read(fs, iter->current_cluster, iter->buffer);
		if(ferror(fs->file)) {
			return OPTIONAL_IO_ERROR;
		}
		iter->current_offset = 0;
	}
	next->current_cluster = iter->current_cluster;
	if (iter->buffer[iter->current_offset+OFFSET_NAME] == 0) { // Empty file name
		return OPTIONAL_STRUCTURE_ERROR;
	}
	memcpy(next->meta, iter->buffer+iter->current_offset, FILE_META);
	next->current_cluster = iter->current_cluster;
	next->current_offset = iter->current_offset;
	iter->current_offset += FILE_META;
	return OPTIONAL_OK;
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

Result close_fs_file(FileSystem* fs) {
	fseek(fs->file, 0, SEEK_SET);
	fwrite(fs->table_cache, 1, sizeof(fs->table_cache), fs->file);
	fflush(fs->file);
	fclose(fs->file);
	return ferror(fs->file);
}

void rem_dir();
void rem_file();

uint8_t LUT[256];

void string_to_lower(uint8_t *string) {
	for(uint8_t *p = string; *p; ++p)
		*p = *p > 0x40 && *p < 0x5b ? *p | 0x60 : *p;
}

void split(uint8_t* before, uint8_t** after, uint8_t delimiter) {
	uint8_t* p = before;
	while(1) {
		if(*p == '\0') {
			*after = p;
			break;
		}
		if(*p == delimiter) {
			*p = '\0';
			*after = p+1;
			break;
		}
		p++;
	}
}

void init_table() {
	for (uint16_t c = 0; c != 256; c++) {
		LUT[c] = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
	}
}

void trim_untill_newline(uint8_t *string) {
	while(*string) {
		if (*string == '\n') {
			*string = '\0';
			break;
		}
		string++;
	}
}

void trim_untill_slash(uint8_t *string) {
	while(*string) {
		if (*string == '/') {
			*string = '\0';
			break;
		}
		string++;
	}
}

enum {
	INPUT_BUFFER = 4096,
	MAX_DEPTH = 256,
	FS_SIZE = 16*1024*1024,
	IO_BUFFER = 4096,
	DIR_STRING_BUFFER = 16*1024
};

const uint8_t* MESSAGE_IO_ERROR = "I/O Error has occured.\n";
const uint8_t* MESSAGE_OUT_OF_SPACE = "Not enough space.\n";
const uint8_t* MESSAGE_NOT_FOUND = "File not found.\n";
const uint8_t* MESSAGE_IS_DIR = "File is a directory.\n";
const uint8_t* MESSAGE_IS_NOT_DIR = "File is not a directory.\n";
const uint8_t* MESSAGE_IS_NOT_FILE = "Not a file.\n";
const uint8_t* MESSAGE_FILE_ALREADY_EXISTS = "File already exists.\n";
const uint8_t* MESSAGE_FILENAME_IS_LONG = "Filename is too long.\n";
const uint8_t* MESSAGE_FILENAME_ILLEGAL_SYMBOLS = "Filename contain illegal symbols.\n";
const uint8_t* MESSAGE_FS_CANT_INIT = "Can't init FS.\n";
const uint8_t* MESSAGE_FS_CANT_MOUNT = "Can't mount FS.\n";
const uint8_t* MESSAGE_UNKNOWN_COMMAND = "Unknown command.\n";

Result verify_filename(uint8_t* filename) {
	size_t len = strlen(filename);
	// TODO: forbid ..
	if (len > MAX_FILE_NAME) {
		printf(MESSAGE_FILENAME_IS_LONG);
		return 1;
	}
	while(*filename) {
		if (!LUT[*(filename++)]) {
			printf(MESSAGE_FILENAME_ILLEGAL_SYMBOLS);
			return 1;
		}
	}
	return 0;
}

Result init_or_mount(uint8_t* input_buffer, FileSystem* fs) {
	while(1) {
		printf("init or mount?\n");
		fgets(input_buffer, INPUT_BUFFER, stdin);
		trim_untill_newline(input_buffer);
		uint8_t* path;
		split(input_buffer, &path, ' ');
		string_to_lower(input_buffer);
		if (strcmp(input_buffer, "init") == 0) {
			if (init_fs_file(fs, path, FS_SIZE / CLUSTER_SIZE)) {
				printf(MESSAGE_FS_CANT_INIT);
				return 1;
			}
			return 0;
		} else if (strcmp(input_buffer, "mount") == 0) {
			if (open_fs_file(fs, path)) {
				printf(MESSAGE_FS_CANT_MOUNT);
				return 1;
			}
			return 0;
		} else if (strcmp(input_buffer, "exit") == 0) {
			return 1;
		} else {
			printf(MESSAGE_UNKNOWN_COMMAND);
		}
	}
}

Result action_write(uint8_t* input_buffer, FileSystem* fs, DirCursor* dir, uint8_t* after_command) {
	if(verify_filename(after_command)) {
		return 0;
	}
	uint8_t file_name[FILE_NAME_BUFFER];
	memset(file_name, 0, FILE_NAME_BUFFER);
	strcpy(file_name, after_command);
	DirEntry file;
	FileIO file_io;
	switch (resolve(fs, dir, &file, file_name)) {
		case OPTIONAL_OK:
			if (is_folder(&file)) {
				printf(MESSAGE_IS_DIR);
				return 0;
			}
			break;
		case OPTIONAL_STRUCTURE_ERROR:
			init_meta(&file, 0, file_name);
			switch (create_file(fs, dir, &file)) {
				case OPTIONAL_OK:
					break;
				case OPTIONAL_STRUCTURE_ERROR:
					printf(MESSAGE_OUT_OF_SPACE);
					return 0;
				case OPTIONAL_IO_ERROR:
					printf(MESSAGE_IO_ERROR);
					return 1;
			}
			break;
		case OPTIONAL_IO_ERROR:
			printf(MESSAGE_IO_ERROR);
			return 1;
	}
	open_file(fs, &file, &file_io);
	while (1) {
		fgets(input_buffer, INPUT_BUFFER, stdin);
		if (input_buffer[0] == '\n') {
			break;
		}
		switch (write_to_file(fs, &file_io, input_buffer, strlen(input_buffer))) {
			case OPTIONAL_OK:
				break;
			case OPTIONAL_STRUCTURE_ERROR:
				printf(MESSAGE_OUT_OF_SPACE);
				goto close_file;
			case OPTIONAL_IO_ERROR:
				printf(MESSAGE_IO_ERROR);
				return 1;
		}
	}
	close_file:;
	if (close_file(fs, &file_io)) {
		printf(MESSAGE_IO_ERROR);
		return 1;
	}
	return 0;
}
Result action_mkdir(FileSystem* fs, DirCursor* current_dir, uint8_t* dir_name) {
	if(verify_filename(dir_name)) {
		return 0;
	}
	DirEntry directory;
	uint8_t name_buffer[FILE_NAME_BUFFER];
	memset(name_buffer, 0, FILE_NAME_BUFFER);
	strcpy(name_buffer, dir_name);
	switch (resolve(fs, current_dir, &directory, name_buffer)) {
		case OPTIONAL_OK:
			printf(MESSAGE_FILE_ALREADY_EXISTS);
			return 0;
		case OPTIONAL_STRUCTURE_ERROR:
			break;
		case OPTIONAL_IO_ERROR:
			printf(MESSAGE_IO_ERROR);
			return 1;
	}
	init_meta(&directory, 1, name_buffer);
	switch (create_file(fs, current_dir, &directory)) {
		case OPTIONAL_OK:
			return 0;
		case OPTIONAL_STRUCTURE_ERROR:
			printf(MESSAGE_OUT_OF_SPACE);
			return 0;
		case OPTIONAL_IO_ERROR:
			printf(MESSAGE_IO_ERROR);
			return 1;
	}
}
Result action_read(FileSystem* fs, DirCursor* current_dir, uint8_t* file_name) {
	DirEntry file;
	uint8_t file_name_buffer[FILE_NAME_BUFFER];
	memset(file_name_buffer, 0, FILE_NAME_BUFFER);
	strcpy(file_name_buffer, file_name);
	switch (resolve(fs, current_dir, &file, file_name_buffer)) {
		case OPTIONAL_OK:
			break;
		case OPTIONAL_STRUCTURE_ERROR:
			printf(MESSAGE_NOT_FOUND);
			return 0;
		case OPTIONAL_IO_ERROR:
			printf(MESSAGE_IO_ERROR);
			return 1;
	}
	if (is_folder(&file)) {
		printf(MESSAGE_IS_DIR);
		return 0;
	}
	FileIO file_io;
	uint8_t buffer[IO_BUFFER];
	FileCursor size = get_file_size(fs, &file);
	FileCursor counter = 0;
	open_file(fs, &file, &file_io);
	printf("File length: %d.\nFile contents:\n", size);
	while (counter < size) {
		size_t to_read = min(IO_BUFFER-1, size - counter);
		if (read_from_file(fs, &file_io, buffer, to_read)) {
			printf(MESSAGE_IO_ERROR);
			return 1;
		}
		buffer[to_read] = '\0';
		printf("%s", buffer);
		counter += to_read;
	}
	if(close_file(fs, &file_io)) {
		printf(MESSAGE_IO_ERROR);
		return 1;
	}
	return 0;
}
Result action_dir(FileSystem* fs, DirCursor* current_dir, uint8_t* file_name) {
	DirIter iter;
	DirEntry entry;

	dir_iter(fs, current_dir, &iter);

	while (1) {
		switch (dir_iter_next(fs, &iter, &entry)) {
			case OPTIONAL_OK:
				printf("%s - ", get_file_name(&entry));
				if (is_folder(&entry)) {
					printf("DIR\n");
				} else {
					printf("FILE - %d bytes\n", get_file_size(fs, &entry));
				}
				break;
			case OPTIONAL_STRUCTURE_ERROR:
				return 0;
			case OPTIONAL_IO_ERROR:
				printf(MESSAGE_IO_ERROR);
				return 1;
		}
	}
}
Result action_export(FileSystem* fs, DirCursor* current_dir, uint8_t* after_command) {
	uint8_t *internal = after_command;
	uint8_t *external;
	split(internal, &external, ' ');

	uint8_t file_name[FILE_NAME_BUFFER];
	memset(file_name, 0, FILE_NAME_BUFFER);
	strcpy(file_name, internal);
	DirEntry file;
	switch (resolve(fs, current_dir, &file, file_name)) {
		case OPTIONAL_OK:
			break;
		case OPTIONAL_STRUCTURE_ERROR:
			printf(MESSAGE_NOT_FOUND);
			return 0;
		case OPTIONAL_IO_ERROR:
			printf(MESSAGE_IO_ERROR);
			return 1;
	}
	FileIO internal_file;
	FileCursor size = get_file_size(fs, &file);
	FileCursor cursor = 0;
	FILE *external_file = fopen(external, "wb");
	if (external_file == NULL) {
		printf(MESSAGE_IO_ERROR);
		return 0;
	}
	open_file(fs, &file, &internal_file);
	while(cursor < size) {
		size_t to_transfer = min(size - cursor, IO_BUFFER);
		uint8_t buffer[IO_BUFFER];
		if (read_from_file(fs, &internal_file, buffer, to_transfer)) {
			fclose(external_file);
			printf(MESSAGE_IO_ERROR);
			return 1;
		}
		fwrite(buffer, 1, to_transfer, external_file);
		if (ferror(external_file)) {
			fclose(external_file);
			printf(MESSAGE_IO_ERROR);
			return 1;
		}
		cursor += to_transfer;
	}
	if (close_file(fs, &internal_file)) {
		printf(MESSAGE_IO_ERROR);
		return 1;
	}

	fclose(external_file);
	return 0;
}
Result action_import(FileSystem* fs, DirCursor* current_dir, uint8_t* after_command) {
	uint8_t *internal = after_command;
	uint8_t *external;
	split(internal, &external, ' ');
	if(verify_filename(internal)) {
		return 0;
	}
	uint8_t file_name[FILE_NAME_BUFFER];
	memset(file_name, 0, FILE_NAME_BUFFER);
	strcpy(file_name, internal);

	DirEntry file;
	FileIO internal_file;

	switch (resolve(fs, current_dir, &file, file_name)) {
		case OPTIONAL_OK:
			if (is_folder(&file)) {
				printf(MESSAGE_IS_NOT_FILE);
				return 0;
			}
			break;
		case OPTIONAL_STRUCTURE_ERROR:
			init_meta(&file, 0, file_name);
			create_file(fs, current_dir, &file);
			break;
		case OPTIONAL_IO_ERROR:
			printf(MESSAGE_IO_ERROR);
			return 1;
	}
	FILE *external_file = fopen(external, "rb");
	if (external_file == NULL || ferror(external_file)) {
		printf(MESSAGE_IO_ERROR);
		return 1;
	}
	open_file(fs, &file, &internal_file);
	while(!feof(external_file)) {
		uint8_t buffer[IO_BUFFER];
		size_t read = fread(buffer, 1, IO_BUFFER, external_file);
		if(ferror(external_file)) {
			printf(MESSAGE_IO_ERROR);
			goto bad_exit;
		}
		switch (write_to_file(fs, &internal_file, buffer, read)) {
			case OPTIONAL_OK:
				break;
			case OPTIONAL_STRUCTURE_ERROR:
				printf(MESSAGE_OUT_OF_SPACE);
				goto bad_exit;
			case OPTIONAL_IO_ERROR:
				printf(MESSAGE_IO_ERROR);
				goto bad_exit;
		}
	}

	if (close_file(fs, &internal_file)) {
		printf(MESSAGE_IO_ERROR);
		goto bad_exit;
	}

	fclose(external_file);
	return 0;

	bad_exit:
	fclose(external_file);
	return 1;
}

int main() {
	init_table();

	uint8_t input_buffer[INPUT_BUFFER];
	FileSystem fs;

	if (init_or_mount(input_buffer, &fs)) {
		return 1;
	}

	DirCursor directory_stack[MAX_DEPTH];
	size_t directory_stack_ptr = 0;
	uint8_t current_path[DIR_STRING_BUFFER] = "/";

	get_root(&fs, &directory_stack[0]);
	while (1) {
		printf("%s> ", current_path);
		fgets(input_buffer, INPUT_BUFFER, stdin);
		trim_untill_newline(input_buffer);

		uint8_t *after_command;
		uint8_t *root_command = input_buffer;
		split(root_command, &after_command, ' ');
		string_to_lower(root_command);

		// Записать строку в файл
		if (strcmp(root_command, "exit") == 0) {
			break;
		} else if (strcmp(root_command, "read") == 0) {
			if(action_read(&fs, &directory_stack[directory_stack_ptr], after_command)) {
				break;
			}
		} else if (strcmp(root_command, "write") == 0) {
			if(action_write(input_buffer, &fs, &directory_stack[directory_stack_ptr], after_command)) {
				break;
			}
		} else if (strcmp(root_command, "mkdir") == 0) {
			if(action_mkdir(&fs, &directory_stack[directory_stack_ptr], after_command)) {
				break;
			}
		} else if (strcmp(root_command, "dir") == 0) {
			if(action_dir(&fs, &directory_stack[directory_stack_ptr], after_command)) {
				break;
			}
		} else if (strcmp(root_command, "import") == 0) {
			if(action_import(&fs, &directory_stack[directory_stack_ptr], after_command)) {
				break;
			}
		} else if (strcmp(root_command, "export") == 0) {
			if(action_export(&fs, &directory_stack[directory_stack_ptr], after_command)) {
				break;
			}
		} else if (strcmp(root_command, "cd") == 0) {
			uint8_t* path = after_command;
			if (strcmp(path, "..") == 0) {
				if (directory_stack_ptr == 0) {
					continue;
				}
				directory_stack_ptr--;
				size_t i = strlen(current_path) - 1;
				while(current_path[i-1] != '/') {
					i--;
				}
				current_path[i] = '\0';
			} else {
				if (*path == '/') {
					strcpy(current_path, "/");
					directory_stack_ptr = 0;
					path++;
				}

				uint8_t folder_name[FILE_NAME_BUFFER];
				while(*path) {
					uint8_t* next;
					split(path, &next, '/');
					memset(folder_name, 0, FILE_NAME_BUFFER);
					strcpy(folder_name, path);
					path = next;

					DirEntry next_dir;
					switch (resolve(&fs, &directory_stack[directory_stack_ptr], &next_dir, folder_name)) {
						case OPTIONAL_OK:
							if(!is_folder(&next_dir)) {
								printf(MESSAGE_IS_NOT_DIR);
								goto exit_loop;
							}
							directory_stack_ptr++;
							open_dir(&fs, &next_dir, &directory_stack[directory_stack_ptr]);

							strcat(current_path, folder_name);
							strcat(current_path, "/");
							break;
						case OPTIONAL_STRUCTURE_ERROR:
							printf(MESSAGE_NOT_FOUND);
							break;
						case OPTIONAL_IO_ERROR:
							printf(MESSAGE_IO_ERROR);
							return 1;
					}
				}
				exit_loop:;
			}
		}
	}
	if(close_fs_file(&fs)) {
		printf(MESSAGE_IO_ERROR);
	}
	return 0;
}
