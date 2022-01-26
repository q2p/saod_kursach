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
	TABLE_SIZE = MAX_CLUSTERS*sizeof(ClusterLocation),
	ROOT_OFFSET = TABLE_SIZE,
	
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
		ret += cluster;
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
	for(size_t i = 1; i != TABLE_SIZE; i++) {
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

	fwrite(fs->table_cache, 1, TABLE_SIZE, fs->file);
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

	rewind(fs->file);
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
				return OPTIONAL_OK;
			}
			if(memcmp(target->meta+OFFSET_NAME, buffer+offset+OFFSET_NAME, FILE_NAME_BUFFER) == 0) {
				return OPTIONAL_STRUCTURE_ERROR;
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

// TODO: buffer?
OptionalResult read_from_file(FileSystem* fs, FileIO* file, uint8_t* buffer, size_t size) {
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

void close_fs_file() {
	// fflush(file);
	// fclose(file);
}

void rem_dir();
void rem_file();

uint8_t LUT[256];

void stringToLower(uint8_t *string) {
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
		LUT[c] = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
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

Result verify_filename(uint8_t* filename) {
	size_t len = strlen(filename);
	// TODO: forbid ..
	if (len > MAX_FILE_NAME) {
		return 1;
	}
	while(*filename) {
		if (!LUT[*(filename++)]) {
			return 1;
		}
	}
	return 0;
}

int main() {
	init_table();

	uint8_t inputBuffer[INPUT_BUFFER];

	FileSystem fs;
	DirCursor directory_stack[MAX_DEPTH];
	size_t directory_stack_ptr = 0;
	uint8_t currentPath[DIR_STRING_BUFFER] = "/";

	printf("Do you wanna to create a file system? [1]\n Or load an existing[2]\n");
	fgets(inputBuffer, INPUT_BUFFER, stdin);
	trim_untill_newline(inputBuffer);
	if (strcmp(inputBuffer, "1") == 0) {
		printf("Enter a file system name:");
		fgets(inputBuffer, INPUT_BUFFER, stdin);
		trim_untill_newline(inputBuffer);

		if (init_fs_file(&fs, inputBuffer, FS_SIZE / CLUSTER_SIZE)) {
			printf("Can't create FS.");
			return 1;
		}
	} else if (strcmp(inputBuffer, "2") == 0) {
		printf("Enter a file system name:");
		fgets(inputBuffer, INPUT_BUFFER, stdin);
		trim_untill_newline(inputBuffer);

		if (open_fs_file(&fs, inputBuffer)) {
			printf("Can't open FS.");
			return 1;
		}
	} else {
		printf("Bad option.");
		return 1;
	}

	get_root(&fs, &directory_stack[0]);
	while (1) {
		printf("%s> ", currentPath);
		fgets(inputBuffer, INPUT_BUFFER, stdin);
		trim_untill_newline(inputBuffer);

		uint8_t *after_command;
		uint8_t *root_command = inputBuffer;
		split(root_command, &after_command, ' ');
		stringToLower(root_command);

		// Записать строку в файл
		if (strcmp(root_command, "exit") == 0) {
			break;
		} else if (strcmp(root_command, "write") == 0) {
			uint8_t *pathToFile = after_command;
			DirEntry file;
			FileIO fileIo;
			if(verify_filename(pathToFile)) {
				printf("Filename is either too long or does contain illegal symbols");
				continue;
			}
			switch (resolve(&fs, &directory_stack[directory_stack_ptr], &file, pathToFile)) {
				case OPTIONAL_OK:
					if (is_folder(&file)) {
						printf("Not a file!");
						continue;
					} else {
						open_file(&fs, &file, &fileIo);
					}
					break;
				case OPTIONAL_STRUCTURE_ERROR:
					init_meta(&file, 0, pathToFile);
					create_file(&fs, &directory_stack[directory_stack_ptr], &file);
					break;
				case OPTIONAL_IO_ERROR:
					printf("IO Error");
					return 1;
			}
			while (1) {
				fgets(inputBuffer, INPUT_BUFFER, stdin);
				if (inputBuffer[0] == '\n') {
					break;
				}
				switch (write_to_file(&fs, &fileIo, inputBuffer, strlen(inputBuffer))) {
					case OPTIONAL_OK:
						break;
					case OPTIONAL_STRUCTURE_ERROR:
						printf("Not enough space");
						break;
					case OPTIONAL_IO_ERROR:
						printf("IO Error");
						return 1;
				}
			}
			if (close_file(&fs, &fileIo)) {
				printf("IO Error");
				return 1;
			}
		} else if (strcmp(root_command, "mkdir") == 0) {
			uint8_t* dir_name = after_command;
			if(verify_filename(dir_name)) {
				printf("Filename is either too long or does contain illegal symbols");
				continue;
			}
			DirEntry directory;
			uint8_t name_buffer[FILE_NAME_BUFFER];
			memset(name_buffer, 0, FILE_NAME_BUFFER);
			strcpy(name_buffer, dir_name);
			switch (resolve(&fs, &directory_stack[directory_stack_ptr], &directory, name_buffer)) {
				case OPTIONAL_OK:
					printf("File already exists!\n");
					continue;
				case OPTIONAL_STRUCTURE_ERROR:
					break;
				case OPTIONAL_IO_ERROR:
					printf("IO Error\n");
					return 1;
			}
			init_meta(&directory, 1, name_buffer);
			switch (create_file(&fs, &directory_stack[directory_stack_ptr], &directory)) {
				case OPTIONAL_OK:
					printf("Folder created\n");
					break;
				case OPTIONAL_STRUCTURE_ERROR:
					printf("Not enough space\n");
					break;
				case OPTIONAL_IO_ERROR:
					printf("IO Error\n");
					return 1;
			}
		} else if (strcmp(root_command, "cd") == 0) {
			uint8_t* path = after_command;
			if (strcmp(path, "..") == 0) {
				if (directory_stack_ptr == 0) {
					continue;
				}
				directory_stack_ptr--;
				size_t i = strlen(currentPath) - 1;
				while(currentPath[i-1] != '/') {
					i--;
				}
				currentPath[i] = '\0';
			} else {
				if (*path == '/') {
					strcpy(currentPath, "/");

					directory_stack_ptr = 0;
					get_root(&fs, &directory_stack[directory_stack_ptr]);
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
								printf("Not a folder");
								goto exit_loop2;
							}
							directory_stack_ptr++;
							open_dir(&fs, &next_dir, &directory_stack[directory_stack_ptr]);

							strcat(currentPath, folder_name);
							strcat(currentPath, "/");
							break;
						case OPTIONAL_STRUCTURE_ERROR:
							printf("File named %s was not found\n", folder_name);
							break;
						case OPTIONAL_IO_ERROR:
							printf("IO Error");
							return 1;
					}
				}
				exit_loop2:;
			}
		} else if (strcmp(root_command, "import") == 0) {
			uint8_t *internal = after_command;
			uint8_t *external;
			uint8_t file_name[FILE_NAME_BUFFER];
			split(internal, &external, ' ');
			if(verify_filename(internal)) {
				printf("Filename is either too long or does contain illegal symbols");
			}
			strcpy(file_name, internal);

			DirEntry file;
			FileIO internal_file;

			switch (resolve(&fs, &directory_stack[directory_stack_ptr], &file, file_name)) {
				case OPTIONAL_OK:
					if (is_folder(&file)) {
						printf("Not a file!");
					} else {
						open_file(&fs, &file, &internal_file);
					}
					break;
				case OPTIONAL_STRUCTURE_ERROR:
					init_meta(&file, 0, file_name);
					create_file(&fs, &directory_stack[directory_stack_ptr], &file);
					break;
				case OPTIONAL_IO_ERROR:
					printf("IO Error");
					return 1;
			}
			FILE *external_file = fopen(external, "rb");
			if (external_file == NULL) {
				printf("File doesn't exist");
				continue;
			}
			if(ferror(external_file)) {
				printf("IO Error");
				continue;
			}
			while(!feof(external_file)) {
				uint8_t buffer[IO_BUFFER];
				size_t read = fread(buffer, 1, IO_BUFFER, external_file);
				if(ferror(external_file)) {
					printf("IO Error");
					break;
				}
				switch (write_to_file(&fs, &internal_file, buffer, read)) {
					case OPTIONAL_OK:
						printf("Data writing was successful\n");
						break;
					case OPTIONAL_STRUCTURE_ERROR:
						printf("Not enough space");
						goto exit_loop1;
				}
			}
			exit_loop1:;
			close_file(&fs, &internal_file);

			fclose(external_file);
		} else if (strcmp(root_command, "read") == 0) {
			uint8_t *file_name = after_command;

			DirEntry file;
			switch (resolve(&fs, &directory_stack[directory_stack_ptr], &file, file_name)) {
				case OPTIONAL_OK:
					break;
				case OPTIONAL_STRUCTURE_ERROR:
					printf("File not found.");
					continue;
				case OPTIONAL_IO_ERROR:
					printf("IO error");
					return 1;
			}
			FileIO fileIo;
			uint8_t buffer[IO_BUFFER];
			FileCursor size = get_file_size(&fs, &file);
			FileCursor counter = 0;
			open_file(&fs, &file, &fileIo);
			printf("File contents:\n");
			while (counter < size) {
				read_from_file(&fs, &fileIo, buffer, IO_BUFFER-1);
				size_t remaining = min(IO_BUFFER-1, size - counter);
				buffer[remaining] = '\0';
				printf("%s", buffer);
				counter += IO_BUFFER-1;
			}
			if(close_file(&fs, &fileIo)) {
				printf("IO error");
				return 1;
			}
		} else if (strcmp(root_command, "export") == 0) {
			uint8_t *dst;
			uint8_t *src = after_command;
			split(after_command, &dst, ' ');

			DirEntry file;
			FileIO fileIo;
			FileCursor size;
			uint8_t buffer[IO_BUFFER];
			int counter = 0;

			switch (resolve(&fs, &directory_stack[directory_stack_ptr], &file, src)) {
				case OPTIONAL_OK:
					break;
				case OPTIONAL_STRUCTURE_ERROR:
					printf("Not enough space");
					continue;
				case OPTIONAL_IO_ERROR:
					printf("IO error");
					return 1;
			}
			FILE *external_file = fopen(dst, "wb");
			if (external_file == NULL) {
				printf("File doesn't exist");
				continue;
			}
			size = get_file_size(&fs, &file);
			open_file(&fs, &file, &fileIo);
			while(counter <= size) {
				read_from_file(&fs, &fileIo, buffer, size);
				fwrite(buffer,1 ,min(size - counter, IO_BUFFER),external_file);
				counter += IO_BUFFER;
			}
			close_file(&fs, &fileIo);

			fclose(external_file);
		} else if (strcmp(root_command, "dir") == 0) {
			DirIter iter;
			DirEntry nextEntry;

			dir_iter(&fs, &directory_stack[directory_stack_ptr], &iter);

			while (dir_iter_next(&fs, &iter, &nextEntry) == OPTIONAL_OK) {
				printf("%s - ", get_file_name(&nextEntry));
				if (is_folder(&nextEntry)) {
					printf("DIR\n");
				} else {
					printf("FILE\n");
				}
			}
		}
	}
	return 0;
}
