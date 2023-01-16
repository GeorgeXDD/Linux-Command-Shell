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


void tee(int argc, char** argv) {

    int opt_a = 0;
    int opt;
    optind = 1;
    while ((opt = getopt(argc, argv, "a")) != -1) {
        switch (opt) {
            case 'a':
                opt_a = 1;
                break;
            default:
                fprintf(stderr, "Usage: tee [-a] [file...]\n");
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

    // Read from stdin and write to output files
    int ispipe = 0;
    if(!isatty(fileno(stdin))) {
        ispipe = 1;
    }

    if(ispipe) {
        char buffer[4096];
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, 4096, stdin)) > 0) {
            // Write to stdout
            fwrite(buffer, 1, bytes_read, stdout);

            // Write to output files
            for (int i = 0; i < num_filenames; i++) {
                fwrite(buffer, 1, bytes_read, files[i]);
            }
        }
    } else {
        struct termios oldt, newt;
        tcgetattr(STDIN_FILENO, &oldt);

        newt = oldt;
        newt.c_lflag &= ~ICANON;

        newt.c_cc[VMIN] = 1;
        newt.c_cc[VTIME] = 0;

        newt.c_lflag |= ECHO;

        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        while(1) {
            char input;
            int bytes_read = read(STDIN_FILENO, &input, 1);
            if(bytes_read == 0 || input == 0x04 || input == 0x14){
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
    char* dest = argv[argc-1];

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
            } else {
                source_filename++;
            }

            int dest_path_len = strlen(dest) + strlen(source_filename) + 2;

            dest_path = malloc(dest_path_len);

            snprintf(dest_path, dest_path_len, "%s/%s", dest, source_filename);
        } else {
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
                char* args[] = {"cp", "-r", source_entry_path, dest_entry_path, NULL};

                cp(4, args);

                free(source_entry_path);

                free(dest_entry_path);
            }

            closedir(source_dir);
        } else {
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
        int dir_lenx = last_slash_index+1;

        char* dir = malloc(dir_lenx+1);
        strncpy(dir, ".", dir_lenx);

        dir[dir_lenx] = '\0';
        return dir;
    }

    if (n_slash == 1 && last_slash_index == 0) {
        char* dir = malloc(last_slash_index+2);
        strncpy(dir, path, last_slash_index+1);

        dir[last_slash_index+1] = '\0';
        return dir;
    }

    int dir_len = last_slash_index;
    if(path[last_slash_index-1]== '/')
        while(path[dir_len-1]== '/' && dir_len >1) --dir_len;
    char* dir = malloc(dir_len+1);
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

#define MAX_LINE_LEN 1024
#define MAX_WORDS 128


int main(int argc, char** argv) {

    char* line;
    char* words[MAX_WORDS];

    while (true) {
        line = readline("terminal@project$> ");
        if (line == NULL) {
            break;
        }
        add_history(line);

        // Split input line into words
        int num_words = 0;
        char** words = malloc(4096 * sizeof(char*));
        char* word = strtok(line, " \t\n");
        while (word != NULL) {
            words[num_words] = word;
            num_words++;
            
            word = strtok(NULL, " \t\n");
        }
        

        // Check for empty input
        if (num_words == 0) {
            free(words);
            continue;
        }

        // Check for "exit" command
        if (strcmp(words[0], "exit") == 0) {
            break;
        }

        // Check for piping
        int pipe_index = -1;
        for (int i = 0; i < num_words; i++) {
            if (strcmp(words[i], "|") == 0) {
                pipe_index = i;
                break;
            }
        }
        if (pipe_index >= 0) {
            // Piping is present: create pipe and fork
            int pipefd[2];

            if (pipe(pipefd) != 0) {
                perror("Error creating pipe");

                exit(EXIT_FAILURE);
            }

            pid_t pid = fork();
            int status;
            if (pid == 0) {
                int test=1;

                   // Child process: redirect stdout to write end of pipe
                if (dup2(pipefd[1], STDOUT_FILENO) != STDOUT_FILENO) {
                    perror("Error redirecting stdout");

                    exit(EXIT_FAILURE);
                }

                // Close read end of pipe
                if (close(pipefd[0]) != 0) {
                    perror("Error closing read end of pipe");

                    exit(EXIT_FAILURE);
                }

                // Execute command preceding pipe
                char** command = words;
                int command_len = pipe_index;

                command[command_len] = NULL;
                
                execvp(command[0], command);
                // execvp failed: print error message

                perror("Error executing command");
                exit(EXIT_FAILURE);

            } else if (pid > 0) {
                // Parent process: redirect stdin to read end of pipe
                if (dup2(pipefd[0], STDIN_FILENO) != STDIN_FILENO) {
                    perror("Error redirecting stdin");

                    exit(EXIT_FAILURE);
                }

                // Close write end of pipe
                if (close(pipefd[1]) != 0) {
                    perror("Error closing write end of pipe");

                    exit(EXIT_FAILURE);
                }

                // Execute command following pipe
                char** command = &words[pipe_index + 1];
                int command_len = num_words - (pipe_index + 1);
                command[command_len] = NULL;

                if (num_words > 0 && strcmp(command[0], "tee") == 0) {

                    pid_t tee_pid = fork();

                    if (tee_pid == 0) {
                        tee(command_len, command);
                        exit(EXIT_SUCCESS);

                    } else if (tee_pid > 0) {
                        // Parent process: wait for tee command to finish

                        int status;

                        waitpid(tee_pid, &status, 0);
                    } else {
                        perror("Error forking process for tee command");

                        exit(EXIT_FAILURE);
                    }
                }else {
                    // Other command: execute in child process
                    pid_t cmd_pid = fork();

                    if (cmd_pid == 0) {
                        execvp(command[0], command);

                        // execvp failed: print error message

                        perror("Error executing command");
                        exit(EXIT_FAILURE);
                    } else if (cmd_pid > 0) {
                        // Parent process: wait for command to finish

                        int status;
                        waitpid(cmd_pid, &status, 0);
                    }else{
                        perror("Error forking process for command");

                        exit(EXIT_FAILURE);
                    }
                }
            } else {

                perror("Error forking process");
                exit(EXIT_FAILURE);
            }
        } else {
            int ok=0;

            // Piping is not present: execute command directly
            if (num_words > 0 && strcmp(words[0], "cp") == 0) {
                ok=1;
                cp(num_words, words);

            } else if (num_words > 0 && strcmp(words[0], "tee") == 0) {
                ok=1;
                tee(num_words, words);

            } else if (num_words > 0 && strcmp(words[0], "dirname") == 0) {
                if (num_words > 3) {
                    ok=1;
                    fprintf(stderr, "Error: dirname requires at least one or at most two arguments\n");

                } else if( num_words == 3){
                    ok=1;

                    char* path1 = dirname(words[1]);
                    char* path2 = dirname(words[2]);

                    printf("%s\n", path1);
                    printf("%s\n", path2);

                    free(path1);
                    free(path2);
                }
                else {
                    ok=1;
                    char* path1 = dirname(words[1]);

                    printf("%s\n", path1);

                    free(path1);
                }
            }else if(num_words > 0 && strcmp(words[0], "help") == 0) {
                help();
            }else if(num_words > 0 && strcmp(words[0], "version") == 0) {
                version();
            }else if (num_words > 0 && strcmp(words[0], "exit") == 0) {

                    break;
            }else {

                if(ok==0){
                    int pid = fork();
                    if (pid == 0) {

                        // child process
                        execvp(words[0], words);

                        printf("Error! No such command exists.\n");
                        exit(1);

                    } else if (pid > 0) {

                        wait(NULL);

                    } else {

                        printf("Fork failed!\n");

                    }
                }
            }
            ok=0;
        }

        free(words);
    }

    return 0;

}