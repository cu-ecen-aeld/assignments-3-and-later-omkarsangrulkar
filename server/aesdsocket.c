#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdbool.h>

#define PORT 9000
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024

static int server_fd = -1;
static int client_fd = -1;
static bool should_exit = false;

void signal_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        syslog(LOG_INFO, "Caught signal, exiting");
        should_exit = true;
        
        // Close client connection if open
        if (client_fd != -1) {
            close(client_fd);
            client_fd = -1;
        }
        
        // Close server socket
        if (server_fd != -1) {
            close(server_fd);
            server_fd = -1;
        }
        
        // Remove data file
        unlink(DATA_FILE);
    }
}

int setup_signal_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        syslog(LOG_ERR, "Failed to setup SIGINT handler: %s", strerror(errno));
        return -1;
    }
    
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        syslog(LOG_ERR, "Failed to setup SIGTERM handler: %s", strerror(errno));
        return -1;
    }
    
    return 0;
}

int create_daemon(void) {
    pid_t pid = fork();
    
    if (pid < 0) {
        syslog(LOG_ERR, "Fork failed: %s", strerror(errno));
        return -1;
    }
    
    if (pid > 0) {
        // Parent process exits
        exit(0);
    }
    
    // Child continues as daemon
    if (setsid() < 0) {
        syslog(LOG_ERR, "Setsid failed: %s", strerror(errno));
        return -1;
    }
    
    // Change working directory to root
    if (chdir("/") < 0) {
        syslog(LOG_ERR, "Chdir failed: %s", strerror(errno));
        return -1;
    }
    
    // Close standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    
    // Redirect to /dev/null
    open("/dev/null", O_RDWR);
    dup(0);
    dup(0);
    
    return 0;
}

int send_file_contents(int sockfd) {
    FILE *fp = fopen(DATA_FILE, "r");
    if (fp == NULL) {
        if (errno == ENOENT) {
            // File doesn't exist yet, nothing to send
            return 0;
        }
        syslog(LOG_ERR, "Failed to open %s for reading: %s", DATA_FILE, strerror(errno));
        return -1;
    }
    
    char buffer[BUFFER_SIZE];
    size_t bytes_read;
    
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        ssize_t bytes_sent = send(sockfd, buffer, bytes_read, 0);
        if (bytes_sent < 0) {
            syslog(LOG_ERR, "Failed to send data: %s", strerror(errno));
            fclose(fp);
            return -1;
        }
    }
    
    fclose(fp);
    return 0;
}

int handle_client(int sockfd, struct sockaddr_in *client_addr) {
    char *ip_str = inet_ntoa(client_addr->sin_addr);
    syslog(LOG_INFO, "Accepted connection from %s", ip_str);
    
    char buffer[BUFFER_SIZE];
    char *line_buffer = NULL;
    size_t line_size = 0;
    size_t line_length = 0;
    
    while (!should_exit) {
        ssize_t bytes_received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        
        if (bytes_received < 0) {
            syslog(LOG_ERR, "Failed to receive data: %s", strerror(errno));
            free(line_buffer);
            return -1;
        }
        
        if (bytes_received == 0) {
            // Connection closed by client
            break;
        }
        
        buffer[bytes_received] = '\0';
        
        // Append to line buffer
        for (ssize_t i = 0; i < bytes_received; i++) {
            if (line_length >= line_size) {
                line_size += BUFFER_SIZE;
                char *new_buffer = realloc(line_buffer, line_size);
                if (new_buffer == NULL) {
                    syslog(LOG_ERR, "Failed to allocate memory: %s", strerror(errno));
                    free(line_buffer);
                    return -1;
                }
                line_buffer = new_buffer;
            }
            
            line_buffer[line_length++] = buffer[i];
            
            // Check for newline
            if (buffer[i] == '\n') {
                // Write to file
                FILE *fp = fopen(DATA_FILE, "a");
                if (fp == NULL) {
                    syslog(LOG_ERR, "Failed to open %s for appending: %s", 
                           DATA_FILE, strerror(errno));
                    free(line_buffer);
                    return -1;
                }
                
                if (fwrite(line_buffer, 1, line_length, fp) != line_length) {
                    syslog(LOG_ERR, "Failed to write to %s: %s", 
                           DATA_FILE, strerror(errno));
                    fclose(fp);
                    free(line_buffer);
                    return -1;
                }
                
                fclose(fp);
                
                // Send back the full file contents
                if (send_file_contents(sockfd) < 0) {
                    free(line_buffer);
                    return -1;
                }
                
                // Reset line buffer
                line_length = 0;
            }
        }
    }
    
    free(line_buffer);
    syslog(LOG_INFO, "Closed connection from %s", ip_str);
    
    return 0;
}

int main(int argc, char *argv[]) {
    bool daemon_mode = false;
    
    // Open syslog
    openlog("aesdsocket", LOG_PID, LOG_USER);
    
    // Parse command line arguments
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = true;
    }
    
    // Setup signal handlers
    if (setup_signal_handlers() < 0) {
        closelog();
        return -1;
    }
    
    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        syslog(LOG_ERR, "Failed to create socket: %s", strerror(errno));
        closelog();
        return -1;
    }
    
    // Set socket options to reuse address
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        syslog(LOG_ERR, "Failed to set socket options: %s", strerror(errno));
        close(server_fd);
        closelog();
        return -1;
    }
    
    // Bind socket to port 9000
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        syslog(LOG_ERR, "Failed to bind socket: %s", strerror(errno));
        close(server_fd);
        closelog();
        return -1;
    }
    
    // Create daemon after successful bind
    if (daemon_mode) {
        if (create_daemon() < 0) {
            close(server_fd);
            closelog();
            return -1;
        }
    }
    
    // Listen for connections
    if (listen(server_fd, 10) < 0) {
        syslog(LOG_ERR, "Failed to listen on socket: %s", strerror(errno));
        close(server_fd);
        unlink(DATA_FILE);
        closelog();
        return -1;
    }
    
    // Accept connections in a loop
    while (!should_exit) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        
        if (client_fd < 0) {
            if (should_exit) {
                break;
            }
            syslog(LOG_ERR, "Failed to accept connection: %s", strerror(errno));
            continue;
        }
        
        handle_client(client_fd, &client_addr);
        
        close(client_fd);
        client_fd = -1;
    }
    
    // Cleanup
    if (server_fd != -1) {
        close(server_fd);
    }
    
    unlink(DATA_FILE);
    closelog();
    
    return 0;
}
