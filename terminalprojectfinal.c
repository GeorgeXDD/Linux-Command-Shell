#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <termios.h>

#define MAX_LINE_LEN 1024
#define MAX_commands 128

void removeLeadingSpaces(char* str) {
    int i = 0;
    while (str[i] == ' ') {
        i++;
    }
    memmove(str, str + i, strlen(str) + 1 - i);
}

char* getFilename(char* command) {
    char* res = (char*)malloc(strlen(command) + 1);
    strcpy(res, "");

    int lastGreaterThanIndex = -1;
    for (int i = 0; i < strlen(command); i++) {
        if (command[i] == '>') {
            lastGreaterThanIndex = i;
        }
    }

    if (lastGreaterThanIndex >= 0) {
        strcpy(res, command + lastGreaterThanIndex + 1);
    }
    removeLeadingSpaces(res);
    return res;
}

void cutString(char* command) {
    int j = 0;
    for (int i = 0; i < strlen(command); i++) {
        if (command[i] != '>') {
            command[j] = command[i];
            j++;
        }
        else {
            break;
        }
    }
    command[j] = '\0';
}

char** pipecheck(char* line) {
    char** commands = malloc(4096 * sizeof(char*));
    char* command = strtok(line, "|");
    int num_commands = 0;
    while (command != NULL) {
        commands[num_commands] = command;
        num_commands++;
        command = strtok(NULL, "|");
    }
    return commands;
}

bool checkIsRedirect(char* line) {
    for (int i = 0; i < strlen(line); i++) {
        if (line[i] == '>') {
            return true;
        }
    }
    return false;
}

char** splitcommand(char* line) {
    char** commands = malloc(4096 * sizeof(char*));
    if (checkIsRedirect(line) == true) {
        cutString(line);
    }
    char* command = strtok(line, " ");
    int num_commands = 0;
    while (command != NULL) {
        commands[num_commands] = command;
        num_commands++;
        command = strtok(NULL, " ");
    }
    return commands;
}

int checklength(char** commands) {
    int count = 0;
    while (commands[count] != NULL) {
        count++;
    }
    return count;
}

int checklengthPrev(char* str) {
    int len = 0;
    while (*str != '\0') {
        len++;
        str++;
    }
    return len;
}

int externalcomm(char** words, char(*result)[1024], char* prevOutput) {
    int fd[2];
    int stdin[2];

    pipe(stdin);
    pipe(fd);

    write(stdin[1], prevOutput, checklengthPrev(prevOutput));

    int pid_t = fork();
    if (pid_t == 0) {
        close(fd[0]);
        dup2(fd[1], STDOUT_FILENO);
        dup2(stdin[0], STDIN_FILENO);
        close(fd[1]);
        close(stdin[0]);
        close(stdin[1]);
        execvp(words[0], words);

    }
    else {
        close(fd[1]);
        close(stdin[0]);
        close(stdin[1]);
        waitpid(pid_t, NULL, 0);

        int bytes;
        char buffer[1024];

        while ((bytes = read(fd[0], buffer, 1024)) > 0) {
            buffer[bytes] = 0;
        }

        close(fd[0]);
        strncpy(*result, buffer, 1023);
        (*result)[1023] = '\0';

        return 0;
    }
}

int checkRedirect(char* command) {
    for (int i = 0; i < strlen(command); i++) {
        if (command[i] == '>' && command[i + 1] == '>') {
            return 2;
        }
        else if (command[i] == '>' && command[i + 1] != '>') {
            return 1;
        }
    }
    return 0;
}

void tee(int argc, char** argv, char(*result)[1024], char* prevOutput) {

    int opt_a = 0;
    int opt;
    optind = 1;
    while ((opt = getopt(argc, argv, "a")) != -1) {
        switch (opt) {
        case 'a':
            opt_a = 1;
            break;
        }
    }


    if (argc - optind < 1) {
        fprintf(stderr, "Error: tee requires at least one file argument\n");
    }

    // Get file arguments
    char** filenames = &argv[optind];
    int num_filenames = argc - optind;

    // Open output files
    FILE** files = malloc(num_filenames * sizeof(FILE*));
    for (int i = 0; i < num_filenames; i++) {
        char* filename = filenames[i];

        char* mode = opt_a ? "a" : "w";
        files[i] = fopen(filename, mode);

        if (files[i] == NULL) {
            printf("Error opening output file");
        }
    }

    if (strcmp(prevOutput, "") != 0) {
        char buffer[4096];
        int checkedlentgh = checklengthPrev(prevOutput);
        size_t bytes_read = (size_t)checkedlentgh;
        // Write to stdout
        // fwrite(prevOutput, 1, bytes_read, stdout);
        strncpy(*result, prevOutput, 1023);
        (*result)[1023] = '\0';

        // Write to output files
        for (int i = 0; i < num_filenames; i++) {
            fwrite(prevOutput, 1, bytes_read, files[i]);
        }
    }
    else {
        struct termios oldt, newt;
        tcgetattr(STDIN_FILENO, &oldt);

        newt = oldt;
        newt.c_lflag &= ~ICANON;

        newt.c_cc[VMIN] = 1;
        newt.c_cc[VTIME] = 0;

        newt.c_lflag |= ECHO;

        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        while (1) {
            char input;
            int bytes_read = read(STDIN_FILENO, &input, 1);
            if (bytes_read == 0 || input == 0x04 || input == 0x14) {
                printf("\n");
                break;
            }
            printf("%c", input);
            for (int i = 0; i < num_filenames; i++) {
                fprintf(files[i], "%c", input);
            }
        }
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    }


    for (int i = 0; i < num_filenames; i++) {
        fclose(files[i]);
    }

    free(files);
}

void cp(int argc, char** argv) {

    int opt_i = 0;
    int opt_r = 0;
    int opt_t = 0;
    int opt_v = 0;
    char* target_dir = NULL;
    int opt;
    optind = 1;

    while ((opt = getopt(argc, argv, "irtv")) != -1) {
        switch (opt) {
        case 'i':
            opt_i = 1;
            break;
        case 'r':
        case 'R':
            opt_r = 1;
            break;
        case 't':
            opt_t = 1;
            target_dir = optarg;
            break;
        case 'v':
            opt_v = 1;
            break;
        default:
            fprintf(stderr, "Usage: cp [-i] [-r] [-t target] [-v] source [source...] destination\n");
        }
    }


    if (argc - optind < 2) {
        fprintf(stderr, "Error: cp requires at least two arguments\n");
    }

    char** sources = &argv[optind];
    int num_sources = argc - optind - 1;
    char* dest = argv[argc - 1];

    // Check if destination is a directory
    int is_dest_dir = 0;

    struct stat dest_stat;
    if (stat(dest, &dest_stat) == 0) {
        if (S_ISDIR(dest_stat.st_mode)) {
            is_dest_dir = 1;
        }
    }

    // Copy files
    for (int i = 0; i < num_sources; i++) {
        // Get source path
        char* source = sources[i];

        // Check if source is a directory
        int is_source_dir = 0;

        struct stat source_stat;
        if (stat(source, &source_stat) == 0) {
            if (S_ISDIR(source_stat.st_mode)) {
                is_source_dir = 1;
            }
        }

        // Get destination path
        char* dest_path;
        if (is_dest_dir) {
            // Destination is a directory: append source file name to destination path

            char* source_filename = strrchr(source, '/');
            if (source_filename == NULL) {
                source_filename = source;
            }
            else {
                source_filename++;
            }

            int dest_path_len = strlen(dest) + strlen(source_filename) + 2;

            dest_path = malloc(dest_path_len);

            snprintf(dest_path, dest_path_len, "%s/%s", dest, source_filename);
        }
        else {
            // Destination is a file: use destination path as-is

            dest_path = dest;
        }
        // Check if file should be copied recursively
        if (is_source_dir && opt_r) {
            // Source is a directory: copy directory recursively

            // Check if destination directory exists
            int dest_exists = 0;

            if (stat(dest_path, &dest_stat) == 0) {
                if (S_ISDIR(dest_stat.st_mode)) {
                    dest_exists = 1;
                }
            }

            if (!dest_exists) {
                // Destination does not exist: create it
                if (mkdir(dest_path, S_IRWXU | S_IRWXG | S_IRWXO) != 0) {
                    perror("Error creating destination directory");
                }
            }

            // Open source directory
            DIR* source_dir = opendir(source);
            if (source_dir == NULL) {
                perror("Error opening source directory");

            }

            // Iterate over source directory entries
            struct dirent* entry;

            while ((entry = readdir(source_dir)) != NULL) {

                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                    continue;
                }

                // Get source and destination paths for entry
                int source_entry_path_len = strlen(source) + strlen(entry->d_name) + 2;

                char* source_entry_path = malloc(source_entry_path_len);

                snprintf(source_entry_path, source_entry_path_len, "%s/%s", source, entry->d_name);

                int dest_entry_path_len = strlen(dest_path) + strlen(entry->d_name) + 2;

                char* dest_entry_path = malloc(dest_entry_path_len);

                snprintf(dest_entry_path, dest_entry_path_len, "%s/%s", dest_path, entry->d_name);

                // Recursively copy entry
                char* args[] = { "cp", "-r", source_entry_path, dest_entry_path, NULL };

                cp(4, args);

                free(source_entry_path);

                free(dest_entry_path);
            }

            closedir(source_dir);
        }
        else {
            // Source is a file: copy file

            // Check if destination file exists
            int dest_exists = 0;

            if (stat(dest_path, &dest_stat) == 0) {
                dest_exists = 1;
            }

            if (dest_exists && opt_i) {
                // Destination exists and "-i" option is specified: prompt user for confirmation
                printf("Overwrite %s? (y/n) ", dest_path);
                char confirm[4];
                fgets(confirm, 4, stdin);
                if (confirm[0] != 'y') {
                    // User did not confirm: skip file
                    continue;
                }
            }

            // Open source and destination files
            FILE* source_file = fopen(source, "r");

            if (source_file == NULL) {
                perror("Error opening source file");

                exit(EXIT_FAILURE);
            }

            FILE* dest_file = fopen(dest_path, "w");

            if (dest_file == NULL) {
                perror("Error opening destination file");
                exit(EXIT_FAILURE);
            }
            // Copy file contents
            char buffer[4096];
            size_t bytes_read;
            while ((bytes_read = fread(buffer, 1, 4096, source_file)) > 0) {
                fwrite(buffer, 1, bytes_read, dest_file);
            }

            // Close files
            fclose(source_file);
            fclose(dest_file);

            // Print verbose output
            if (opt_v) {
                printf("%s -> %s\n", source, dest_path);
            }

        }

    }

}

char* dirname(const char* path) {
    if (path == NULL || path[0] == '\0') {
        return ".";
    }

    int last_slash_index = -1;

    int len = strlen(path);

    int n_slash = 0;

    for (int i = 0; i < len; i++) {
        if (path[i] == '/') {
            last_slash_index = i;
            n_slash++;
        }
    }

    if (last_slash_index == -1) {
        int last_slash_index = 0;
        int dir_lenx = last_slash_index + 1;

        char* dir = malloc(dir_lenx + 1);
        strncpy(dir, ".", dir_lenx);

        dir[dir_lenx] = '\0';
        return dir;
    }

    if (n_slash == 1 && last_slash_index == 0) {
        char* dir = malloc(last_slash_index + 2);
        strncpy(dir, path, last_slash_index + 1);

        dir[last_slash_index + 1] = '\0';
        return dir;
    }

    int dir_len = last_slash_index;
    if (path[last_slash_index - 1] == '/')
        while (path[dir_len - 1] == '/' && dir_len > 1) --dir_len;
    char* dir = malloc(dir_len + 1);
    strncpy(dir, path, dir_len);
    dir[dir_len] = '\0';

    return dir;
}

void help() {
    printf("\n");
    printf("Information about the cp command:\n");
    printf("The cp command is used to copy files and directories.\n");
    printf("Syntax: cp [options] source destination\n");
    printf("Example: cp file1.txt file2.txt (copies file1.txt to file2.txt)\n\n");

    printf("The cp has the following arguments: -i, -v, -t, -r.\n");
    printf("Example input: cp -i fil1.txt file2.txt\n");
    printf("Example output: Overwrite file2.txt? (y/n)\n\n");

    printf("Example input: cp -v file1.txt file2.txt\n");
    printf("Example output: file.txt -> file2.txt\n\n");

    printf("Example input: cp file -t directory\n");
    printf("Example output: *The -t argument will copy the file into the directory\n\n");

    printf("Example input: cp -r source_directory destination_directory\n");
    printf("Example output: The source_directory will be copy into the destination_directory\n\n");

    printf("Information about the tee command:\n");
    printf("The tee command is used to read from standard input and write to standard output and one or more files.\n");
    printf("Syntax: tee [options] [file(s)]\n");
    printf("Example input: tee file1.txt file2.txt (then we will be able to type in console what should be added to the files)\n");
    printf("Syntax: tee -a [file(s)]\n");
    printf("Example input: tee -a file1.txt file2.txt (then we will be able to type in console what should be appended to the files)\n\n");

    printf("Information about the dirname command:\n");
    printf("The dirname command returns the directory portion of a file path.\n");
    printf("Syntax: dirname [file]\n");
    printf("Example: dirname /etc/passwd returns /etc\n\n");
}

void version() {
    char version_number[] = "1.1";
    char author[] = "Fofiu Florin George";
    char contact[] = "florin.fofiu03@e-uvt.ro";
    printf("\n> Version: %s\n> Author: %s\n> Contact: %s\n\n", version_number, author, contact);
}


int main(int argc, char** argv) {

    char* line;
    bool runnin = true;
    while (runnin) {
        line = readline("terminal@project$> ");

        int commandslength = 0;
        char prevOutput[1024];
        strcpy(prevOutput, "");
        char** commands = malloc(4096 * sizeof(char*));

        if (line == NULL) {
            break;
        }
        add_history(line);

        commands = pipecheck(line);
        commandslength = checklength(commands);
        char result[1024];

        int out = checkRedirect(commands[commandslength - 1]);
        char* file = getFilename(commands[commandslength - 1]);

        for (int i = 0; i < commandslength; i++) {

            char** words = malloc(4096 * sizeof(char*));
            strcpy(result, "");

            words = splitcommand(commands[i]);
            int wordslength = checklength(words);

            if (wordslength > 0 && strcmp(words[0], "cp") == 0) {
                cp(wordslength, words);

            }
            else if (wordslength > 0 && strcmp(words[0], "tee") == 0) {
                strcpy(result, "");
                tee(wordslength, words, &result, prevOutput);
                strcpy(prevOutput, result);

            }
            else if (wordslength > 0 && strcmp(words[0], "dirname") == 0) {
                for (int i = 1; i < wordslength; i++) {
                    char* path1 = dirname(words[i]);
                    // printf("%s\n", path1);
                    strcat(result, path1);
                    strcat(result, "\n");
                    free(path1);
                }

            }
            else if (wordslength > 0 && strcmp(words[0], "help") == 0) {
                help();

            }
            else if (wordslength > 0 && strcmp(words[0], "version") == 0) {
                version();

            }
            else if (wordslength > 0 && strcmp(words[0], "exit") == 0) {
                runnin = false;

            }
            else {
                strcpy(result, "");
                externalcomm(words, &result, prevOutput);
                strcpy(prevOutput, result);
            }
        }

        result[strlen(result) + 1] = '\0';

        if (out == 0) {
            printf("%s", result);
        }
        if (out == 1) {
            FILE* file2 = fopen(file, "w");
            fwrite(result, sizeof(char), strlen(result), file2);
            fclose(file2);
        }
        if (out == 2) {
            FILE* file3 = fopen(file, "a");
            fwrite(result, sizeof(char), strlen(result), file3);
            fclose(file3);
        }

    }
}