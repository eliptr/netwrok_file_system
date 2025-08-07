#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <sys/inotify.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <limits.h>

typedef struct sockaddr_in SA_IN;
typedef struct sockaddr SA;

void get_time(char *buffer) {
    time_t now;
    struct tm *timeinfo;

    time(&now);
    timeinfo = localtime(&now);
    strftime(buffer, 64, "[%Y-%m-%d %H:%M:%S]", timeinfo);
}

int main(int argc, char *argv[]) {
    char *logfile = NULL;
    char *host_ip = NULL;
    int worker_limit = 5;
    int port_number = -1;
    int buffer_size = 0;

    int opt;
    while ((opt = getopt(argc, argv, "l:h:p:")) != -1) {
        switch (opt) {
            case 'l':
                logfile = optarg;
                break;
            case 'p':
                port_number = atoi(optarg);
                break;
            case 'h':
                host_ip = optarg;
                break;
            default:
                fprintf(stderr, "Usage: %s -l <console-logfile> -h <host_IP> -p <host_port>\n", argv[0]);
                return 1;
        }
    }

    if (!logfile || !host_ip ||  port_number == -1) {
        fprintf(stderr, "Usage: %s -l <console-logfile> -h <host_IP> -p <host_port>\n", argv[0]);
        return 1;
    }

    int sock;
    SA_IN server_addr;
    char buffer[BUFSIZ];

    FILE *conlog_fp = fopen(logfile, "w");
    if (conlog_fp == NULL) {
        printf("error opening manlog\n");
        return 1;
    }

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        return 0;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_number);

    if (inet_pton(AF_INET, host_ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid address or address not supported");
        close(sock);
        return 0;
    }

    if (connect(sock, (SA*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(sock);
        return 0;
    }

    printf("connected\n");
    char time_str[64];
    
    char cmd[BUFSIZ];
    printf("> ");
    while (fgets(cmd, sizeof(cmd), stdin) != NULL) {
        size_t len = strlen(cmd);
        if (len > 0 && cmd[len - 1] == '\n') {
            cmd[len - 1] = '\0';
        }
        write(sock, cmd, strlen(cmd) + 1);
        get_time(time_str);
        fprintf(conlog_fp, "%s Command %s\n", time_str, cmd);

        if (strncmp(cmd , "shutdown", 8) == 0) {
            break;
        } else if (strncmp(cmd , "add", 3) == 0) {
            char send_back[BUFSIZ];
            while (1) {
                read(sock, send_back, sizeof(send_back));
                //printf("sent back: %s\n", send_back);
                
                if (strcmp(send_back, "end") == 0) {
                    break;
                }
                printf("%s\n", send_back);
                fprintf(conlog_fp, "%s\n", send_back);
                write(sock, send_back, strlen(send_back) + 1);
            }
            
        } else if (strncmp(cmd , "cancel", 6) == 0) {
            char send_back[BUFSIZ];
            while (1) {
                read(sock, send_back, sizeof(send_back));
                //printf("sent back: %s\n", send_back);

                if (strcmp(send_back, "end") == 0) {
                    break;
                }
                printf("%s\n", send_back);
                fprintf(conlog_fp, "%s\n", send_back);
                write(sock, send_back, strlen(send_back) + 1);
            }
        }
        

        printf("> ");
    }
    char send_back[BUFSIZ];
    read(sock, send_back, sizeof(send_back));
    printf("%s\n", send_back);
    fprintf(conlog_fp, "%s\n", send_back);
    write(sock, send_back, strlen(send_back) + 1);

    read(sock, send_back, sizeof(send_back));
    printf("%s\n", send_back);
    fprintf(conlog_fp, "%s\n", send_back);
    write(sock, send_back, strlen(send_back) + 1);

    read(sock, send_back, sizeof(send_back));
    printf("%s\n", send_back);
    fprintf(conlog_fp, "%s\n", send_back);
    write(sock, send_back, strlen(send_back) + 1);
    close(sock);
    fclose(conlog_fp);
    return 0;
}