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
#include <libgen.h>
#include <errno.h>
#include <sys/wait.h>

#define PORT 8998
#define BUFFER_SIZE 1024

// Function declarations
void execClientOperations(int client_socket, char *buffer);
void prcclient(int client_socket);
void execUfileOperation(int client_socket, char *filename, char *destination_path);
void functionToCreateNewDirectory(const char *path);
void sendFileContentToServer(const char *server_ip, int server_port, const char *filename, const char *destination_path);
void expandPathDir(char *expanded_path, const char *path);
void execRmfileOperation(int client_socket, char *filename);
void forwardTarRequestToServer(int client_socket, const char *tar_name, const char *server_ip, int server_port);
void execDtarOperation(int client_socket, const char *filetype);
void forwardFileContentToClient(int client_socket, const char *filename);
void execDisplayOperation(int client_socket, const char *directory);
void getFilePathsFromServer(const char *server_ip, int server_port, const char *directory, char *output);
void getFilepaths(const char *directory, const char *filetype, char *output);
void execDfileOperation(int client_socket, const char *filename);
void processCFileDownload(int client_socket, const char *filename);
void modifyPathForServer(char *path, const char *old_part, const char *new_part);
void processTextFileDownload(int client_socket, const char *filename);
void processPdfFileDownload(int client_socket, const char *filename);
void forwardContToServer(const char *server_ip, int server_port, int client_socket, const char *command, const char *filename);
void retrieveAndSendTarContent(int client_socket, const char *directory, const char *file_pattern);

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size;
    pid_t child_pid;

    // Creates a socket for the server
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        // If socket creation fails, print error and exit
        perror("Socket creation failed");
        exit(1);
    }

    // Set up the server address structure
    server_addr.sin_family = AF_INET; // IPv4
    server_addr.sin_port = htons(PORT); // Convert port number to network byte order
    server_addr.sin_addr.s_addr = INADDR_ANY; // Bind to all available interfaces
    memset(server_addr.sin_zero, '\0', sizeof(server_addr.sin_zero)); // Zero out the rest of the structure

    // Bind the socket to the specified port and address
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        // If binding fails, print error, close socket, and exit
        perror("Binding failed");
        close(server_socket);
        exit(1);
    }

    // Listen for incoming connections on the socket
    if (listen(server_socket, 10) < 0) {
        // If listening fails, print error, close socket, and exit
        perror("Listening failed");
        close(server_socket);
        exit(1);
    }

    printf("Smain server is listening on port %d\n", PORT);

    while (1) {
        addr_size = sizeof(client_addr);
        // Accept an incoming connection
        client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &addr_size);
        if (client_socket < 0) {
            // If accept fails, print error and continue to the next iteration
            perror("Accept failed");
            continue;
        }

        printf("Connection accepted from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        // Fork a new process to handle the client
        child_pid = fork();
        if (child_pid == 0) {
            // In the child process: close the server socket, handle the client, and exit
            close(server_socket);
            prcclient(client_socket);
            exit(EXIT_SUCCESS);
        } else if (child_pid < 0) {
            // If fork fails, print error and close the client socket
            perror("Fork failed");
            close(client_socket);
        }
        // In the parent process, close the client socket
        close(client_socket);
    }

    // Close the server socket when done (although this point is never reached)
    close(server_socket);
    return 0;
}

// Function to process client requests
void prcclient(int client_socket) {
    char buffer[BUFFER_SIZE];
    int bytes_read;

    while (1) {
        // Receive a message from the client
        bytes_read = recv(client_socket, buffer, BUFFER_SIZE, 0);
        if (bytes_read <= 0) {
            if (bytes_read == 0) {
                // Connection closed by the client
                // printf("Client disconnected\n"); // Commented out for silent operation
            } else {
                // Error receiving data
                perror("recv failed");
            }
            // Close the client socket and return
            close(client_socket);
            return;
        }

        // Null-terminate the received data to safely handle it as a string
        buffer[bytes_read] = '\0';
        printf("Received command: %s\n", buffer);

        // Execute the appropriate client operation based on the received command
        execClientOperations(client_socket, buffer);
    }
}

// Function to handle client operations based on the command received
void execClientOperations(int client_socket, char *buffer) {
    char command[BUFFER_SIZE];
    char filename[BUFFER_SIZE];
    char destination_path[BUFFER_SIZE];

    // Parse the command and arguments from the received buffer
    sscanf(buffer, "%s %s %s", command, filename, destination_path);

    // Execute the appropriate function based on the command
    if (strcmp(command, "ufile") == 0) { //upload file
        execUfileOperation(client_socket, filename, destination_path);
    } else if (strcmp(command, "rmfile") == 0) { //remove file
        execRmfileOperation(client_socket, filename);
    } else if (strcmp(command, "dtar") == 0) {//downlaod tar content
        execDtarOperation(client_socket, filename);
    } else if (strcmp(command, "display") == 0) {//display filename
        execDisplayOperation(client_socket, filename);
    } else if (strcmp(command, "dfile") == 0) {//download file
        execDfileOperation(client_socket, filename);
    } else {
        // Handle unknown commands
        printf("Unknown command: %s\n", command);
        send(client_socket, "Invalid command\n", 16, 0);
    }
}

// Function to create a new directory and its parent directories
void functionToCreateNewDirectory(const char *path) {
    char tmp[BUFFER_SIZE];
    char *p = NULL;
    size_t len;

    // Copy the path into a temporary buffer
    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0; // Remove the trailing slash if present
    }
    // Create directories iteratively from the root
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0; // Temporarily terminate the string to create intermediate directories
            mkdir(tmp, S_IRWXU);
            *p = '/'; // Restore the slash
        }
    }
    // Create the final directory
    mkdir(tmp, S_IRWXU);
}

// Function to expand a path, replacing '~' with the home directory
void expandPathDir(char *expanded_path, const char *path) {
    if (path[0] == '~') {
        // If the path starts with '~', replace it with the home directory
        const char *home = getenv("HOME");
        snprintf(expanded_path, BUFFER_SIZE, "%s%s", home, path + 1);
    } else {
        // Otherwise, just copy the path as is
        snprintf(expanded_path, BUFFER_SIZE, "%s", path);
    }
}

// Function to handle the "ufile" command, which uploads a file to the server
void execUfileOperation(int client_socket, char *filename, char *destination_path) {
    char buffer[BUFFER_SIZE];
    char expanded_path[BUFFER_SIZE];
    char server_message[BUFFER_SIZE];
    char server_ip[BUFFER_SIZE];
    int server_port;
    int file;

    // Expand the destination path to handle '~'
    expandPathDir(expanded_path, destination_path);

    // Check the file extension and determine the target server
    if (strstr(filename, ".pdf") != NULL) {
        // If the file is a PDF, transfer it to the Spdf server
        snprintf(server_message, BUFFER_SIZE, "ufile %s %s", filename, expanded_path);
        strcpy(server_ip, "127.0.0.1"); // Localhost IP
        server_port = 8777; // Port for Spdf server
        send(client_socket, "ready", strlen("ready"), 0); // Notify client to send file
        sendFileContentToServer(server_ip, server_port, filename, expanded_path);
        // Notify client of successful upload
        snprintf(buffer, BUFFER_SIZE, "File %s uploaded successfully to Spdf server\n", filename);
        send(client_socket, buffer, strlen(buffer), 0);
    } else if (strstr(filename, ".txt") != NULL) {
        // Transfer to Stext server
        printf("Filename1:%s\n", filename);
        snprintf(server_message, BUFFER_SIZE, "ufile %s %s", filename, expanded_path);
        strcpy(server_ip, "127.0.0.1"); // Localhost IP
        server_port = 8878; // Port for Stext server
        send(client_socket, "ready", strlen("ready"), 0); // Notify client to send file
        printf("Filename2:%s\n", filename);
        sendFileContentToServer(server_ip, server_port, filename, expanded_path);
        // Notify client of successful upload
        snprintf(buffer, BUFFER_SIZE, "File %s uploaded successfully to Stext server\n", filename);
        send(client_socket, buffer, strlen(buffer), 0);
    } else if (strstr(filename, ".c") != NULL) {
        // If the file is a C source file, store it locally on the Smain server
        // Create the destination directory if it doesn't already exist
        functionToCreateNewDirectory(expanded_path);
        printf("Filename1:%s\n", filename);
        // Prepare the full path where the file will be saved
        snprintf(buffer, BUFFER_SIZE, "%s/%s", expanded_path, filename);
        printf("Filename2:%s\n", filename);
        
        // Open the file for writing; create it if it doesn't exist
        file = open(buffer, O_WRONLY | O_CREAT, 0666);
        if (file < 0) {
            // Handle file creation error
            perror("File creation failed");
            snprintf(buffer, BUFFER_SIZE, "Error creating file %s\n", filename);
            send(client_socket, buffer, strlen(buffer), 0);
            return;
        }

        // Notify the client that the server is ready to receive the file
        send(client_socket, "ready", strlen("ready"), 0); // Notify client to send file
        printf("Server ready to receive file %s\n", filename);
        
        // Receive the file content from the client and write it to the file
        int bytes_read;
        while ((bytes_read = recv(client_socket, buffer, BUFFER_SIZE, 0)) > 0) {
            // Check for a termination signal indicating the end of file transfer
            if (strncmp(buffer, "END_OF_FILE", 11) == 0) {
                break;
            }
            // Write the received data to the file
            write(file, buffer, bytes_read);
        }

        // Close the file after writing all the data
        close(file);

        // Send a final response to the client indicating successful upload
        snprintf(buffer, BUFFER_SIZE, "File %s uploaded successfully\n", filename);
        printf("Sending final response for file %s\n", filename);
        send(client_socket, buffer, strlen(buffer), 0); // Send final response to client
        
        // Ensure the output buffer is flushed
        fflush(stdout);
    } else {
        // Handle unsupported file types
        snprintf(buffer, BUFFER_SIZE, "Unsupported file type: %s\n", filename);
        send(client_socket, buffer, strlen(buffer), 0);
    }
}

// Function to send file content to another server (e.g., Spdf or Stext)
void sendFileContentToServer(const char *server_ip, int server_port, const char *filename, const char *destination_path) {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    char message[BUFFER_SIZE];
    int file;
    int bytes_read;

    // Create a socket for communication with the remote server
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        // Handle socket creation error
        perror("Socket creation failed");
        return;
    }

    // Set up the server address structure
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    // Attempt to connect to the remote server
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        // Handle connection error
        perror("Connection to server failed");
        close(sock);
        return;
    }

    // Adjust the destination path to replace /smain/ with /stext/ or /spdf/
    char adjusted_path[BUFFER_SIZE];
    strncpy(adjusted_path, destination_path, sizeof(adjusted_path) - 1);
    adjusted_path[sizeof(adjusted_path) - 1] = '\0'; // Ensure null termination
    char *pos;
    if ((pos = strstr(adjusted_path, "/smain")) != NULL) {
        if (server_port == 8777) { // For PDF files
            strncpy(pos + 1, "spdf", 4); // Replace '/smain/' with '/spdf/'
            memmove(pos + 5, pos + 6, strlen(pos + 6) + 1); // Shift the rest of the string
        } else if (server_port == 8878) { // For TXT files
            strncpy(pos + 1, "stext", 5); // Replace '/smain/' with '/stext/'
            memmove(pos + 6, pos + 6, strlen(pos + 6) + 1); // Shift the rest of the string
        }
    }

    // Send the "ufile" command along with the filename and destination path to the server
    snprintf(message, BUFFER_SIZE, "ufile %s %s", filename, adjusted_path);
    send(sock, message, strlen(message), 0);

    // Introduce a small delay (e.g., 100 milliseconds) before sending the content
    usleep(100000); // 100,000 microseconds = 100 milliseconds

    // Open the file and send its contents
    file = open(filename, O_RDONLY);
    if (file < 0) {
        // Handle file opening error
        perror("File open failed");
        close(sock);
        return;
    }

    // Read the file content and send it to the remote server
    while ((bytes_read = read(file, buffer, BUFFER_SIZE)) > 0) {
        send(sock, buffer, bytes_read, 0);
    }

    // Close the file and the socket after the transfer is complete
    close(file);
    close(sock);
}

// Function to send a request to delete a file on a remote server
void sendRmRequestToServer(const char *server_ip, int server_port, const char *filename, int client_socket) {
    int sock;
    struct sockaddr_in server_addr;
    char message[BUFFER_SIZE];
    char adjusted_path[BUFFER_SIZE];

    // Adjust the file path by replacing "/smain/" with "/stext/" or "/spdf/"
    strncpy(adjusted_path, filename, sizeof(adjusted_path) - 1);
    adjusted_path[sizeof(adjusted_path) - 1] = '\0'; // Ensure null termination
    char *pos;

    // Adjust the filename path to replace /smain/ with /stext/ or /spdf/
    if ((pos = strstr(adjusted_path, "/smain/")) != NULL) {
        if (server_port == 8777) { // For PDF files
            strncpy(pos + 1, "spdf", 4); // Replace '/smain/' with '/spdf/'
            memmove(pos + 5, pos + 6, strlen(pos + 6) + 1); // Shift the rest of the string
        } else if (server_port == 8878) { // For TXT files
            strncpy(pos + 1, "stext", 5); // Replace '/smain/' with '/stext/'
            memmove(pos + 6, pos + 6, strlen(pos + 6) + 1); // Shift the rest of the string
        }
    }

    // Prepare the "rmfile" command to be sent to the remote server
    snprintf(message, BUFFER_SIZE, "rmfile %s", adjusted_path);

    // Create a socket for communication with the remote server
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        // Handle socket creation error
        perror("Socket creation failed");
        send(client_socket, "Error contacting server\n", 24, 0);
        return;
    }

    // Set up the server address structure
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    // Attempt to connect to the remote server
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        // Handle connection error
        perror("Connection to server failed");
        close(sock);
        send(client_socket, "Error contacting server\n", 24, 0);
        return;
    }
    // Send the delete request to the remote server
    send(sock, message, strlen(message), 0);

    // Read server response and forward it to the client
    int bytes_read;
    while ((bytes_read = recv(sock, message, BUFFER_SIZE, 0)) > 0) {
        send(client_socket, message, bytes_read, 0);
    }

    // Close the socket after the operation is complete
    close(sock);
}

// Function to execute the "rmfile" command, which deletes a file
void execRmfileOperation(int client_socket, char *filename) {
    char buffer[BUFFER_SIZE];
    char expanded_path[BUFFER_SIZE];
    char server_message[BUFFER_SIZE];
    char server_ip[BUFFER_SIZE];
    int server_port;

    // Expand the file path to handle '~'
    expandPathDir(expanded_path, filename);

    if (strstr(filename, ".pdf") != NULL) {
        // Request Spdf server to delete the file
        snprintf(server_message, BUFFER_SIZE, "rmfile %s", expanded_path);
        strcpy(server_ip, "127.0.0.1");
        server_port = 8777;
    } else if (strstr(filename, ".txt") != NULL) {
        // Request Stext server to delete the file
        snprintf(server_message, BUFFER_SIZE, "rmfile %s", expanded_path);
        strcpy(server_ip, "127.0.0.1");
        server_port = 8878;
    } else if (strstr(filename, ".c") != NULL) {
        // Delete the C file locally on the Smain server
        if (remove(expanded_path) == 0) {
            snprintf(buffer, BUFFER_SIZE, "File deleted successfully!\n");
        } else {
            snprintf(buffer, BUFFER_SIZE, "Wrong file/directory entered! Please check:  %s\n", filename);
        }
        // Send the response back to the client
        send(client_socket, buffer, strlen(buffer), 0);
        return;
    } else {
        // Handle unsupported file types
        snprintf(buffer, BUFFER_SIZE, "Unsupported file type: %s! Please provide a valid filename to delete!\n ", filename);
        send(client_socket, buffer, strlen(buffer), 0);
        return;
    }

    // Send the delete request to the appropriate remote server
    sendRmRequestToServer(server_ip, server_port, expanded_path, client_socket);
}

// Function to forward the content of a file to the client
void forwardFileContentToClient(int client_socket, const char *filename) {
    int file;
    char buffer[BUFFER_SIZE];
    int bytes_read;

    // Open the file for reading
    file = open(filename, O_RDONLY);
    if (file < 0) {
        // Handle file opening error
        snprintf(buffer, BUFFER_SIZE, "Error: File / Directory does not exist! Please check the file path provided!\n");
        send(client_socket, buffer, strlen(buffer), 0);
        return;
    }

    // Read the file content and send it to the client
    while ((bytes_read = read(file, buffer, BUFFER_SIZE)) > 0) {
        send(client_socket, buffer, bytes_read, 0);
    }

    // Close the file after the transfer is complete
    close(file);
    // Send termination signal to client
    // send(client_socket, "END_OF_FILE", strlen("END_OF_FILE"), 0);
    
    // Close the socket after sending all data
    close(client_socket);
}

// Function to handle the "dtar" command, which creates a tar archive of specified file types
void execDtarOperation(int client_socket, const char *filetype) {
    char tar_name[BUFFER_SIZE];

    if (strcmp(filetype, ".c") == 0) {
        // Handle .c files by creating a tar archive locally on Smain server
        retrieveAndSendTarContent(client_socket, "~/smain", "*.c");
    } else if (strcmp(filetype, ".pdf") == 0) {
        // Forward the request to Spdf server to create a tar archive of .pdf files
        forwardTarRequestToServer(client_socket, "pdf.tar", "127.0.0.1", 8777);
    } else if (strcmp(filetype, ".txt") == 0) {
        // Forward the request to Stext server to create a tar archive of .txt files
        forwardTarRequestToServer(client_socket, "text.tar", "127.0.0.1", 8878);
    } else {
        // Handle unsupported file types
        send(client_socket, "Invalid file type\n", 18, 0);
    }
}

// Function to forward a tar creation request to a remote server (e.g., Spdf or Stext)
void forwardTarRequestToServer(int client_socket, const char *file_pattern, const char *server_ip, int server_port) {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    int bytes_read;

    // Create a socket for communication with the remote server
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        // Handle socket creation error
        perror("Socket creation failed");
        return;
    }

    // Set up the server address structure
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    // Attempt to connect to the remote server
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        // Handle connection error
        perror("Connection to server failed");
        close(sock);
        return;
    }

    // Send the tar creation request to the remote server
    snprintf(buffer, sizeof(buffer), "dtar %s", file_pattern);
    send(sock, buffer, strlen(buffer), 0);

    // Receive the tar content from Spdf or Stext and forward it to the client
    while ((bytes_read = recv(sock, buffer, BUFFER_SIZE, 0)) > 0) {
        send(client_socket, buffer, bytes_read, 0);
    }

    // Close the socket after the operation is complete
    close(sock);

    // Notify the client that the tar file has been completely received
    send(client_socket, "END_OF_FILE", strlen("END_OF_FILE"), 0);
}

// Function to retrieve file paths from a remote server (e.g., Spdf or Stext)
void getFilePathsFromServer(const char *server_ip, int server_port, const char *directory, char *output) {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    int bytes_read;
    char message[BUFFER_SIZE];

    // Create a socket for communication with the remote server
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        // Handle socket creation error
        perror("Socket creation failed");
        return;
    }

    // Set up the server address structure
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    // Attempt to connect to the remote server
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        // Handle connection error
        perror("Connection to server failed");
        close(sock);
        return;
    }

    // Construct the correct display command with the fully mapped directory
    snprintf(message, sizeof(message), "display %s", directory);
    send(sock, message, strlen(message), 0);

    // Receive the list of files from the remote server and append it to the output
    while ((bytes_read = recv(sock, buffer, BUFFER_SIZE, 0)) > 0) {
        buffer[bytes_read] = '\0';
        strcat(output, buffer);
    }
    // Close the socket after receiving all the data
    close(sock);
} 

// Function to retrieve file paths of a specific type in a directory
void getFilepaths(const char *directory, const char *filetype, char *output) {
    FILE *fp;
    char commandop[BUFFER_SIZE];

    // Function to retrieve file paths of a specific type in a directory
    snprintf(commandop, sizeof(commandop), "find %s -type f -name '*%s'", directory, filetype);
    // Open a pipe to run the command
    fp = popen(commandop, "r");
    printf("%s", commandop);
    if (fp == NULL) {
        // Handle error running the command
        printf("Failed to run command");
        return;
    }
    
    // Read the command output and append it to the output buffer
    while (fgets(commandop, sizeof(commandop), fp) != NULL) {
        strcat(output, commandop);
    }

    // Close the pipe after reading all the output
    pclose(fp);
}

// Function to handle the "display" command, which lists files in a directory
void execDisplayOperation(int client_socket, const char *directory) {
    char buffer[BUFFER_SIZE];
    char output[BUFFER_SIZE * 10] = ""; // Adjust size as needed
    char mapped_directory[BUFFER_SIZE];
    char normalized_directory[BUFFER_SIZE];

    // Ensure the directory ends with a slash
    snprintf(normalized_directory, sizeof(normalized_directory), "%s", directory);
    if (normalized_directory[strlen(normalized_directory) - 1] != '/') {
        strcat(normalized_directory, "/");
    }

    printf("Directory: %s\n", normalized_directory);

    // Handle local collection of .c files in Smain
    getFilepaths(normalized_directory, ".c", output);

    // Prepare mapped paths and send them to the appropriate servers
    if (strstr(normalized_directory, "/smain/") != NULL) {
        // Mapping for Spdf server
        snprintf(mapped_directory, sizeof(mapped_directory), "%s", normalized_directory);
        char *pos;
        if ((pos = strstr(mapped_directory, "/smain/")) != NULL) {
            strncpy(pos + 1, "spdf", 4); // Replace '/smain/' with '/spdf/'
            memmove(pos + 5, pos + 6, strlen(pos + 6) + 1); // Adjust the rest of the path
        }
        getFilePathsFromServer("127.0.0.1", 8777, mapped_directory, output);

        // Mapping for Stext server
        snprintf(mapped_directory, sizeof(mapped_directory), "%s", normalized_directory);
        if ((pos = strstr(mapped_directory, "/smain/")) != NULL) {
            strncpy(pos + 1, "stext", 5); // Replace '/smain/' with '/stext/'
            memmove(pos + 6, pos + 6, strlen(pos + 6) + 1); // Adjust the rest of the path
        }
        getFilePathsFromServer("127.0.0.1", 8878, mapped_directory, output);
    }

    // Print the collected files for debugging
    printf("Collected files:\n%s\n", output);

    // Send the consolidated list to the client
    send(client_socket, output, strlen(output), 0);

    // Close the client socket to indicate the end of the message
    close(client_socket);
}

// Function to handle the "dfile" command, which downloads a file
void execDfileOperation(int client_socket, const char *filename) {
    char buffer[BUFFER_SIZE];
    char base_name[BUFFER_SIZE];
    char modified_path[BUFFER_SIZE];
    
    // Extract the base name of the file (i.e., remove the directory path)
    strcpy(base_name, basename((char *)filename));

    // Determine the file extension and handle accordingly
    if (strstr(base_name, ".c") != NULL) {
        // Handle .c files similar to .txt and .pdf by fetching content
        snprintf(modified_path, BUFFER_SIZE, "%s", filename);
        modifyPathForServer(modified_path, "smain", "smain");  // Keep the path within smain
        processCFileDownload(client_socket, modified_path);  // New function to handle .c files
    } else if (strstr(base_name, ".txt") != NULL) {
        // Handle .txt files by fetching them from Stext
        snprintf(modified_path, BUFFER_SIZE, "%s", filename);
        modifyPathForServer(modified_path, "smain", "stext");
        processTextFileDownload(client_socket, modified_path);
    } else if (strstr(base_name, ".pdf") != NULL) {
        // Handle .pdf files by fetching them from Spdf
        snprintf(modified_path, BUFFER_SIZE, "%s", filename);
        modifyPathForServer(modified_path, "smain", "spdf");
        processPdfFileDownload(client_socket, modified_path);
    } else {
        // Handle unsupported file types
        snprintf(buffer, BUFFER_SIZE, "Unsupported file type: %s\n", base_name);
        send(client_socket, buffer, strlen(buffer), 0);
    }
}

// Function to process the download of a .c file and send it to the client
void processCFileDownload(int client_socket, const char *filename) {
    char expanded_path[BUFFER_SIZE];
    // Expand the file path to handle '~'
    expandPathDir(expanded_path, filename);
    // Forward the file content to the client
    forwardFileContentToClient(client_socket, expanded_path);
}

// Function to modify a file path for server operations
void modifyPathForServer(char *path, const char *old_part, const char *new_part) {
    char *pos = strstr(path, old_part);
    if (pos != NULL) {
        // Replace the old part with the new part
        size_t old_len = strlen(old_part);
        size_t new_len = strlen(new_part);
        memmove(pos + new_len, pos + old_len, strlen(pos + old_len) + 1);
        memcpy(pos, new_part, new_len);
    }
}

// Function to process the download of a .txt file from the Stext server
void processTextFileDownload(int client_socket, const char *filename) {
    char expanded_path[BUFFER_SIZE];
    // Expand the file path to handle '~'
    expandPathDir(expanded_path, filename); 

    // Fetch the file from the Stext server and save it locally
    forwardContToServer("127.0.0.1", 8878, client_socket, "dfile", filename);

    // Send the fetched file content to the client
    forwardFileContentToClient(client_socket, expanded_path);
}

// Function to process the download of a .pdf file from the Spdf server
void processPdfFileDownload(int client_socket, const char *filename) {
    char expanded_path[BUFFER_SIZE];
    expandPathDir(expanded_path, filename);

    // Fetch the file from the Spdf server and save it locally
    forwardContToServer("127.0.0.1", 8777, client_socket, "dfile", filename);

    // Send the fetched file content to the client
    forwardFileContentToClient(client_socket, expanded_path);
}

// Function to forward a command and file request to a remote server and send the response back to the client
void forwardContToServer(const char *server_ip, int server_port, int client_socket, const char *command, const char *filename) {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    int bytes_read;

    // Create a socket for communication with the remote server
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        // Handle socket creation error
        perror("Socket creation failed");
        return;
    }

    // Set up the server address structure
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    // Attempt to connect to the remote server
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        // Handle connection error
        perror("Connection to server failed");
        close(sock);
        return;
    }

    // Send the entire dfile command with the path to the respective server
    snprintf(buffer, sizeof(buffer), "%s %s", command, filename);
    send(sock, buffer, strlen(buffer), 0);

    // Receive the file content from the remote server and forward it to the client
    while ((bytes_read = recv(sock, buffer, BUFFER_SIZE, 0)) > 0) {
        send(client_socket, buffer, bytes_read, 0);
    }

    close(sock); // Close the socket after the operation is complete
}

// Function to retrieve and send the contents of a tar archive to the client
void retrieveAndSendTarContent(int client_socket, const char *directory, const char *file_pattern) {
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

    // Close the pipe after sending all the data
    pclose(fp);
    // Notify the client that the tar file has been completely sent
    send(client_socket, "END_OF_FILE", strlen("END_OF_FILE"),0);
}