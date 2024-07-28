#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUFFER_SIZE 1024

void execute_command(int sock, char *command);

int main() {
    int sock;
    struct sockaddr_in server_addr;
    char command[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to server failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    while (1) {
        printf("Enter command: ");
        fgets(command, BUFFER_SIZE, stdin);
        command[strcspn(command, "\n")] = '\0'; // Remove newline character

        execute_command(sock, command);
    }

    close(sock);
    return 0;
}

void execute_command(int sock, char *command) {
    char buffer[BUFFER_SIZE];
    int bytes_read;

    send(sock, command, strlen(command), 0);
    bytes_read = recv(sock, buffer, BUFFER_SIZE, 0);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        printf("Server response: %s\n", buffer);

        // If the server is ready to receive the file, send the file content
        if (strcmp(buffer, "ready") == 0) {
            char filename[BUFFER_SIZE];
            sscanf(command, "ufile %s", filename);

            int file = open(filename, O_RDONLY);
            if (file < 0) {
                perror("File open failed");
                return;
            }
            while ((bytes_read = read(file, buffer, BUFFER_SIZE)) > 0) {
                send(sock, buffer, bytes_read, 0);
            }
            close(file);
        }
    } else {
        perror("recv failed");
    }
}
