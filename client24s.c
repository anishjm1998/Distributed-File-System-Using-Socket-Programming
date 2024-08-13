#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <libgen.h>
#include <sys/stat.h>

#define PORT 8197
#define BUFFER_SIZE 1024

void execute_command(int sock, char *command);
int validate_arguments(const char *command, int expected_arg_count, const char *valid_filetypes[]);
const char *get_basename(const char *path);

int main() {
    int sock;
    struct sockaddr_in server_addr;
    char command[BUFFER_SIZE];

    while (1) {
        // Create socket
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            perror("Socket creation failed");
            exit(EXIT_FAILURE);
        }

        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(PORT);
        inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

        // Connect to the server
        if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            perror("Connection to server failed");
            close(sock);
            exit(EXIT_FAILURE);
        }

        printf("Enter command: ");
        fgets(command, BUFFER_SIZE, stdin);
        command[strcspn(command, "\n")] = '\0'; // Remove newline character

        execute_command(sock, command);

        close(sock); // Close the socket after each command
    }

    return 0;
}

void execute_command(int sock, char *command) {
    char buffer[BUFFER_SIZE];
    int bytes_read;
    FILE *file = NULL;
    char filename[BUFFER_SIZE];
    char tar_name[BUFFER_SIZE];
    char destination_path[BUFFER_SIZE]; // Declare destination_path
    if (strncmp(command, "ufile ", 6) == 0) {
    if (!validate_arguments(command, 2, NULL)) {
        printf("Incorrect usage of ufile. Correct usage: ufile <filename> <destination_path>\n");
        return;
    }

    sscanf(command + 6, "%s %s", filename, destination_path); // Extract filename and destination path
    
    // Add file type validation here
    const char *extension = strrchr(filename, '.'); // Get the file extension
    if (extension == NULL || (strcmp(extension, ".c") != 0 && strcmp(extension, ".txt") != 0 && strcmp(extension, ".pdf") != 0)) {
        printf("Error: Invalid file type '%s'. Only .c, .txt, and .pdf files are allowed.\n", extension ? extension : "no extension");
        return; // Exit the function, do not send the command to the server
    }
    
    // Add the validation logic here
    char expanded_path[BUFFER_SIZE];
    const char *base_dir = "/smain";

    if (destination_path[0] == '~') {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(expanded_path, BUFFER_SIZE, "%s%s", home, destination_path + 1);
        } else {
            printf("Error: Unable to retrieve home directory.\n");
            return;
        }
    } else {
        strncpy(expanded_path, destination_path, BUFFER_SIZE);
    }


    if (strstr(expanded_path, base_dir) == NULL || strstr(expanded_path, base_dir) != expanded_path + strlen(getenv("HOME"))) {
        printf("Error: The destination path must be within the '%s' directory.\n", base_dir);
        return; // Exit the function, as the destination path is invalid
    }

    // Continue with the existing logic...
    if (access(filename, F_OK) != 0) {
        fprintf(stderr, "Error: File '%s' does not exist!\n", filename);
        return; // Exit the function, allowing the user to enter the next command
    }

        // Send the initial command to the server
        send(sock, command, strlen(command), 0);

        file = fopen(filename, "rb"); // Open the file to send
        if (file == NULL) {
            perror("File open failed");
            return;
        }
        printf("Sending file %s\n", filename);
        while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
            send(sock, buffer, bytes_read, 0);
        }
        fclose(file);
        // Read the final response from the server after file transfer
        bytes_read = recv(sock, buffer, BUFFER_SIZE, 0);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            printf("File %s uploaded successfully!\n", filename);
        } else {
            perror("recv failed");
        }

    } else if (strncmp(command, "rmfile ", 7) == 0) {
    if (!validate_arguments(command, 1, NULL)) {
        printf("Incorrect usage of rmfile. Correct usage: rmfile <filepath>\n");
        return;
    }

    sscanf(command + 7, "%s", filename); // Extract filename after "rmfile "

    // Add the validation logic here
    char expanded_path[BUFFER_SIZE];
    const char *base_dir = "/smain";

    if (filename[0] == '~') {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(expanded_path, BUFFER_SIZE, "%s%s", home, filename + 1);
        } else {
            printf("Error: Unable to retrieve home directory.\n");
            return;
        }
    } else {
        strncpy(expanded_path, filename, BUFFER_SIZE);
    }

    if (strstr(expanded_path, base_dir) == NULL) {
        printf("Error: The file path must be within the '%s' directory.\n", base_dir);
        return; // Exit the function, as the file path is invalid
    }

    // Send the rmfile command to the server
    send(sock, command, strlen(command), 0);

    // Read the server's response
    bytes_read = recv(sock, buffer, BUFFER_SIZE, 0);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        printf("Server response: %s\n", buffer);
    } else {
        perror("recv failed");
    }
}
 else if (strncmp(command, "dtar ", 5) == 0) {
        const char *valid_filetypes[] = {".c", ".pdf", ".txt", NULL};
        if (!validate_arguments(command, 1, valid_filetypes)) {
            printf("Incorrect usage of dtar. Correct usage: dtar <.c/.pdf/.txt>\n");
            return;
        }

        char filetype[BUFFER_SIZE];
        sscanf(command + 5, "%s", filetype);

        // Send the initial command to the server
        send(sock, command, strlen(command), 0);

        // Receive the response from the server
        bytes_read = recv(sock, buffer, BUFFER_SIZE, 0);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            
            if (strncmp(buffer, "No matching files found", 23) == 0) {
                printf("Server response: %s\n", buffer);
                return; // Exit since no tar file will be created
            }
        }

        // Determine the appropriate tar file name based on the file type
        if (strcmp(filetype, ".c") == 0) {
            strcpy(tar_name, "cfiles.tar");
        } else if (strcmp(filetype, ".pdf") == 0) {
            strcpy(tar_name, "pdf.tar");
        } else if (strcmp(filetype, ".txt") == 0) {
            strcpy(tar_name, "text.tar");
        } else {
            printf("Unsupported file type.\n");
            return;
        }

        FILE *file = fopen(tar_name, "wb");
        if (file == NULL) {
            perror("File creation failed");
            return;
        }

        while ((bytes_read = recv(sock, buffer, BUFFER_SIZE, 0)) > 0) {
            if (strncmp(buffer, "END_OF_FILE", 11) == 0) {
                break;
            }
            // Write the received content to the file
            fwrite(buffer, 1, bytes_read, file);
            // Clear the buffer for the next read
            memset(buffer, 0, BUFFER_SIZE);
        }

        fclose(file);
        printf("Tar file %s downloaded successfully.\n", tar_name);
    } else if (strncmp(command, "display ", 8) == 0) {
    int files_found = 0;  // Variable to track if any files are found
    if (!validate_arguments(command, 1, NULL)) {
        printf("Incorrect usage of display. Correct usage: display <pathname>\n");
        return;
    }

    sscanf(command + 8, "%s", destination_path); // Extract path after "display "

    // Add the validation logic here
    char expanded_path[BUFFER_SIZE];
    const char *base_dir = "/smain";

    if (destination_path[0] == '~') {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(expanded_path, BUFFER_SIZE, "%s%s", home, destination_path + 1);
        } else {
            printf("Error: Unable to retrieve home directory.\n");
            return;
        }
    } else {
        strncpy(expanded_path, destination_path, BUFFER_SIZE);
    }

    if (strstr(expanded_path, base_dir) == NULL) {
        printf("Error: The path must be within the '%s' directory.\n", base_dir);
        return; // Exit the function if the path is invalid
    }

    // Send the initial command to the server
    send(sock, command, strlen(command), 0);

    char buffer[BUFFER_SIZE];
    int bytes_read;

    while ((bytes_read = recv(sock, buffer, BUFFER_SIZE, 0)) > 0) {
        buffer[bytes_read] = '\0';

        // Split the buffer into individual file paths and process each one
        char *token = strtok(buffer, "\n");
        while (token != NULL) {
            char *relative_path = NULL;

            // Find the position of "smain", "spdf", or "stext" in the path
            if ((relative_path = strstr(token, "/smain/")) != NULL) {
                relative_path += strlen("/smain/"); // Adjust pointer based on length
            } else if ((relative_path = strstr(token, "/spdf/")) != NULL) {
                relative_path += strlen("/spdf/"); // Adjust pointer based on length
            } else if ((relative_path = strstr(token, "/stext/")) != NULL) {
                relative_path += strlen("/stext/"); // Adjust pointer based on length
            }

            // If found, print the part after the matched keyword, otherwise print the original token
            if (relative_path != NULL) {
                printf("%s\n", relative_path);
            } else {
                printf("%s\n", token);
            }

            files_found = 1;  // Mark that files were found
            token = strtok(NULL, "\n");
        }
    }

    if (!files_found) {
        printf("No files found in the given path!\n");
    }

    if (bytes_read < 0) {
        perror("recv failed");
    }

    printf("\n");
}
else if (strncmp(command, "dfile ", 6) == 0) {
        if (!validate_arguments(command, 1, NULL)) {
            printf("Incorrect usage of dfile. Correct usage: dfile <filepath>\n");
            return;
        }

        char filename[BUFFER_SIZE];
        char base_filename[BUFFER_SIZE];
        FILE *file = NULL;
        int bytes_read;
        int content_received = 0;  // Flag to check if any content was received

        // Extract the filename from the command
        sscanf(command + 6, "%s", filename);
        
        // Validate the file type
        const char *valid_extensions[] = {".txt", ".pdf", ".c"};
        int is_valid_extension = 0;
        const char *extension = strrchr(filename, '.'); // Get the file extension

        if (extension != NULL) {
            for (int i = 0; i < sizeof(valid_extensions) / sizeof(valid_extensions[0]); i++) {
                if (strcmp(extension, valid_extensions[i]) == 0) {
                    is_valid_extension = 1;
                    break;
                }
            }
        }

        if (!is_valid_extension) {
            printf("Error: Unsupported file type '%s'. Supported file types are: .txt, .pdf, .c\n", extension);
            return;
        }
        // Send the initial command to the server
        send(sock, command, strlen(command), 0);

        // Get the base name of the file and make a mutable copy
        strncpy(base_filename, get_basename(filename), BUFFER_SIZE - 1);
        base_filename[BUFFER_SIZE - 1] = '\0';  // Ensure null termination

        // Open the file in write mode
        file = fopen(base_filename, "wb");
        if (file == NULL) {
            perror("File creation failed");
            return;
        }

        // Receive and write the file content
        while ((bytes_read = recv(sock, buffer, BUFFER_SIZE, 0)) > 0) {
            // Check for termination signal
            char *end_marker = strstr(buffer, "END_OF_FILE");
            if (end_marker != NULL) {
                fwrite(buffer, 1, end_marker - buffer, file);
                content_received = 1;
                break; // Stop receiving once the termination signal is found
            } else {
                fwrite(buffer, 1, bytes_read, file);
                content_received = 1;
            }
        }

        // Ensure file is closed if it was opened
        if (file != NULL) {
            fclose(file);
        }

        if (content_received) {
            printf("File %s downloaded successfully.\n", base_filename);
        } else {
            printf("No content received for file %s.\n", base_filename);
        }

        if (bytes_read < 0) {
            perror("recv failed");
        }
    } else {
        // Handle unknown command
        printf("Invalid command! Please enter a valid command from <ufile, dfile, rmfile, dtar, display>!\n");
    }

    // Return control to main loop
    return;
}

int validate_arguments(const char *command, int expected_arg_count, const char *valid_filetypes[]) {
    char args[BUFFER_SIZE];
    int arg_count = 0;
    char *token;
    char temp_command[BUFFER_SIZE];
    
    // Copy the command to a temporary string
    strncpy(temp_command, command, BUFFER_SIZE);
    
    // Extract the command name (e.g., ufile, rmfile, etc.)
    token = strtok(temp_command, " ");
    
    // Extract and count the arguments
    while ((token = strtok(NULL, " ")) != NULL) {
        arg_count++;
        strcpy(args, token);  // Save the last argument for filetype validation
    }
    
    // Check the argument count
    if (arg_count != expected_arg_count) {
        return 0; // Invalid argument count
    }
    
    // If specific valid filetypes are provided, validate against them
    if (valid_filetypes != NULL && expected_arg_count == 1) {
        int valid = 0;
        for (int i = 0; valid_filetypes[i] != NULL; i++) {
            if (strcmp(args, valid_filetypes[i]) == 0) {
                valid = 1;
                break;
            }
        }
        if (!valid) {
            return 0; // Invalid filetype
        }
    }
    
    return 1; // Valid arguments
}

const char *get_basename(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}
