#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>

#define PORT 9000
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024

volatile sig_atomic_t shutdown_requested = 0;
int server_fd = -1;
int client_fd = -1;

void signal_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        syslog(LOG_INFO, "Caught signal, exiting");
        shutdown_requested = 1;
        
        if (client_fd != -1) {
            close(client_fd);
            client_fd = -1;
        }
        if (server_fd != -1) {
            close(server_fd);
            server_fd = -1;
        }
        unlink(DATA_FILE);
        closelog();
        exit(0);
    }
}

int main(int argc, char *argv[]) {
    bool daemon_mode = false;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char *line_buffer = NULL;
    size_t line_size = 0;
    size_t line_pos = 0;
    
    // Check for -d flag
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = true;
    }
    
    // Open syslog
    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);
    
    // Setup signal handlers
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        syslog(LOG_ERR, "Socket creation failed: %s", strerror(errno));
        closelog();
        return -1;
    }
    
    // Set SO_REUSEADDR
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        syslog(LOG_ERR, "setsockopt failed: %s", strerror(errno));
        close(server_fd);
        closelog();
        return -1;
    }
    
    // Bind socket
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        syslog(LOG_ERR, "Bind failed: %s", strerror(errno));
        close(server_fd);
        closelog();
        return -1;
    }
    
    // Daemonize if requested
    if (daemon_mode) {
        pid_t pid = fork();
        if (pid < 0) {
            syslog(LOG_ERR, "Fork failed: %s", strerror(errno));
            close(server_fd);
            closelog();
            return -1;
        }
        if (pid > 0) {
            // Parent exits
            exit(0);
        }
        
        // Child continues
        if (setsid() < 0) {
            syslog(LOG_ERR, "setsid failed: %s", strerror(errno));
            close(server_fd);
            closelog();
            return -1;
        }
        
        chdir("/");
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        
        open("/dev/null", O_RDONLY);
        open("/dev/null", O_WRONLY);
        open("/dev/null", O_WRONLY);
    }
    
    // Listen
    if (listen(server_fd, 10) < 0) {
        syslog(LOG_ERR, "Listen failed: %s", strerror(errno));
        close(server_fd);
        closelog();
        return -1;
    }
    
    // Accept loop
    while (!shutdown_requested) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            syslog(LOG_ERR, "Accept failed: %s", strerror(errno));
            continue;
        }
        
        // Log connection
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);
        
        // Receive data until newline
        line_size = BUFFER_SIZE;
        line_buffer = malloc(line_size);
        if (!line_buffer) {
            syslog(LOG_ERR, "malloc failed");
            close(client_fd);
            client_fd = -1;
            continue;
        }
        line_pos = 0;
        
        while (!shutdown_requested) {
            char recv_buf[BUFFER_SIZE];
            ssize_t bytes_recv = recv(client_fd, recv_buf, sizeof(recv_buf), 0);
            
            if (bytes_recv <= 0) {
                if (bytes_recv == 0) {
                    // Connection closed
                    break;
                }
                if (errno == EINTR) continue;
                syslog(LOG_ERR, "recv failed: %s", strerror(errno));
                break;
            }
            
            // Append to line buffer
            for (ssize_t i = 0; i < bytes_recv; i++) {
                if (line_pos >= line_size - 1) {
                    line_size *= 2;
                    char *new_buf = realloc(line_buffer, line_size);
                    if (!new_buf) {
                        syslog(LOG_ERR, "realloc failed");
                        free(line_buffer);
                        close(client_fd);
                        client_fd = -1;
                        goto next_client;
                    }
                    line_buffer = new_buf;
                }
                
                line_buffer[line_pos++] = recv_buf[i];
                
                if (recv_buf[i] == '\n') {
                    // Complete packet received
                    line_buffer[line_pos] = '\0';
                    
                    // Append to file
                    int fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
                    if (fd < 0) {
                        syslog(LOG_ERR, "open failed: %s", strerror(errno));
                    } else {
                        write(fd, line_buffer, line_pos);
                        close(fd);
                    }
                    
                    // Send file contents back
                    fd = open(DATA_FILE, O_RDONLY);
                    if (fd >= 0) {
                        char send_buf[BUFFER_SIZE];
                        ssize_t bytes_read;
                        while ((bytes_read = read(fd, send_buf, sizeof(send_buf))) > 0) {
                            send(client_fd, send_buf, bytes_read, 0);
                        }
                        close(fd);
                    }
                    
                    // Reset for next packet
                    line_pos = 0;
                }
            }
        }
        
        free(line_buffer);
        line_buffer = NULL;
        
next_client:
        syslog(LOG_INFO, "Closed connection from %s", client_ip);
        close(client_fd);
        client_fd = -1;
    }
    
    close(server_fd);
    closelog();
    return 0;
}
