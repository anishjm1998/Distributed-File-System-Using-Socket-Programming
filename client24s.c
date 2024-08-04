#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8093
#define BUFFER_SIZE 1024

void execute_command(int sock, char *command);

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

    // Send the initial command to the server
    send(sock, command, strlen(command), 0);

    if (strncmp(command, "ufile ", 6) == 0) {
        sscanf(command + 6, "%s", filename); // Extract filename after "ufile "
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
            printf("Server response: %s\n", buffer);
        } else {
            perror("recv failed");
        }
    } else if (strncmp(command, "dtar ", 5) == 0) {
        printf("Executing command: %s\n", command); 
        sscanf(command + 5, "%s", filename); // Assume server sends back a tar file immediately after the command
        file = fopen(filename, "wb");
        if (file == NULL) {
            perror("File creation failed");
            return;
        }

        while ((bytes_read = recv(sock, buffer, BUFFER_SIZE, 0)) > 0) {
            if (strncmp(buffer, "END_OF_FILE", 11) == 0) {
                break;
            }
            fwrite(buffer, 1, bytes_read, file);
        }
        fclose(file);

        if (bytes_read < 0) {
            perror("recv failed");
        } else {
            printf("File %s downloaded successfully.\n", filename);
        }
    } else {
        // Handle general server responses
        bytes_read = recv(sock, buffer, BUFFER_SIZE, 0);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            printf("Server response: %s\n", buffer);
        } else {
            perror("recv failed");
        }
    }

    // Return control to main loop
    return;
}
