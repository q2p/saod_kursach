#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <stdlib.h>
#include <ctype.h>

#include "src/fat.h"

char inputBuffer[4096];

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

int main() {
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

        char *command = strtok(inputBuffer, " ");
        char *rootCommand = command;
        stringToLower(rootCommand);

        // Записать строку в файл
        if (strstr(rootCommand, "write") != NULL) {
            command = strtok(NULL, " ");
            char *pathToFile = command;
            char *text = malloc(30);
            text[0] = '\0';
            char *temp;
            int flag = 0;
            command = strtok(NULL, " ");
            while(command!=NULL) {
                if (flag != 0) {
                    temp = text;
                    text = strcat(temp, command);
                    strcat(text, " ");
                } else {
                    strcpy(text,command);
                    flag = 1;
                    strcat(text, " ");
                }
                if (flag != 0) {
                    command = strtok(NULL, " ");
                    flag = 1;
                }
            }
            printf("%s\n", text);
            free(text);

            // Выйти из программы
        } else if (strstr(rootCommand, "close") != NULL){
            break;

            // Создать папку
        } else if (strstr(rootCommand, "mkdir") != NULL) {
            uint8_t *dirName = (uint8_t*)malloc(MAX_FILE_NAME* sizeof(uint8_t));
            dirName = strtok(NULL, " ");

            size_t lengthDirName = strlen(dirName);
            DirEntry *directory = (DirEntry*)malloc(sizeof(DirEntry));

            init_meta(directory, 1, dirName);
        }

    }
    return 0;
}
