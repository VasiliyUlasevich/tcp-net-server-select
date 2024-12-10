//
// Created by trac on 11/19/24.
//
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>

#include "tcp_server.h"

void sig_handler(int sig_no) {
    if (sig_no == SIGINT) {
        fprintf(stderr, "SIGNAL: Received SIGINT\n");
        http_abort_signal = 1;
    } else {
        // another signal (SIGUSR1)
        // for test purposes
    }
}

void set_signal_handler() {
    struct sigaction sa;

    memset(&sa, 0 , sizeof(struct sigaction));
    sa.sa_handler = SIG_IGN;
    for (int i = 1; i < MAX_SIGNAL_NUM + 1; ++i) {
        if (i != SIGINT && i != SIGUSR1 && i != SIGKILL && i != SIGSTOP && i != 32 && i != 33) {
            if (sigaction(i, &sa, NULL) == -1) {
                fprintf(stderr, "ERROR: Cannot assign signal(%d) handler\n", i);
                perror("sigaction.");
            }
        }
    }
    sa.sa_handler = &sig_handler;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        fprintf(stderr, "ERROR: Cannot assign SIGINT handler\n");
        perror("sigaction.");
    }
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        fprintf(stderr, "ERROR: Cannot assign SIGUSR1 handler\n");
        perror("sigaction.");
    }
}

int init_listen_server(char *address, uint16_t port) {
    int s;
    struct sockaddr_in socket_address;
    int true_value = 1;

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == -1) {
        fprintf(stderr,"ERROR: socket()\n");
        return 0;
    }

    if (setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&true_value,sizeof(true_value)) == -1)
    {
        fprintf(stderr,"ERROR: setsockopt()\n");
        return 0;
    }

    // set as nonblocking
    if (ioctl(s, FIONBIO, (char *)&true_value) == -1) {
        fprintf(stderr,"ERROR: ioctl()\n");
        return 0;
    }

    memset((void *)&socket_address, 0, sizeof(socket_address));
    socket_address.sin_addr.s_addr = inet_addr(address);

    if (!inet_aton(address, &socket_address.sin_addr)) {
        fprintf(stderr,"ERROR: inet_addr()\n");
        errno = EFAULT;
        return 0;
    }

    socket_address.sin_port = htons(port);
    socket_address.sin_family = AF_INET;

    if (bind(s, (struct sockaddr *)&socket_address, sizeof(socket_address)) == -1) {
        fprintf(stderr,"ERROR: bind()\n");
        return 0;
    }

    if (listen(s,SOMAXCONN) == -1) {
        fprintf(stderr,"ERROR: listen()\n");
        return 0;
    }

    // return server socket
    return s;
}

void start_server_loop(int server_socket, connection_handler_p handler) {
    int client_socket;
    fd_set rsds, sds;
    int max_sd;
    int rc;
    struct timeval timeout;
    pthread_t thr;
    int i;

    timeout.tv_sec = 0;
    timeout.tv_usec = 1000;

    FD_ZERO(&sds);
    FD_SET(server_socket, &sds);
    max_sd = server_socket;

    while(!http_abort_signal) {
        memcpy(&rsds,&sds,sizeof(sds));
        rc = select(max_sd + 1, &rsds, NULL, NULL, &timeout);
        if (rc < 0) {
            // error
            perror("select()");
            fprintf(stderr,"Number of sockets in select: %d\n", max_sd);
            break;
        } else if (rc == 0) {
            // no events , timed out
            usleep(0);
            continue;
        } else {
            // "read" event
            for (i = 0; i <= max_sd; ++i) {
                if (FD_ISSET(i, &rsds)) {
                    if (i == server_socket) {
                        do {
                            client_socket = accept(server_socket, NULL, NULL);
                            if (client_socket) {
                                FD_SET(client_socket, &sds);
                                if (client_socket > max_sd) {
                                    max_sd = client_socket;
                                }
                            } else {
                                if (errno != EWOULDBLOCK) {
                                    perror("accept()");
                                    http_abort_signal = 1;
                                }
                            }
                        } while (client_socket != -1);
                    } else {
                        // Do useful work
                        handler(&i);
                        // Clean up
                        FD_CLR(i, &sds);
                        if (i == max_sd) {
                            while (FD_ISSET(max_sd, &sds) == 0)
                                max_sd -= 1;
                        }
                    }
                }
            }
        }
    }

    shutdown(server_socket,SHUT_RDWR);
    close(server_socket);
}
