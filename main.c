#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <conio.h>

#include "src/fat.h"

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
            uint8_t *pathToFile = afterCommand;
            DirEntry file;
            FileIO fileIo;
            uint8_t content_buf[4096];
            switch (resolve(&fs, &directory_stack[directory_stack_ptr], &file, pathToFile)) {
                case OPTIONAL_OK:
                    printf("not create file");
                    if(verify_filename(pathToFile)) {
                        printf("Filename is either too long or does contain illegal symbols");
                    } else {
                        if (is_folder(&file)) {
                            printf("Not a file!");
                        } else {
                            open_file(&fs, &file, &fileIo);
                            while (1) {
                                fgets(content_buf, 4096, stdin);
                                if (content_buf[0] == '\n') {
                                    break;
                                }
                                switch (write_to_file(&fs, &fileIo, content_buf, strlen(content_buf))) {
                                    case OPTIONAL_OK:
                                        break;
                                    case OPTIONAL_STRUCTURE_ERROR:
                                        printf("Not enough space");
                                        break;
                                }
                            }
                            close_file(&fs, &fileIo);
                        }
                    }
                    break;
                case OPTIONAL_STRUCTURE_ERROR:
                    printf("create file");
                    init_meta(&file, 0, pathToFile);
                    if(verify_filename(pathToFile)) {
                        printf("Filename is either too long or does contain illegal symbols");
                    } else {
                        open_file(&fs, &file, &fileIo);
                        while (1) {
                            fgets(content_buf, 4096, stdin);
                            if (content_buf[0] == '\n') {
                                break;
                            }
                            switch (write_to_file(&fs, &fileIo, content_buf, strlen(content_buf))) {
                                case OPTIONAL_OK:
                                    break;
                                case OPTIONAL_STRUCTURE_ERROR:
                                    printf("Not enough space");
                                    break;
                            }
                        }
                        close_file(&fs, &fileIo);
                    }
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
                        printf("Folder created\n");
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
            if (*path == '/') {
                currentPath[0] = '/';
                currentPath[1] = '\0';

                directory_stack_ptr = 0;
                memset(directory_stack,  0, MAX_DEPTH* sizeof(int));
                get_root(&fs, &directory_stack[0]);

                uint8_t *full_path = afterCommand;
                uint8_t *ssc;
                int l = 0;
                uint8_t folder_name[64];
                ssc = strstr(full_path, "/");
                do {

                    l = strlen(ssc) + 1;
                    full_path = &full_path[strlen(full_path)-l+2];
                    ssc = strstr(full_path, "/");
                    strcpy(folder_name, full_path);
                    trim_untill_slash(folder_name);

                    DirEntry next_dir;

                    switch (resolve(&fs, &directory_stack[directory_stack_ptr], &next_dir, folder_name)) {
                        case OPTIONAL_OK:
                            if(is_folder(&next_dir)) {
                                directory_stack_ptr++;
                                open_dir(&fs, &next_dir, &directory_stack[directory_stack_ptr]);

                                strcat(currentPath, folder_name);
                                strcat(currentPath, "/");
                            } else {
                                printf("Not a folder");
                            }
                            break;
                        case OPTIONAL_STRUCTURE_ERROR:
                            //printf("Not found %s\n", folder_name);
                            break;
                        case OPTIONAL_IO_ERROR:
                            printf("IO Error");
                            return 1;
                    }
                } while(ssc);

            } else if (strcmp(path, "..") == 0) {
                DirEntry previous_dir;
                if (directory_stack_ptr == 0) {
                    printf("You are in root directory\n");
                    continue;
                }
                if (directory_stack_ptr != 0) {
                    directory_stack_ptr--;

                    switch (resolve(&fs, &directory_stack[directory_stack_ptr], &previous_dir, path)) {
                        case OPTIONAL_OK:
                            if (is_folder(&previous_dir)) {
                                open_dir(&fs, &previous_dir, &directory_stack[directory_stack_ptr]);

                                size_t last_symbol = strlen(currentPath) - 1;
                                currentPath[last_symbol] = '\0';
                                for (size_t i = last_symbol - 2; currentPath[i] != '/'; i--) {
                                    currentPath[i] = '\0';
                                }
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
            } else {
                DirEntry next_dir;
                switch (resolve(&fs, &directory_stack[directory_stack_ptr], &next_dir, path)) {
                    case OPTIONAL_OK:
                        if(is_folder(&next_dir)) {
                            directory_stack_ptr++;
                            open_dir(&fs, &next_dir, &directory_stack[directory_stack_ptr]);

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
        } else if (strcmp(rootCommand, "import") == 0) {
            uint8_t *src;
            uint8_t *dst = afterCommand;
            split(afterCommand, &src, ' ');
            FILE *source = fopen(src, "rt");
            if (source == NULL) {
                printf("File doesn't exist");
                continue;
            }

            DirEntry file;
            FileIO fileIo;

            switch (resolve(&fs, &directory_stack[directory_stack_ptr], &file, dst)) {
                case OPTIONAL_OK:
                    printf("not create file");
                    if(verify_filename(dst)) {
                        printf("Filename is either too long or does contain illegal symbols");
                    } else {
                        if (is_folder(&file)) {
                            printf("Not a file!");
                        } else {
                            open_file(&fs, &file, &fileIo);
                            while(!feof(source)) {
                                uint8_t buffer[4096];
                                fread(buffer, 1, 4096, source);
                                switch (write_to_file(&fs, &fileIo, buffer, strlen(buffer))) {
                                    case OPTIONAL_OK:
                                        printf("Data writing was successful\n");
                                        break;
                                    case OPTIONAL_STRUCTURE_ERROR:
                                        printf("Not enough space");
                                        break;
                                }
                            }

                            close_file(&fs, &fileIo);
                        }
                    }
                    break;
                case OPTIONAL_STRUCTURE_ERROR:
                    printf("create file");
                    init_meta(&file, 0, dst);
                    if(verify_filename(dst)) {
                        printf("Filename is either too long or does contain illegal symbols");
                    } else {
                        open_file(&fs, &file, &fileIo);
                        while(!feof(source)) {
                            uint8_t buffer[4096];
                            fread(buffer, 1, 4096, source);
                            switch (write_to_file(&fs, &fileIo, buffer, strlen(buffer))) {
                                case OPTIONAL_OK:
                                    printf("Data writing was successful\n");
                                    break;
                                case OPTIONAL_STRUCTURE_ERROR:
                                    printf("Not enough space");
                                    break;
                            }
                        }
                        close_file(&fs, &fileIo);
                    }
            }

            fclose(source);

        } else if (strcmp(rootCommand, "read") == 0) {
            uint8_t *file_name = afterCommand;

            DirEntry file;
            FileIO fileIo;
            FileCursor size;
            uint8_t dst_buf[100000];
            switch (resolve(&fs, &directory_stack[directory_stack_ptr], &file, file_name)) {
                case OPTIONAL_OK:
                    size = get_file_size(&fs, &file);
                    open_file(&fs, &file, &fileIo);
                    read_from_file(&fs, &fileIo, dst_buf, size);
                    close_file(&fs, &fileIo);
                    printf("File content:\n%s", dst_buf);
                    break;
                case OPTIONAL_STRUCTURE_ERROR:
                    printf("Not enough space");
                    break;
                case OPTIONAL_IO_ERROR:
                    printf("IO error");
                    return 1;
            }
        } else if (strcmp(rootCommand, "export") == 0) {
            uint8_t *dst;
            uint8_t *src = afterCommand;
            split(afterCommand, &dst, ' ');

            DirEntry file;
            FileIO fileIo;
            FileCursor size;
            uint8_t buffer[100000];

            switch (resolve(&fs, &directory_stack[directory_stack_ptr], &file, src)) {
                case OPTIONAL_OK:
                    size = get_file_size(&fs, &file);
                    open_file(&fs, &file, &fileIo);
                    read_from_file(&fs, &fileIo, buffer, size);
                    close_file(&fs, &fileIo);
                    break;
                case OPTIONAL_STRUCTURE_ERROR:
                    printf("Not enough space");
                    break;
                case OPTIONAL_IO_ERROR:
                    printf("IO error");
                    return 1;
            }

            FILE *dst_file = fopen(dst, "wt");
            if (dst_file == NULL) {
                printf("File doesn't exist");
                continue;
            }

            fprintf(dst_file, "%s", buffer);
        }
    }
    return 0;
}
