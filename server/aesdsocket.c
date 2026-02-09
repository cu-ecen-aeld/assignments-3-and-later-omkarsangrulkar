#define _GNU_SOURCE
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

void signal_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        shutdown_requested = 1;
    }
}

void cleanup_and_exit(int server_fd) {
    syslog(LOG_INFO, "Caught signal, exiting");
    if (server_fd != -1) {
        close(server_fd);
    }
    unlink(DATA_FILE);
    closelog();
    exit(0);
}

int main(int argc, char *argv[]) {
    bool daemon_mode = false;
    int server_fd = -1, client_fd = -1;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received, bytes_read;
    int data_fd;
    
    // Check for -d flag
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = true;
    }
    
    // Open syslog
    openlog("aesdsocket", LOG_PID, LOG_USER);
    
    // Setup signal handlers
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        syslog(LOG_ERR, "sigaction SIGINT failed: %s", strerror(errno));
        return -1;
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        syslog(LOG_ERR, "sigaction SIGTERM failed: %s", strerror(errno));
        return -1;
    }
    
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
        
        // Redirect standard files to /dev/null
        int fd = open("/dev/null", O_RDWR);
        if (fd != -1) {
            dup2(fd, STDIN_FILENO);
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > 2) {
                close(fd);
            }
        }
    }
    
    // Listen
    if (listen(server_fd, 10) < 0) {
        syslog(LOG_ERR, "Listen failed: %s", strerror(errno));
        cleanup_and_exit(server_fd);
    }
    
    // Accept loop
    while (!shutdown_requested) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR && shutdown_requested) {
                break;
            }
            syslog(LOG_ERR, "Accept failed: %s", strerror(errno));
            continue;
        }
        
        // Log connection
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);
        
        // Receive data and write to file
        while (!shutdown_requested) {
            bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
            if (bytes_received <= 0) {
                break;
            }
            
            buffer[bytes_received] = '\0';
            
            // Write to file
            data_fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (data_fd >= 0) {
                write(data_fd, buffer, bytes_received);
                close(data_fd);
            }
            
            // Check for newline
            if (strchr(buffer, '\n') != NULL) {
                // Send file contents back
                data_fd = open(DATA_FILE, O_RDONLY);
                if (data_fd >= 0) {
                    while ((bytes_read = read(data_fd, buffer, sizeof(buffer))) > 0) {
                        send(client_fd, buffer, bytes_read, 0);
                    }
                    close(data_fd);
                }
                // Don't break - keep connection open for more packets
            }
        }
        
        syslog(LOG_INFO, "Closed connection from %s", client_ip);
        close(client_fd);
        client_fd = -1;
    }
    
    cleanup_and_exit(server_fd);
    return 0;
}