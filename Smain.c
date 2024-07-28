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

#define PORT 8080
#define BUFFER_SIZE 1024

void handle_client(int client_socket, char *buffer);
void prcclient(int client_socket);
void handle_ufile(int client_socket, char *filename, char *destination_path);
void create_dir(const char *path);
void send_file_to_server(const char *server_ip, int server_port, const char *filename, const char *destination_path);
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

    printf("Smain server is listening on port %d\n", PORT);

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
            prcclient(client_socket);
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

void prcclient(int client_socket) {
    char buffer[BUFFER_SIZE];
    int bytes_read;

    while (1) {
        bytes_read = recv(client_socket, buffer, BUFFER_SIZE, 0);
        if (bytes_read <= 0) {
            perror("recv failed");
            close(client_socket);
            return;
        }

        buffer[bytes_read] = '\0';
        printf("Received command: %s\n", buffer);

        handle_client(client_socket, buffer);
    }
}

void handle_client(int client_socket, char *buffer) {
    char command[BUFFER_SIZE];
    char filename[BUFFER_SIZE];
    char destination_path[BUFFER_SIZE];

    sscanf(buffer, "%s %s %s", command, filename, destination_path);

    if (strcmp(command, "ufile") == 0) {
        handle_ufile(client_socket, filename, destination_path);
    } else {
        printf("Unknown command: %s\n", command);
        send(client_socket, "Invalid command\n", 16, 0);
    }
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

void handle_ufile(int client_socket, char *filename, char *destination_path) {
    char buffer[BUFFER_SIZE];
    char expanded_path[BUFFER_SIZE];
    char server_message[BUFFER_SIZE];
    char server_ip[BUFFER_SIZE];
    int server_port;
    int file;

    expand_path(expanded_path, destination_path);

    if (strstr(filename, ".pdf") != NULL) {
        // Transfer to Spdf server
        snprintf(server_message, BUFFER_SIZE, "ufile %s %s", filename, expanded_path);
        strcpy(server_ip, "127.0.0.1");
        server_port = 8081;
    } else if (strstr(filename, ".txt") != NULL) {
        // Transfer to Stext server
        snprintf(server_message, BUFFER_SIZE, "ufile %s %s", filename, expanded_path);
        strcpy(server_ip, "127.0.0.1");
        server_port = 8082;
    } else if (strstr(filename, ".c") != NULL) {
        // Store locally on Smain server
        create_dir(expanded_path);
        snprintf(buffer, BUFFER_SIZE, "%s/%s", expanded_path, filename);
        file = open(buffer, O_WRONLY | O_CREAT, 0666);
        if (file < 0) {
            perror("File creation failed");
            return;
        }
        send(client_socket, "ready", strlen("ready"), 0);
        int bytes_read;
        while ((bytes_read = recv(client_socket, buffer, BUFFER_SIZE, 0)) > 0) {
            write(file, buffer, bytes_read);
        }
        close(file);
        snprintf(buffer, BUFFER_SIZE, "File %s uploaded successfully\n", filename);
        send(client_socket, buffer, strlen(buffer), 0);
        return;
    } else {
        snprintf(buffer, BUFFER_SIZE, "Unsupported file type: %s\n", filename);
        send(client_socket, buffer, strlen(buffer), 0);
        return;
    }

    send(client_socket, "ready", strlen("ready"), 0);
    send_file_to_server(server_ip, server_port, filename, expanded_path);
}


void send_file_to_server(const char *server_ip, int server_port, const char *filename, const char *destination_path) {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    char message[BUFFER_SIZE];
    int file;
    int bytes_read;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to server failed");
        close(sock);
        return;
    }

    // Adjust the destination path to replace /smain/ with /stext/ or /spdf/
    char adjusted_path[BUFFER_SIZE];
    strncpy(adjusted_path, destination_path, sizeof(adjusted_path) - 1);
    adjusted_path[sizeof(adjusted_path) - 1] = '\0'; // Ensure null termination
    char *pos;
    if ((pos = strstr(adjusted_path, "/smain/")) != NULL) {
        if (server_port == 8081) { // For PDF files
            strncpy(pos + 1, "spdf", 4); // Replace '/smain/' with '/spdf/'
            memmove(pos + 5, pos + 6, strlen(pos + 6) + 1); // Shift the rest of the string
        } else if (server_port == 8082) { // For TXT files
            strncpy(pos + 1, "stext", 5); // Replace '/smain/' with '/stext/'
            memmove(pos + 6, pos + 6, strlen(pos + 6) + 1); // Shift the rest of the string
        }
    }

    snprintf(message, BUFFER_SIZE, "ufile %s %s", filename, adjusted_path);
    send(sock, message, strlen(message), 0);

    // Open the file and send its contents
    file = open(filename, O_RDONLY);
    if (file < 0) {
        perror("File open failed");
        close(sock);
        return;
    }

    while ((bytes_read = read(file, buffer, BUFFER_SIZE)) > 0) {
        send(sock, buffer, bytes_read, 0);
    }

    close(file);
    close(sock);
}
