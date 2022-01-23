#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <stdlib.h>
#include <ctype.h>

#include "src/fat.h"

typedef struct stack{
	struct stack *next;
	uint8_t *name;
} Stack;

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

enum {
	INPUT_BUFFER = 4096,
	MAX_DEPTH = 256,
	FS_SIZE = 16*1024*1024
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
	uint8_t currentPath[4096] = "/";

	if (init_fs_file(&fs, "fs.dat", FS_SIZE / CLUSTER_SIZE) != 0) {
		printf("Can't open FS.");
		return 1;
	}

	get_root(&fs, &directory_stack[0]);
	while (1) {
		printf("%s> ", currentPath);
		fgets(inputBuffer, INPUT_BUFFER, stdin);
		trim_untill_newline(inputBuffer);

		uint8_t *afterCommand;
		uint8_t *rootCommand = inputBuffer;
		split(rootCommand, &afterCommand, ' ');
		stringToLower(rootCommand);

		// Записать строку в файл
		if (strcmp(rootCommand, "write") == 0) {
			uint8_t *content;
			uint8_t *pathToFile = afterCommand;
			split(afterCommand, &content, ' ');
			printf("%d %s\n", strlen(content) + 1, content);

			if(verify_filename(pathToFile)) {
				printf("Filename is either too long or does contain illegal symbols");
			} else {
				//....
			}

		} else if (strcmp(rootCommand, "exit") == 0) {
			break;

			// Создать папку
		} else if (strcmp(rootCommand, "mkdir") == 0) {
			uint8_t *dirName = afterCommand;
			if(verify_filename(dirName)) {
				printf("Filename is either too long or does contain illegal symbols");
			} else {
				DirEntry directory;
				init_meta(&directory, 1, dirName);
				switch (create_file(&fs, &directory_stack[directory_stack_ptr], &directory)) {
					case OPTIONAL_OK:
						printf("Folder created");
						break;
					case OPTIONAL_STRUCTURE_ERROR:
						printf("Not enough space");
						break;
					case OPTIONAL_IO_ERROR:
						printf("IO Error");
						return 1;
				}
			}
		} else if (strcmp(rootCommand, "cd") == 0) {
			uint8_t *path = afterCommand;
			if (strcmp(path, "..") == 0) {
				if(directory_stack_ptr != 0) {
					directory_stack_ptr--;
				}
			} else {
				DirEntry next_dir;
				switch (resolve(&fs, &directory_stack[directory_stack_ptr], &next_dir, path)) {
					case OPTIONAL_OK:
						if(is_folder(&next_dir)) {
							directory_stack_ptr++;
							open_dir(&next_dir, &directory_stack[directory_stack_ptr]);

							strcat(currentPath, path);
							strcat(currentPath, "/");
						} else {
							printf("Not a folder");
						}
						break;
					case OPTIONAL_STRUCTURE_ERROR:
						printf("Not found");
						break;
					case OPTIONAL_IO_ERROR:
						printf("IO Error");
						return 1;
				}
			}
		}
	}
	return 0;
}
