#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <stdlib.h>
#include <ctype.h>

#include "src/fat.h"

char LUT[256];

char * strDelimiter(const char * string) {
    char *returnString = malloc(sizeof(string) + 1);
    while(1) {
        if (strcmp(string, " ")) {
            strcpy(returnString, "\0");
            break;
        }
        strcpy(returnString,string);
        string++;
        returnString++;
    }
    return returnString;
}

void stringToLower(char *string) {
    for(char *p = string; *p; ++p)
        *p = *p > 0x40 && *p < 0x5b ? *p | 0x60 : *p;
}

void split(char* before, char** after, char delimeter) {
    char* p = before;
    while(1) {
        if(*p == '\0') {
            *after = p;
        }
        if(*p == delimeter) {
            *p = '\0';
            *after = p+1;
        }
        p++;
    }
}

int main() {
    for(uint16_t c = 0; c != 256; c++) {
        LUT[c] = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    }
    
    char inputBuffer[4096];

    FileSystem *fileSystem = (FileSystem*)malloc(sizeof(FileSystem));
    DirCursor *dirCursor = (DirCursor*) malloc(sizeof(DirCursor));
    char *path_fs = "t";
    if (init_fs_file(fileSystem, path_fs, MAX_CLUSTERS) != 0) {
        printf("Произошла неизвестная ошибка");
        return 1;
    }
    get_root(fileSystem, dirCursor);
    while(1) {
        printf("/>>");
        fgets(inputBuffer, sizeof(inputBuffer), stdin);

        char *afterCommand;
        char *rootCommand = inputBuffer;
        split(rootCommand, &afterCommand, ' ');
        stringToLower(rootCommand);

        // Записать строку в файл
        if (strcmp(rootCommand, "write") == 0) {
            char *content;
            char *pathToFile = afterCommand;
            split(pathToFile, &content, ' ');
            printf("%d %s\n", strlen(content)+1, content);

            // Выйти из программы
        } else if (strcmp(rootCommand, "exit") == 0){
            break;

            // Создать папку
        } else if (strcmp(rootCommand, "mkdir") == 0) {
            char *dirName = afterCommand;
            size_t len = strlen(dirName);
            if (len > MAX_FILE_NAME) {
                printf("allo");
            }
            for(char *p = dirName; *p; ++p) {
                if(!LUT[*p]) {
                    printf("allo");
                }
            }
            memset(dirName+len, 0, MAX_FILE_NAME-len);
            DirEntry directory;
            init_meta(&directory, 1, dirName);
            switch (create_file(fileSystem, dirCursor, &directory)) {
                OPTIONAL_OK:
                    break;
                OPTIONAL_STRUCTURE_ERROR:
                    printf("не хватает места");
                    break;
                OPTIONAL_IO_ERROR:
                    printf("ошибка чтения/записи");
                    return 1;
            }

        }

    }
    return 0;
}
