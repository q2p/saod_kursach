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

void push(Stack **head, uint8_t *value) {
    Stack *tmp = malloc(sizeof(Stack));
    if (tmp == NULL) {
        exit(1);
    }
    tmp->next = *head;
    tmp->name = value;
    *head = tmp;
}

uint8_t *pop(Stack **head) {
    Stack *out;
    uint8_t *value;
    if (*head == NULL) {
        exit(1);
    }
    out = *head;
    *head = (*head)->next;
    value = out->name;
    free(out);
    return value;
}

char LUT[256];

void stringToLower(char *string) {
    for(char *p = string; *p; ++p)
        *p = *p > 0x40 && *p < 0x5b ? *p | 0x60 : *p;
}

void split(char* before, char** after, char delimiter) {
    char* p = before;
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

int main() {
    for (uint16_t c = 0; c != 256; c++) {
        LUT[c] = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    }

    char inputBuffer[4096];

    FileSystem *fileSystem = (FileSystem *) malloc(sizeof(FileSystem));
    DirCursor *dirCursor = (DirCursor *) malloc(sizeof(DirCursor));
    Stack *head = NULL;
    char *path_fs = "t";
    if (init_fs_file(fileSystem, path_fs, MAX_CLUSTERS) != 0) {
        printf("Произошла неизвестная ошибка");
        return 1;
    }
    get_root(fileSystem, dirCursor);
    while (1) {
        char currentPath[4096] = {0};
        if (head == NULL) {
            printf("/>");
        } else {
            strcat(currentPath, "/");
            Stack *tmp = head;
            while (tmp) {
                strcat(currentPath, tmp->name);
                tmp = tmp->next;
                if (tmp == NULL) {
                    strcat(currentPath, ">");
                } else {
                    strcat(currentPath, "/");
                }
            }
            printf("%s", currentPath);
        }
        fgets(inputBuffer, sizeof(inputBuffer), stdin);

        for (int i = 0; inputBuffer[i] != '\0'; i++) {
            if (inputBuffer[i] == '\n') {
                inputBuffer[i] = '\0';
            }
        }

        char *afterCommand;
        char *rootCommand = inputBuffer;
        split(rootCommand, &afterCommand, ' ');
        stringToLower(rootCommand);

        // Записать строку в файл
        if (strcmp(rootCommand, "write") == 0) {
            char *content;
            char *pathToFile = afterCommand;
            split(pathToFile, &content, ' ');
            printf("%d %s\n", strlen(content) + 1, content);

            // Выйти из программы
        } else if (strcmp(rootCommand, "exit") == 0) {
            break;

            // Создать папку
        } else if (strcmp(rootCommand, "mkdir") == 0) {
            char *dirName = afterCommand;
            size_t len = strlen(dirName);
            if (len > MAX_FILE_NAME) {
                printf("allo");
            }
            for (char *p = dirName; *p; ++p) {
                if (!LUT[*p]) {
                    printf("allo");
                }
            }
            memset(dirName + len, 0, MAX_FILE_NAME - len);
            DirEntry directory;
            init_meta(&directory, 1, dirName);
            switch (create_file(fileSystem, dirCursor, &directory)) {
                case OPTIONAL_OK:
                    break;
                case OPTIONAL_STRUCTURE_ERROR:
                    printf("не хватает места");
                    break;
                case OPTIONAL_IO_ERROR:
                    printf("ошибка чтения/записи");
                    return 1;
            }
            // To folder/file
        } else if (strcmp(rootCommand, "cd") == 0) {
            char *path = afterCommand;
            DirEntry *dir;
            if (strcmp(path, "..") == 0) {
                path = pop(&head);
//                switch (resolve(fileSystem, dirCursor, dir,path)) {
//                case OPTIONAL_OK:
//                    printf("OK");
//                    break;
//                case OPTIONAL_STRUCTURE_ERROR:
//                    printf("STRUCT");
//                    break;
//                case OPTIONAL_IO_ERROR:
//                    printf("IO");
//                    return 1;
//            }
                //dirCursor = openDir(dir);

                // temporarily while segmentation fault in resolve
                continue;
            } else {

                /*
                 *      It causes segmentation fault in src/fat.c:151
                 */
//            switch (resolve(fileSystem, dirCursor, dir,path)) {
//                case OPTIONAL_OK:
//                    printf("OK");
//                    break;
//                case OPTIONAL_STRUCTURE_ERROR:
//                    printf("STRUCT");
//                    break;
//                case OPTIONAL_IO_ERROR:
//                    printf("IO");
//                    return 1;
//            }
                //dirCursor = openDir(dir);
                push(&head, path);
            }
        }
    }
    return 0;
}
