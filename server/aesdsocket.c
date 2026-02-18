#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#define PORT 9000
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024

static volatile sig_atomic_t shutdown_requested = 0;

static void signal_handler(int signo)
{
    if (signo == SIGINT || signo == SIGTERM) {
        shutdown_requested = 1;
    }
}

static void cleanup_and_exit(int server_fd)
{
    // Per spec: log on SIGINT/SIGTERM exit
    syslog(LOG_INFO, "Caught signal, exiting");

    if (server_fd != -1) {
        close(server_fd);
    }
    unlink(DATA_FILE);
    closelog();
    exit(0);
}

static int setup_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    // Restarting syscalls is okay; accept/recv may still be interrupted depending on platform
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        syslog(LOG_ERR, "sigaction SIGINT failed: %s", strerror(errno));
        return -1;
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        syslog(LOG_ERR, "sigaction SIGTERM failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

static int daemonize_after_bind(void)
{
    pid_t pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "Fork failed: %s", strerror(errno));
        return -1;
    }
    if (pid > 0) {
        // Parent exits
        exit(0);
    }

    // Child continues
    if (setsid() < 0) {
        syslog(LOG_ERR, "setsid failed: %s", strerror(errno));
        return -1;
    }

    if (chdir("/") != 0) {
        syslog(LOG_ERR, "chdir failed: %s", strerror(errno));
        return -1;
    }

    // Redirect stdio to /dev/null
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        (void)dup2(fd, STDIN_FILENO);
        (void)dup2(fd, STDOUT_FILENO);
        (void)dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO) close(fd);
    }
    return 0;
}

static int send_all(int sockfd, const char *buf, size_t len)
{
    size_t total = 0;
    while (total < len) {
        ssize_t s = send(sockfd, buf + total, len - total, 0);
        if (s < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        total += (size_t)s;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    bool daemon_mode = false;
    int server_fd = -1;
    int client_fd = -1;
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received, bytes_read;

    // Check for -d flag
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = true;
    }

    openlog("aesdsocket", LOG_PID, LOG_USER);

    if (setup_signals() != 0) {
        closelog();
        return -1;
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        syslog(LOG_ERR, "Socket creation failed: %s", strerror(errno));
        closelog();
        return -1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        syslog(LOG_ERR, "setsockopt failed: %s", strerror(errno));
        close(server_fd);
        closelog();
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        syslog(LOG_ERR, "Bind failed: %s", strerror(errno));
        close(server_fd);
        closelog();
        return -1;
    }

    // Daemonize (after ensuring bind works, per spec)
    if (daemon_mode) {
        if (daemonize_after_bind() != 0) {
            close(server_fd);
            closelog();
            return -1;
        }
    }

    if (listen(server_fd, 10) < 0) {
        syslog(LOG_ERR, "Listen failed: %s", strerror(errno));
        cleanup_and_exit(server_fd);
    }

    // Accept loop
    while (!shutdown_requested) {
        socklen_t client_len = sizeof(client_addr);
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR && shutdown_requested) {
                break;
            }
            syslog(LOG_ERR, "Accept failed: %s", strerror(errno));
            continue;
        }

        char client_ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        // Accumulate until newline(s), write each packet, then return entire file
        char *acc = NULL;
        size_t acc_len = 0;

        while (!shutdown_requested) {
            bytes_received = recv(client_fd, buffer, sizeof(buffer), 0);
            if (bytes_received < 0) {
                if (errno == EINTR && shutdown_requested) {
                    break;
                }
                syslog(LOG_ERR, "recv failed: %s", strerror(errno));
                break;
            }
            if (bytes_received == 0) {
                // Client closed connection
                break;
            }

            char *new_acc = realloc(acc, acc_len + (size_t)bytes_received);
            if (!new_acc) {
                syslog(LOG_ERR, "realloc failed");
                free(acc);
                acc = NULL;
                acc_len = 0;
                break;
            }
            acc = new_acc;
            memcpy(acc + acc_len, buffer, (size_t)bytes_received);
            acc_len += (size_t)bytes_received;

            // Process complete packets (newline-terminated)
            while (1) {
                void *nlptr = memchr(acc, '\n', acc_len);
                if (!nlptr) break;

                size_t pkt_len = ((char *)nlptr - acc) + 1;

                // Append exactly one packet to file
                int data_fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
                if (data_fd < 0) {
                    syslog(LOG_ERR, "open data file failed: %s", strerror(errno));
                    break;
                }

                ssize_t w = write(data_fd, acc, pkt_len);
                if (w < 0 || (size_t)w != pkt_len) {
                    syslog(LOG_ERR, "write failed: %s", strerror(errno));
                    close(data_fd);
                    break;
                }
                close(data_fd);

                // Send file contents back in chunks
                data_fd = open(DATA_FILE, O_RDONLY);
                if (data_fd < 0) {
                    syslog(LOG_ERR, "open data file for read failed: %s", strerror(errno));
                    break;
                }

                while ((bytes_read = read(data_fd, buffer, sizeof(buffer))) > 0) {
                    if (send_all(client_fd, buffer, (size_t)bytes_read) != 0) {
                        syslog(LOG_ERR, "send failed: %s", strerror(errno));
                        break;
                    }
                }
                if (bytes_read < 0) {
                    syslog(LOG_ERR, "read failed: %s", strerror(errno));
                }
                close(data_fd);

                // Remove consumed packet from accumulator
                size_t remaining = acc_len - pkt_len;
                if (remaining > 0) {
                    memmove(acc, acc + pkt_len, remaining);
                }
                acc_len = remaining;

                if (acc_len == 0) {
                    free(acc);
                    acc = NULL;
                } else {
                    char *shrunk = realloc(acc, acc_len);
                    if (shrunk) acc = shrunk;
                }
            }
        }

        free(acc);

        syslog(LOG_INFO, "Closed connection from %s", client_ip);
        close(client_fd);
        client_fd = -1;
    }

    cleanup_and_exit(server_fd);
    return 0;
}
