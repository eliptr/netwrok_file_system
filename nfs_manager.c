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
#include "mqueue.h"

#define BUFSIZE 4096
#define SERVER_BACKLOG 100
#define SERVERPORT 8989
#define THREAD_POOL_SIZE 20
#define CHUNK_SIZE 4096

// struct bufn {
//     char file_name[128];
//     char src_host[128];
//     char src_port[128];
//     char tar_host[128];
//     char tar_port[128];
// };

//pthread_t thread_pool[THREAD_POOL_SIZE];
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condition_var = PTHREAD_COND_INITIALIZER;
int shutdown_requested = 0;
FILE *manlog_fp;

typedef struct sockaddr_in SA_IN;
typedef struct sockaddr SA;

void *handle_connection(buf_info *pclient_info);

void *thread_function(void *arg);

// void get_time(char *buffer) {
//     time_t now;
//     struct tm *timeinfo;

//     time(&now);
//     timeinfo = localtime(&now);
//     strftime(buffer, 64, "[%Y-%m-%d %H:%M:%S]", timeinfo);
// }

int open_socket(const char *ip, int port) {
    int sock;
    SA_IN server_addr;
    char buffer[BUFSIZE];

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        return 0;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid address or address not supported");
        close(sock);
        return 0;
    }

    if (connect(sock, (SA*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(sock);
        return 0;
    }
    return sock;
}

int main(int argc, char *argv[]) {
    char *logfile = NULL;
    char *config_file = NULL;
    int worker_limit = 5;
    int port_number = -1;
    int buffer_size = 0;

    int opt;
    while ((opt = getopt(argc, argv, "l:c:n:p:b:")) != -1) {
        switch (opt) {
            case 'l':
                logfile = optarg;
                break;
            case 'c':
                config_file = optarg;
                break;
            case 'n':
                worker_limit = atoi(optarg);
                break;
            case 'p':
                port_number = atoi(optarg);
                break;
            case 'b':
                buffer_size = atoi(optarg);
                break;
            default:
                fprintf(stderr, "Usage: %s -l <logfile> -c <config_file> -n <worker_limit> -p <port_number> -b <bufferSize>\n", argv[0]);
                return 1;
        }
    }

    if (!logfile || !config_file || worker_limit == 0 || port_number == -1 || buffer_size == 0) {
        fprintf(stderr, "Usage: %s -l <logfile> -c <config_file> -n <worker_limit> -p <port_number> -b <bufferSize>\n", argv[0]);
        return 1;
    }

    char time_str[32];

    manlog_fp = fopen(logfile, "w");
    if (manlog_fp == NULL) {
        printf("error opening manlog\n");
        return 1;
    }

    //thread pool
    pthread_t thread_pool[worker_limit];
    for (int i = 0;i<worker_limit; i++) {
        pthread_create(&thread_pool[i], NULL, thread_function, NULL);
    }

    //config file read
    FILE *config_fp = fopen(config_file, "r");
    if (config_fp == NULL) {
        printf("error opening config_file\n");
        return 1;
    }
    char temp;
    char line[1024];

    while (fgets(line, sizeof(line), config_fp)) {
        line[strcspn(line, "\n")] = '\0'; // Remove newline

        char *source = strtok(line, " ");
        char *target = strtok(NULL, " ");

        if (!source || !target) {
            fprintf(stderr, "Invalid line — skipping\n");
            continue;
        }

        char source_copy[256];
        strncpy(source_copy, source, sizeof(source_copy));
        source_copy[sizeof(source_copy) - 1] = '\0';

        char *at_s = strchr(source_copy, '@');
        char *colon_s = strrchr(source_copy, ':');

        if (!at_s || !colon_s || colon_s < at_s) {
            fprintf(stderr, "Invalid source format\n");
            continue;
        }

        *at_s = '\0';
        *colon_s = '\0';

        char *src_path = source_copy;
        char *src_ip = at_s + 1;
        char *src_port = colon_s + 1;

        char target_copy[256];
        strncpy(target_copy, target, sizeof(target_copy));
        target_copy[sizeof(target_copy) - 1] = '\0';

        char *at_t = strchr(target_copy, '@');
        char *colon_t = strrchr(target_copy, ':');

        if (!at_t || !colon_t || colon_t < at_t) {
            fprintf(stderr, "Invalid target format\n");
            continue;
        }

        *at_t = '\0';
        *colon_t = '\0';

        char *tgt_path = target_copy;
        char *tgt_ip = at_t + 1;
        char *tgt_port = colon_t + 1;

        //printf("  Source: %-10s | IP: %-15s | Port: %s\n", src_path, src_ip, src_port);
        //printf("  Target: %-10s | IP: %-15s | Port: %s\n\n", tgt_path, tgt_ip, tgt_port);

        int sock;
        SA_IN server_addr;
        char buffer[BUFSIZE];

        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            perror("Socket creation error");
            return 1;
        }

        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(atoi(src_port));

        if (inet_pton(AF_INET, src_ip, &server_addr.sin_addr) <= 0) {
            perror("Invalid address or address not supported");
            close(sock);
            return 1;
        }

        if (connect(sock, (SA*)&server_addr, sizeof(server_addr)) < 0) {
            perror("Connection failed");
            close(sock);
            return 1;
        }
        int passize = 6 + strlen(src_path);
        char readthis[passize];
        snprintf(readthis, passize, "LIST %s", src_path);
        //printf("LIST %s\n", src_path);
        send(sock, readthis, strlen(readthis), 0);
        send(sock, "\n", 1, 0);  

        ssize_t bytes_received;
        //printf("Receiving data from server:\n");
        while ((bytes_received = read(sock, buffer, BUFSIZE - 1)) > 0) {
            buffer[bytes_received] = '\0';
            //printf("%s", buffer);
        }

        if (bytes_received < 0) {
            perror("Read error");
        }

        //printf("\nConnection closed.\n");
        close(sock);

        char *file_name_buf = strtok(buffer, "\n");

        while (file_name_buf != NULL) {
            buf_info *pclient = malloc(sizeof(buf_info));

            if (strcmp(file_name_buf, ".") == 0) {
                break;
            }
            char final_path[256];
            char final_tar[256];
            snprintf(final_path, 256, "%s/%s", src_path, file_name_buf);
            snprintf(final_tar, 256, "%s/%s", tgt_path, file_name_buf);
            strcpy(pclient->file_name, final_path);
            strcpy(pclient->src_host, src_ip);
            strcpy(pclient->src_port, src_port);
            strcpy(pclient->tar_host, tgt_ip);
            strcpy(pclient->tar_port, tgt_port);
            strcpy(pclient->tar_file_name, final_tar);

            pthread_mutex_lock(&mutex);
            int cursize = queue_size();
            if (cursize <= buffer_size) {
                enqueue(pclient);
                free(pclient);
            }
            
            //printf("transfered\n");
            pthread_cond_signal(&condition_var);
            pthread_mutex_unlock(&mutex);
            if (cursize > buffer_size) {
                printf("queue is full\n");
                continue;
            }
            get_time(time_str);
            printf("%s Added file: %s/%s@%s:%s -> %s/%s@%s:%s\n", time_str, src_path, file_name_buf, src_ip, src_port, tgt_path, file_name_buf, tgt_ip, tgt_port);
            fprintf(manlog_fp, "%s Added file: %s/%s@%s:%s -> %s/%s@%s:%s\n", time_str, src_path, file_name_buf, src_ip, src_port, tgt_path, file_name_buf, tgt_ip, tgt_port);

            file_name_buf = strtok(NULL, "\n");
        }
        
    }

    fclose(config_fp);
    //fclose(manlog_fp);

    //console connect

    //console server connection
    int server_socket, console_socket, addr_size;
    SA_IN server_addr, client_addr;

    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket failed");
        return 1;
    }
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port_number);

    if ((bind(server_socket, (SA*)&server_addr, sizeof(server_addr))) == -1) {
        perror("bind failed");
        return -1;
    }
    
    if ((listen(server_socket, 1)) == -1) {
        perror("listen failed");
        return -1;
    }

    //printf("ready for console.\n");
    addr_size = sizeof(SA_IN);
    if ((console_socket = accept(server_socket, (SA*)&client_addr, (socklen_t*)&addr_size)) == -1) {
        perror("accept failed");
        return -1;
    }
    //printf("connected to console\n");
    while (1) {
        char console_cmd[512];
        int msgsize = 0;
        while (1) {
            if (read(console_socket, console_cmd + msgsize, 1) < 0) {
                fprintf(stderr, "Error reading from console socket: %s\n", strerror(errno));
                close(console_socket);
                return 1;
            }
            if (console_cmd[msgsize] == ' ' || strcmp(console_cmd, "shutdown") == 0) {
                console_cmd[msgsize] = 0;
                break;
            }
            msgsize++;

            if (msgsize > BUFSIZ-1) break;
        }
        //printf("full console cmd: %s\n", console_cmd);
        if (strcmp(console_cmd, "add") == 0) {
            //printf("add new folder\n");
            char con_src[256];
            char con_tar[256];
            msgsize = 0;
            while (1) {
                if (read(console_socket, con_src + msgsize, 1) < 0) {
                    fprintf(stderr, "Error reading from console socket: %s\n", strerror(errno));
                    close(console_socket);
                    return 1;
                }
                if (con_src[msgsize] == ' ') {
                    con_src[msgsize] = 0;
                    break;
                }
                msgsize++;
        
                if (msgsize > BUFSIZ-1) break;
            }


            char *at_s = strchr(con_src, '@');
            char *colon_s = strrchr(con_src, ':');

            if (!at_s || !colon_s || colon_s < at_s) {
                fprintf(stderr, "Invalid source format\n");
                continue;
            }

            *at_s = '\0';
            *colon_s = '\0';

            char *src_path = con_src;
            char *src_ip = at_s + 1;
            char *src_port = colon_s + 1;

            //printf("src path: %s, str ip: %s, src port: %s\n", src_path, src_ip, src_port);

            msgsize = 0;
            
            while (1) {
                if (read(console_socket, con_tar + msgsize, 1) < 0) {
                    fprintf(stderr, "Error reading from console socket: %s\n", strerror(errno));
                    close(console_socket);
                    return 1;
                }
                if (con_tar[msgsize] == 0) {
                    con_tar[msgsize] = 0;
                    break;
                }
                msgsize++;
        
                if (msgsize > BUFSIZ-1) break;
            }
            con_tar[msgsize] = 0;

            at_s = strchr(con_tar, '@');
            colon_s = strrchr(con_tar, ':');

            if (!at_s || !colon_s || colon_s < at_s) {
                fprintf(stderr, "Invalid source format\n");
                continue;
            }

            *at_s = '\0';
            *colon_s = '\0';

            char *tar_path = con_tar;
            char *tar_ip = at_s + 1;
            char *tar_port = colon_s + 1;

            //printf("tar path: %s, tar ip: %s, tar port: %s\n", tar_path, tar_ip, tar_port);

            int sock;
            SA_IN server_addr;
            char buffer[BUFSIZE];

            if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
                perror("Socket creation error");
                return 1;
            }

            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(atoi(src_port));

            if (inet_pton(AF_INET, src_ip, &server_addr.sin_addr) <= 0) {
                perror("Invalid address or address not supported");
                close(sock);
                return 1;
            }

            if (connect(sock, (SA*)&server_addr, sizeof(server_addr)) < 0) {
                perror("Connection failed");
                close(sock);
                return 1;
            }
            int passize = 6 + strlen(src_path);
            char readthis[passize];
            snprintf(readthis, passize, "LIST %s", src_path);
            //printf("LIST %s\n", src_path);
            send(sock, readthis, strlen(readthis), 0);
            send(sock, "\n", 1, 0);  

            ssize_t bytes_received;
            //printf("Receiving data from server:\n");
            while ((bytes_received = read(sock, buffer, BUFSIZE - 1)) > 0) {
                buffer[bytes_received] = '\0';
                //printf("%s", buffer);
            }

            if (bytes_received < 0) {
                perror("Read error");
            }

            //printf("\nConnection closed.\n");
            close(sock);

            char *file_name_buf = strtok(buffer, "\n");

            while (file_name_buf != NULL) {
                buf_info *pclient = malloc(sizeof(buf_info));

                if (strcmp(file_name_buf, ".") == 0) {
                    break;
                }
                char final_path[256];
                char final_tar[256];
                snprintf(final_path, 256, "%s/%s", src_path, file_name_buf);
                snprintf(final_tar, 256, "%s/%s", tar_path, file_name_buf);
                strcpy(pclient->file_name, final_path);
                strcpy(pclient->src_host, src_ip);
                strcpy(pclient->src_port, src_port);
                strcpy(pclient->tar_host, tar_ip);
                strcpy(pclient->tar_port, tar_port);
                strcpy(pclient->tar_file_name, final_tar);

                if (exists(final_path, final_tar)) {
                    get_time(time_str);
                    printf("%s Already in queue: %s@%s:%s\n", time_str, final_path, src_ip, src_port);
                    fprintf(manlog_fp, "%s Already in queue: %s@%s:%s\n", time_str, final_path, src_ip, src_port);
                    char send_console[BUFSIZE];
                    sprintf(send_console, "%s Already in queue: %s@%s:%s\n", time_str, final_path, src_ip, src_port);
                    write(console_socket, send_console, strlen(send_console) + 1);
                    read(console_socket, send_console, sizeof(send_console));
                    memset(send_console, 0, sizeof(send_console));
                    file_name_buf = strtok(NULL, "\n");
                    continue;
                }

                pthread_mutex_lock(&mutex);
                int cursize = queue_size();
                if (cursize <= buffer_size) {
                    enqueue(pclient);
                    free(pclient);
                }
                pthread_cond_signal(&condition_var);
                pthread_mutex_unlock(&mutex);

                if (cursize > buffer_size) {
                    printf("queue is full\n");
                    continue;
                }

                get_time(time_str);
                printf("%s Added file: %s/%s@%s:%s -> %s/%s@%s:%s\n", time_str, src_path, file_name_buf, src_ip, src_port, tar_path, file_name_buf, tar_ip, tar_port);
                fprintf(manlog_fp, "%s Added file: %s/%s@%s:%s -> %s/%s@%s:%s\n", time_str, src_path, file_name_buf, src_ip, src_port, tar_path, file_name_buf, tar_ip, tar_port);
                char send_console[BUFSIZE];
                sprintf(send_console, "%s Added file: %s/%s@%s:%s -> %s/%s@%s:%s\n", time_str, src_path, file_name_buf, src_ip, src_port, tar_path, file_name_buf, tar_ip, tar_port);
                write(console_socket, send_console, strlen(send_console) + 1);
                read(console_socket, send_console, sizeof(send_console));
                memset(send_console, 0, sizeof(send_console));
                file_name_buf = strtok(NULL, "\n");
            }
            char *endmsg = "end";
            write(console_socket, endmsg, strlen(endmsg) + 1);
           
        } else if (strcmp(console_cmd, "shutdown") == 0) {
            //printf("shut it \n");
            break;
        } else if (strcmp(console_cmd, "cancel") == 0) {
            char con_src[256];
            msgsize = 0;
            while (1) {
                if (read(console_socket, con_src + msgsize, 1) < 0) {
                    fprintf(stderr, "Error reading from console socket: %s\n", strerror(errno));
                    close(console_socket);
                    return 1;
                }
                if (con_src[msgsize] == 0) {
                    con_src[msgsize] = 0;
                    break;
                }
                msgsize++;
        
                if (msgsize > BUFSIZ-1) break;
            }
            
            //printf("folder name: %s\n", con_src);
            buf_info *check;
            check = dequeue_by_filename(con_src);
            //printf("dequed\n");
            if (check != NULL) {
                while (check != NULL) {
                    get_time(time_str);
                    printf("%s Synchronization stopped for: %s@%s:%s\n", time_str, check->file_name, check->src_host, check->src_port);
                    fprintf(manlog_fp, "%s Synchronization stopped for: %s@%s:%s\n", time_str, check->file_name, check->src_host, check->src_port);
                    char send_console[BUFSIZ];
                    sprintf(send_console, "%s Synchronization stopped for: %s@%s:%s\n", time_str, check->file_name, check->src_host, check->src_port);
                    write(console_socket, send_console, strlen(send_console) + 1);
                    read(console_socket, send_console, sizeof(send_console));
                    memset(send_console, 0, sizeof(send_console));

                    check = dequeue_by_filename(con_src);
                }
                free(check);
                char *endmsg = "end";
                write(console_socket, endmsg, strlen(endmsg) + 1);
            } else {
                get_time(time_str);
                printf("%s Directory not being synchronized: %s\n", time_str, con_src);
                char send_console[BUFSIZ];
                sprintf(send_console, "%s Directory not being synchronized: %s\n", time_str, con_src);
                write(console_socket, send_console, strlen(send_console) + 1);
                read(console_socket, send_console, sizeof(send_console));
                memset(send_console, 0, sizeof(send_console));
                char *endmsg = "end";
                write(console_socket, endmsg, strlen(endmsg) + 1);
            }
            
        }
    }

    get_time(time_str);
    printf("%s Shutting down manager...\n%s Waiting for all active workers to finish.\n", time_str, time_str);
    char send_console[BUFSIZ];
    sprintf(send_console, "%s Shutting down manager...\n%s Waiting for all active workers to finish.\n", time_str, time_str);
    write(console_socket, send_console, strlen(send_console) + 1);
    read(console_socket, send_console, sizeof(send_console));
    memset(send_console, 0, sizeof(send_console));
    
    shutdown_requested = 1;
    pthread_mutex_lock(&mutex);
    pthread_cond_broadcast(&condition_var);  
    pthread_mutex_unlock(&mutex);

    get_time(time_str);
    printf("%s Processing remaining queued tasks.\n", time_str);
    sprintf(send_console, "%s Processing remaining queued tasks.\n", time_str);
    write(console_socket, send_console, strlen(send_console) + 1);
    read(console_socket, send_console, sizeof(send_console));
    memset(send_console, 0, sizeof(send_console));

    for (int i = 0; i < worker_limit; i++) {
        pthread_join(thread_pool[i], NULL); 
    }

    get_time(time_str);
    printf("%s Manager shutdown complete.\n", time_str);
    sprintf(send_console, "%s Manager shutdown complete.\n", time_str);
    write(console_socket, send_console, strlen(send_console) + 1);
    read(console_socket, send_console, sizeof(send_console));
    memset(send_console, 0, sizeof(send_console));
    

    close(console_socket);
    close(server_socket);
    fclose(manlog_fp);
}

void *thread_function(void *arg) {
    while (true) {
        buf_info *pclient;
        //sleep(20);
        pthread_mutex_lock(&mutex);
        if ((pclient = dequeue()) == NULL) {
            //printf("null\n");
            if (shutdown_requested) {
                pthread_mutex_unlock(&mutex);
                break;
            }

            pthread_cond_wait(&condition_var, &mutex);
            //pclient = dequeue();
            if ((pclient = dequeue()) == NULL && shutdown_requested) {
                pthread_mutex_unlock(&mutex);
                break;
            }
        }
        pthread_mutex_unlock(&mutex);
        // if (shutdown_requested && pclient == NULL) {
        //     break;
        // }
        if (pclient != NULL) {
            handle_connection(pclient);
            free(pclient);
        }
    }
    return NULL;
}


void *handle_connection(buf_info *pclient_info) {
    //printf("this worked %s\n", pclient_info->file_name);
    char time_str[64];

    int src_sock = open_socket(pclient_info->src_host, atoi(pclient_info->src_port));
    if (src_sock == 0) {
        fprintf(stderr, "Error opening source socket to %s:%s: %s\n", pclient_info->src_host, pclient_info->src_port, strerror(errno));
        char send_log[BUFSIZ];
        get_time(time_str);
        sprintf(send_log, "%s [%s@%s:%s] [%s@%s:%s] [%d] [PULL] [ERROR] [File: %s - %s]\n", time_str, pclient_info->file_name, pclient_info->src_host, pclient_info->src_port, pclient_info->tar_file_name, pclient_info->tar_host, pclient_info->tar_port, getpid(), pclient_info->file_name, strerror(errno));
        fprintf(manlog_fp, "%s", send_log);
        memset(send_log, 0, sizeof(send_log));
        return NULL;
    }

    int passize = 7 + strlen(pclient_info->file_name);
    char readthis[passize];
    snprintf(readthis, passize, "PULL %s\n", pclient_info->file_name);

    if (write(src_sock, readthis, strlen(readthis)) < 0) {
        fprintf(stderr, "Error writing to source socket: %s\n", strerror(errno));
        close(src_sock);
        char send_log[BUFSIZ];
        get_time(time_str);
        sprintf(send_log, "%s [%s@%s:%s] [%s@%s:%s] [%d] [PULL] [ERROR] [File: %s - %s]\n", time_str, pclient_info->file_name, pclient_info->src_host, pclient_info->src_port, pclient_info->tar_file_name, pclient_info->tar_host, pclient_info->tar_port, getpid(), pclient_info->file_name, strerror(errno));
        fprintf(manlog_fp, "%s", send_log);
        memset(send_log, 0, sizeof(send_log));
        return NULL;
    }

    int tar_sock = open_socket(pclient_info->tar_host, atoi(pclient_info->tar_port));
    if (tar_sock == 0) {
        //printf("error opening tar socket");
        fprintf(stderr, "Error opening target socket to %s:%s: %s\n", 
            pclient_info->tar_host, pclient_info->tar_port, strerror(errno));
        close(tar_sock);
        char send_log[BUFSIZ];
        get_time(time_str);
        sprintf(send_log, "%s [%s@%s:%s] [%s@%s:%s] [%d] [PUSH] [ERROR] [File: %s - %s]\n", time_str, pclient_info->file_name, pclient_info->src_host, pclient_info->src_port, pclient_info->tar_file_name, pclient_info->tar_host, pclient_info->tar_port, getpid(), pclient_info->file_name, strerror(errno));
        fprintf(manlog_fp, "%s", send_log);
        memset(send_log, 0, sizeof(send_log));
        return NULL;
    }

    ssize_t bytes_received;
    char buffer[CHUNK_SIZE];
    int first_chunk = 1;
    int act_bytes = 0;
    int msgsize = 0;
    char pull_header[256];
    while (1) {
        read(src_sock, pull_header+msgsize, 1);
        if (pull_header[msgsize] == ' ') {
            pull_header[msgsize] = 0;
            break;
        }
        msgsize++;
    }

    //printf("pull header: %s\n", pull_header);
    act_bytes = atoi(pull_header);
    if (act_bytes == -1) {
        char ermsg[1024];
        msgsize = 0;
        while (1) {
            read(src_sock, ermsg+msgsize, 1);
            if (ermsg[msgsize] == 0) {
                break;
            }
            msgsize++;
        }
        char send_log[BUFSIZ];
        get_time(time_str);
        sprintf(send_log, "%s [%s@%s:%s] [%s@%s:%s] [%d] [PULL] [ERROR] [File: %s - %s]\n", time_str, pclient_info->file_name, pclient_info->src_host, pclient_info->src_port, pclient_info->tar_file_name, pclient_info->tar_host, pclient_info->tar_port, getpid(), pclient_info->file_name, ermsg);
        fprintf(manlog_fp, "%s", send_log);
        memset(send_log, 0, sizeof(send_log));
    }
    //printf("Receiving data from server:\n");
    strcpy(buffer, "");
    while ((bytes_received = read(src_sock, buffer, CHUNK_SIZE)) > 0) {
        buffer[bytes_received] = '\0';
        //printf("read from loop: %s\n", buffer);
        char push_header[256];
        int header_len;
        int tar_msg;
        if (first_chunk) {
            header_len = snprintf(push_header, sizeof(push_header), "PUSH %s -1 \n", pclient_info->tar_file_name);
            first_chunk = 0;
            //printf("PUSH %s -1\n", pclient_info->tar_file_name);
            
            if (write(tar_sock, push_header, header_len) < 0) {
                fprintf(stderr, "Error writing initial PUSH header: %s\n", strerror(errno));
                char send_log[BUFSIZ];
                get_time(time_str);
                sprintf(send_log, "%s [%s@%s:%s] [%s@%s:%s] [%d] [PUSH] [ERROR] [File: %s - %s]\n", time_str, pclient_info->file_name, pclient_info->src_host, pclient_info->src_port, pclient_info->tar_file_name, pclient_info->tar_host, pclient_info->tar_port, getpid(), pclient_info->file_name, strerror(errno));
                fprintf(manlog_fp, "%s", send_log);
                memset(send_log, 0, sizeof(send_log));
                break;
            }
            
            if (read(tar_sock, &tar_msg, sizeof(int)) <= 0) {
                fprintf(stderr, "Error reading initial response from target: %s\n", strerror(errno));
                char send_log[BUFSIZ];
                get_time(time_str);
                sprintf(send_log, "%s [%s@%s:%s] [%s@%s:%s] [%d] [PUSH] [ERROR] [File: %s - %s]\n", time_str, pclient_info->file_name, pclient_info->src_host, pclient_info->src_port, pclient_info->tar_file_name, pclient_info->tar_host, pclient_info->tar_port, getpid(), pclient_info->file_name, strerror(errno));
                fprintf(manlog_fp, "%s", send_log);
                memset(send_log, 0, sizeof(send_log));
                break;
            }
        }
        header_len = snprintf(push_header, sizeof(push_header), "PUSH %s %ld %s", pclient_info->tar_file_name, strlen(buffer), buffer);
        
        //printf("PUSH %s %ld %s\n", pclient_info->tar_file_name, strlen(buffer), buffer);
        
        if (write(tar_sock, push_header, strlen(push_header)) < 0) {
            fprintf(stderr, "Error sending chunk to target: %s\n", strerror(errno));
            char send_log[BUFSIZ];
            get_time(time_str);
            sprintf(send_log, "%s [%s@%s:%s] [%s@%s:%s] [%d] [PUSH] [ERROR] [File: %s - %s]\n", time_str, pclient_info->file_name, pclient_info->src_host, pclient_info->src_port, pclient_info->tar_file_name, pclient_info->tar_host, pclient_info->tar_port, getpid(), pclient_info->file_name, strerror(errno));
            fprintf(manlog_fp, "%s", send_log);
            memset(send_log, 0, sizeof(send_log));
            break;
        }
        

        if (read(tar_sock, &tar_msg, sizeof(int)) <= 0) {
            fprintf(stderr, "Error reading confirmation from target: %s\n", strerror(errno));
            char send_log[BUFSIZ];
            get_time(time_str);
            sprintf(send_log, "%s [%s@%s:%s] [%s@%s:%s] [%d] [PUSH] [ERROR] [File: %s - %s]\n", time_str, pclient_info->file_name, pclient_info->src_host, pclient_info->src_port, pclient_info->tar_file_name, pclient_info->tar_host, pclient_info->tar_port, getpid(), pclient_info->file_name, strerror(errno));
            fprintf(manlog_fp, "%s", send_log);
            memset(send_log, 0, sizeof(send_log));
            break;
        }


        memset(buffer, 0, CHUNK_SIZE);
        strcpy(push_header, "");
        strcpy(pull_header, "");
    }

    char eof_msg[256];
    snprintf(eof_msg, sizeof(eof_msg), "PUSH %s 0 \n", pclient_info->tar_file_name);

    if (write(tar_sock, eof_msg, strlen(eof_msg)) < 0) {
        fprintf(stderr, "Error sending EOF to target: %s\n", strerror(errno));
        char send_log[BUFSIZ];
        get_time(time_str);
        sprintf(send_log, "%s [%s@%s:%s] [%s@%s:%s] [%d] [PUSH] [ERROR] [File: %s - %s]\n", time_str, pclient_info->file_name, pclient_info->src_host, pclient_info->src_port, pclient_info->tar_file_name, pclient_info->tar_host, pclient_info->tar_port, getpid(), pclient_info->file_name, strerror(errno));
        fprintf(manlog_fp, "%s", send_log);
        memset(send_log, 0, sizeof(send_log));
        return NULL;
    }
    strcpy(eof_msg, "");

    char send_log[BUFSIZ];
    get_time(time_str);
    sprintf(send_log, "%s [%s@%s:%s] [%s@%s:%s] [%d] [PULL] [SUCCESS] [%d bytes pulled]\n", time_str, pclient_info->file_name, pclient_info->src_host, pclient_info->src_port, pclient_info->tar_file_name, pclient_info->tar_host, pclient_info->tar_port, getpid(), act_bytes);
    fprintf(manlog_fp, "%s", send_log);
    memset(send_log, 0, sizeof(send_log));

    get_time(time_str);
    sprintf(send_log, "%s [%s@%s:%s] [%s@%s:%s] [%d] [PUSH] [SUCCESS] [%d bytes pushed]\n", time_str, pclient_info->file_name, pclient_info->src_host, pclient_info->src_port, pclient_info->tar_file_name, pclient_info->tar_host, pclient_info->tar_port, getpid(), act_bytes);
    fprintf(manlog_fp, "%s", send_log);
    close(tar_sock);
    close(src_sock);


    return NULL;
}