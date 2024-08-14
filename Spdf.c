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

#define PORT 8777
#define BUFFER_SIZE 1024

// Function declarations
void execRequest(int client_socket);
void createDirectory(const char *path);
void expandPathDir(char *expanded_path, const char *path);
void execRmfileOperation(int client_socket, char *filename);
void execDtarOperation(int client_socket);
void getFilepaths(const char *directory, const char *filetype, char *output);
void forwardTarContent(int client_socket, const char *directory, const char *file_pattern);
char* modifyPath(const char *original_path);

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size;
    pid_t child_pid;

    // Create a socket for the server
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    // Configure server address structure
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    memset(server_addr.sin_zero, '\0', sizeof(server_addr.sin_zero));

    // Bind the socket to the specified port and address
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Binding failed");
        close(server_socket);
        exit(1);
    }

     // Listen for incoming connections
    if (listen(server_socket, 10) < 0) {
        perror("Listening failed");
        close(server_socket);
        exit(1);
    }

    printf("Spdf server is listening on port %d\n", PORT);

    while (1) {
        addr_size = sizeof(client_addr);
        // Accept a new connection from a client
        client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &addr_size);
        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }

        printf("Connection accepted from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        
        // Create a child process to handle the client's request
        child_pid = fork();
        if (child_pid == 0) {
            close(server_socket);
            execRequest(client_socket); // Handle the client's request in the child process
            exit(EXIT_SUCCESS);
        } else if (child_pid < 0) {
            perror("Fork failed");
            close(client_socket);
        }
        close(client_socket); // Close the client socket in the parent process
    }

    close(server_socket); // Close the server socket when done
    return 0;
}

// Function to create a directory and its parents if they don't exist
void createDirectory(const char *path) {
    char tmp[BUFFER_SIZE];
    char *p = NULL;
    size_t len;

    // Copy the path to a temporary buffer
    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);

    // Remove the trailing slash if present
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }
    // Iterate over the path and create each directory in the hierarchy
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, S_IRWXU); // Create directory with read, write, execute permissions
            *p = '/';
        }
    }
    mkdir(tmp, S_IRWXU); // Create the final directory
}

// Function to expand a path that begins with '~' to the user's home directory
void expandPathDir(char *expanded_path, const char *path) {
    if (path[0] == '~') {
        const char *home = getenv("HOME"); // Get the home directory from environment variables
        snprintf(expanded_path, BUFFER_SIZE, "%s%s", home, path + 1); // Expand the path
    } else {
        snprintf(expanded_path, BUFFER_SIZE, "%s", path); // Copy the path as is if it doesn't start with '~'
    }
}

// Function to handle incoming client requests
void execRequest(int client_socket) {
    char buffer[BUFFER_SIZE];
    char command[BUFFER_SIZE];
    char filename[BUFFER_SIZE];
    char destination_path[BUFFER_SIZE];
    char expanded_path[BUFFER_SIZE];
    int bytes_read;
    int file;

    // Receive the command from the client
    bytes_read = recv(client_socket, buffer, BUFFER_SIZE, 0);
    if (bytes_read <= 0) {
        perror("recv failed");
        close(client_socket);
        return;
    }

    buffer[bytes_read] = '\0';
    printf("Received request: %s\n", buffer);

    // Parse the command, filename, and destination path from the received buffer
    sscanf(buffer, "%s %s %s", command, filename, destination_path);
    expandPathDir(expanded_path, destination_path); // Expand the destination path if needed

    if (strcmp(command, "ufile") == 0) {
        createDirectory(expanded_path); // Ensure the destination directory exists
        snprintf(buffer, BUFFER_SIZE, "%s/%s", expanded_path, filename); // Construct the full file path
        file = open(buffer, O_WRONLY | O_CREAT, 0666);  // Open or create the file with write permissions
        if (file < 0) {
            perror("File creation failed");
            return;
        }

        // Receive file data from the client and write it to the file
        while ((bytes_read = recv(client_socket, buffer, BUFFER_SIZE, 0)) > 0) {
            write(file, buffer, bytes_read);
        }

        close(file); // Close the file
        snprintf(buffer, BUFFER_SIZE, "File %s uploaded successfully!\n", filename); // Send a success message to the client
        send(client_socket, buffer, strlen(buffer), 0);
    } else if (strcmp(command, "dtar") == 0) {
        execDtarOperation(client_socket); // Handle "dtar" command (create and send a tar archive)
    } else if (strcmp(command, "rmfile") == 0) {
        execRmfileOperation(client_socket, filename);  // Handle "rmfile" command (remove a file)
    } else if (strcmp(command, "display") == 0) {
        char output[BUFFER_SIZE * 10] = ""; // Buffer to hold the list of filenames

        getFilepaths(filename, ".pdf", output); // Collect filenames with the specified extension

        // Print the collected files for debugging
        printf("Collected files in Spdf:\n%s\n", output);

        // Send the collected files to the client
        send(client_socket, output, strlen(output), 0);
        
        // Close the client socket to indicate end of transmission
        close(client_socket);
    } else if (strcmp(command, "dfile") == 0) {
        // Handle "dfile" command (download a file)
        expandPathDir(expanded_path, filename); // Expand the file path if needed
        file = open(expanded_path, O_RDONLY); // Open the file for reading
        if (file < 0) {
            // Send an error message to the client if the file cannot be opened
            snprintf(buffer, BUFFER_SIZE, "Error: File / Directory does not exist! Please check the file path provided!\n");
            send(client_socket, buffer, strlen(buffer), 0);
            close(client_socket);
            return;
        }

        // Read the file content and send it back to the client
        while ((bytes_read = read(file, buffer, BUFFER_SIZE)) > 0) {
            send(client_socket, buffer, bytes_read, 0);
        }

        close(file); // Close the file
        send(client_socket, "END_OF_FILE", strlen("END_OF_FILE"), 0); // Indicate end of file transfer
    } else {
        snprintf(buffer, BUFFER_SIZE, "Invalid command\n"); // Handle invalid commands
        send(client_socket, buffer, strlen(buffer), 0);
    }
}

// Function to handle the "rmfile" command (remove a file)
void execRmfileOperation(int client_socket, char *filename) {
    char buffer[BUFFER_SIZE];
    char expanded_path[BUFFER_SIZE];
    char * newFilename[BUFFER_SIZE];

    expandPathDir(expanded_path, filename); // Expand the file path if needed

    // Attempt to remove the file
    if (remove(expanded_path) == 0) {
        snprintf(buffer, BUFFER_SIZE, "File deleted successfully!\n"); // Success message
    } else {
        snprintf(buffer, BUFFER_SIZE, "Wrong file/directory entered! Please check: %s\n", modifyPath(filename)); // Error message
    }
    send(client_socket, buffer, strlen(buffer), 0);
}

// Function to convert a file path by replacing "/spdf/" with "/smain/"
char* modifyPath(const char *original_path) {
    // Allocate memory for the converted path
    char *converted_path = (char *)malloc(strlen(original_path) + 1);
    if (converted_path == NULL) {
        perror("Failed to allocate memory");
        return NULL;
    }

    // Copy the original path to the converted path
    strcpy(converted_path, original_path);
    char *pos;
    // Replace /spdf/ with /smain/
    pos = strstr(converted_path, "/spdf/");
    if (pos != NULL) {
        // Create a new temporary buffer to hold the modified string
        char temp_buffer[BUFFER_SIZE];
        snprintf(temp_buffer, sizeof(temp_buffer), "/smain/%s", pos + 6);
        strcpy(pos, temp_buffer);
    }

    return converted_path;
}

// Function to handle the "dtar" command (create and send a tar archive)
void execDtarOperation(int client_socket) {
    const char* directory = "~/spdf";  // Directory to search for files
    const char* file_pattern = "*.pdf"; // File pattern to match

    forwardTarContent(client_socket, directory, file_pattern); // Create and send the tar archive
}

// Function to create and send the content of a tar archive
void forwardTarContent(int client_socket, const char *directory, const char *file_pattern) {
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

// Function to collect filenames of a specified type in a directory
void getFilepaths(const char *directory, const char *filetype, char *output) {
    char cmd[BUFFER_SIZE];
    FILE *fp;

    // Adjust the find command to output full paths instead of basenames
    snprintf(cmd, sizeof(cmd), "find %s -type f -name '*%s'", directory, filetype);
    fp = popen(cmd, "r");
    if (fp == NULL) {
        perror("Failed to run command");
        return;
    }

    // Read the output of the find command and append it to the output buffer
    while (fgets(cmd, sizeof(cmd), fp) != NULL) {
        strcat(output, cmd);
    }

    pclose(fp);
}