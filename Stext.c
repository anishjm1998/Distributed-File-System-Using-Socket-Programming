#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>

#define PORT 8666
#define BUFFER_SIZE 1024

void handle_request(int client_socket);
void create_dir(const char *path);
void expand_path(char *expanded_path, const char *path);
void handle_rmfile(int client_socket, char *filename);
void handle_dtar(int client_socket);
void collect_filenames(const char *directory, const char *filetype, char *output);
void send_tar_content(int client_socket, const char *directory, const char *file_pattern);
char* convert_path(const char *original_path);

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size;
    pid_t child_pid;

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    memset(server_addr.sin_zero, '\0', sizeof(server_addr.sin_zero));

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Binding failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, 10) < 0) {
        perror("Listening failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("Stext server is listening on port %d\n", PORT);

    while (1) {
        addr_size = sizeof(client_addr);
        client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &addr_size);
        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }

        printf("Connection accepted from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        child_pid = fork();
        if (child_pid == 0) {
            close(server_socket);
            handle_request(client_socket);
            exit(EXIT_SUCCESS);
        } else if (child_pid < 0) {
            perror("Fork failed");
            close(client_socket);
        }
        close(client_socket);
    }

    close(server_socket);
    return 0;
}

void create_dir(const char *path) {
    char tmp[BUFFER_SIZE];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, S_IRWXU);
            *p = '/';
        }
    }
    mkdir(tmp, S_IRWXU);
}

void expand_path(char *expanded_path, const char *path) {
    if (path[0] == '~') {
        const char *home = getenv("HOME");
        snprintf(expanded_path, BUFFER_SIZE, "%s%s", home, path + 1);
    } else {
        snprintf(expanded_path, BUFFER_SIZE, "%s", path);
    }
}

void handle_request(int client_socket) {
    char buffer[BUFFER_SIZE];
    char command[BUFFER_SIZE];
    char filename[BUFFER_SIZE];
    char destination_path[BUFFER_SIZE];
    char expanded_path[BUFFER_SIZE];
    int bytes_read;
    int file;

    bytes_read = recv(client_socket, buffer, BUFFER_SIZE, 0);
    if (bytes_read <= 0) {
        perror("recv failed");
        close(client_socket);
        return;
    }

    buffer[bytes_read] = '\0';
    printf("Received request: %s\n", buffer);

    sscanf(buffer, "%s %s %s", command, filename, destination_path);
    printf("\nCommand: %s", command);
    printf("\nFilename: %s", filename);
    printf("\nDestination Path: %s", destination_path);
    expand_path(expanded_path, destination_path);

    if (strcmp(command, "ufile") == 0) {
        create_dir(expanded_path); // Ensure the destination directory exists
        snprintf(buffer, BUFFER_SIZE, "%s/%s", expanded_path, filename);
        file = open(buffer, O_WRONLY | O_CREAT, 0666);
        if (file < 0) {
            perror("File creation failed");
            return;
        }

        while ((bytes_read = recv(client_socket, buffer, BUFFER_SIZE, 0)) > 0) {
            write(file, buffer, bytes_read);
        }

        close(file);
        snprintf(buffer, BUFFER_SIZE, "File %s stored successfully\n", filename);
        send(client_socket, buffer, strlen(buffer), 0);
    } else if (strcmp(command, "dtar") == 0) {
        handle_dtar(client_socket);
    } else if (strcmp(command, "rmfile") == 0) {
        handle_rmfile(client_socket, filename);
    } else if (strcmp(command, "display") == 0) {
        char output[BUFFER_SIZE * 10] = ""; // Adjust size as needed

        collect_filenames(filename, ".txt", output); // Use the mapped path
        // Print the collected files for debugging
        printf("Collected files in Stext:\n%s\n", output);

        // Send the collected files to the client
        send(client_socket, output, strlen(output), 0);
        
        // Close the client socket to indicate end of transmission
        close(client_socket);
    } else if (strcmp(command, "dfile") == 0) {
        // Open the requested file based on the modified path
        expand_path(expanded_path, filename);
        file = open(expanded_path, O_RDONLY);
        if (file < 0) {
            // Send an error message to the client
            snprintf(buffer, BUFFER_SIZE, "Error: File / Directory does not exist! Please check the file path provided!\n");
            send(client_socket, buffer, strlen(buffer), 0);
            close(client_socket);
            return;
        }

        // Read the file content and send it back to the client
        while ((bytes_read = read(file, buffer, BUFFER_SIZE)) > 0) {
            send(client_socket, buffer, bytes_read, 0);
        }

        close(file);
        send(client_socket, "END_OF_FILE", strlen("END_OF_FILE"), 0);
    }else {
        snprintf(buffer, BUFFER_SIZE, "Invalid command\n");
        send(client_socket, buffer, strlen(buffer), 0);
    }
}

void handle_rmfile(int client_socket, char *filename) {
    char buffer[BUFFER_SIZE];
    char expanded_path[BUFFER_SIZE];

    expand_path(expanded_path, filename);

    if (remove(expanded_path) == 0) {
        snprintf(buffer, BUFFER_SIZE, "File deleted successfully!\n");
    } else {
        snprintf(buffer, BUFFER_SIZE, "Wrong file/directory entered! Please check: %s\n", convert_path(filename));
    }
    send(client_socket, buffer, strlen(buffer), 0);
}

char* convert_path(const char *original_path) {
    // Allocate memory for the converted path
    char *converted_path = (char *)malloc(strlen(original_path) + 1);
    if (converted_path == NULL) {
        perror("Failed to allocate memory");
        return NULL;
    }

    // Copy the original path to the converted path
    strcpy(converted_path, original_path);

    // Replace /stext/ with /smain/
    char *pos = strstr(converted_path, "/stext/");
    if (pos != NULL) {
        // Create a new temporary buffer to hold the modified string
        char temp_buffer[BUFFER_SIZE];
        snprintf(temp_buffer, sizeof(temp_buffer), "/smain/%s", pos + 7);
        strcpy(pos, temp_buffer);
    }

    return converted_path;
}


void handle_dtar(int client_socket) {
    // Specify the directory and tar file name
    const char* directory = "~/stext"; 
    const char* file_pattern = "*.txt"; 

    send_tar_content(client_socket, directory, file_pattern);
}

void send_tar_content(int client_socket, const char *directory, const char *file_pattern) {
    char cmd[BUFFER_SIZE];
    FILE *fp;
    char buffer[BUFFER_SIZE];
    int bytes_read;

    // Expand the directory path if it contains '~'
    char full_directory[BUFFER_SIZE];
    if (directory[0] == '~') {
        const char *home_dir = getenv("HOME");
        snprintf(full_directory, sizeof(full_directory), "%s%s", home_dir, directory + 1);
    } else {
        strncpy(full_directory, directory, BUFFER_SIZE);
    }

    // First, check if there are any matching files
    snprintf(cmd, sizeof(cmd), "find %s -type f -name '%s' -print -quit", full_directory, file_pattern);
    fp = popen(cmd, "r");
    if (fp == NULL) {
        perror("Failed to run find command");
        send(client_socket, "Error checking files\n", 21, 0);
        return;
    }

    // If no files found, inform the client and return
    if (fgets(buffer, sizeof(buffer), fp) == NULL) {
        send(client_socket, "No matching files found\n", 24, 0);
        pclose(fp);
        return;
    }
    pclose(fp);

    // Proceed with tar creation using the simplified find and tar command
    snprintf(cmd, sizeof(cmd), "cd %s && find . -type f -name '%s' | tar -cvf - -T -", full_directory, file_pattern);
    fp = popen(cmd, "r");
    if (fp == NULL) {
        perror("Failed to run tar command");
        send(client_socket, "Error creating tar\n", 19, 0);
        return;
    }

    // Server-side: Send a small signal before the actual data
    send(client_socket, "START_OF_FILE", strlen("START_OF_FILE"), 0);

    // Stream the tar content to the client
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, fp)) > 0) {
        send(client_socket, buffer, bytes_read, 0);
    }

    pclose(fp);
    send(client_socket, "END_OF_FILE", strlen("END_OF_FILE"), 0);
}

void collect_filenames(const char *directory, const char *filetype, char *output) {
    char cmd[BUFFER_SIZE];
    FILE *fp;

    // Adjust the find command to output full paths instead of basenames
    snprintf(cmd, sizeof(cmd), "find %s -type f -name '*%s'", directory, filetype);
    fp = popen(cmd, "r");
    if (fp == NULL) {
        perror("Failed to run command");
        return;
    }

    while (fgets(cmd, sizeof(cmd), fp) != NULL) {
        strcat(output, cmd);
    }

    pclose(fp);
}
