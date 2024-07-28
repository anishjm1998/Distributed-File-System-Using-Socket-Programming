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

#define PORT 8082
#define BUFFER_SIZE 1024

void handle_request(int client_socket);
void create_dir(const char *path);
void expand_path(char *expanded_path, const char *path);

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
    } else {
        snprintf(buffer, BUFFER_SIZE, "Invalid command\n");
        send(client_socket, buffer, strlen(buffer), 0);
    }
}
