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
#define SERVER_BACKLOG 10

typedef struct sockaddr_in SA_IN;
typedef struct sockaddr SA;

void handle_connection(int client_socket);

int main(int argc, char *argv[]) {
    // if (argc != 2) {
    //     fprintf(stderr, "Usage: %s <port>\n", argv[0]);
    //     return 1;
    // }
    int server_port = -1;
    int opt;
    
    while ((opt = getopt(argc, argv, "p:")) != -1) {
        switch (opt) {
            case 'p':
                server_port = atoi(optarg);
                break;
            default:
                fprintf(stderr, "Usage: %s <port>\n", argv[0]);
                return 1;
        }
    }
    if (server_port == -1) {
        printf("Usage: %s <port>\n", argv[0]);
        return 1;
    }
    printf("server port: %d\n", server_port);

    int server_socket, client_socket, addr_size;
    SA_IN server_addr, client_addr;

    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket failed");
        return 1;
    }
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(server_port);

    if ((bind(server_socket, (SA*)&server_addr, sizeof(server_addr))) == -1) {
        perror("bind failed");
        return -1;
    }
    
    if ((listen(server_socket, SERVER_BACKLOG)) == -1) {
        perror("listen failed");
        return -1;
    }

    while (true) {
        printf("waiting for connections.\n");
        addr_size = sizeof(SA_IN);
        if ((client_socket = accept(server_socket, (SA*)&client_addr, (socklen_t*)&addr_size)) == -1) {
            perror("accept failed");
            return -1;
        }
        printf("connected\n");

        handle_connection(client_socket);
    }

    return 0;
}

void handle_connection(int client_socket) {

    char buf[BUFSIZE];
    size_t bytes_read;
    int msgsize = 0;
    char actualpath[PATH_MAX+1];
    
    while((bytes_read = read(client_socket, buf+msgsize, sizeof(buf)-msgsize-1))> 0) {
        msgsize += bytes_read;
        if (msgsize > BUFSIZE-1 || buf[msgsize-1] == '\n') break;
    }
    if (bytes_read == -1) {
        perror("reiceve error");
    }
    buf[msgsize-1] = 0;

    printf("REQUEST: %s\n", buf);
    fflush(stdout);

    if (strncmp(buf, "LIST", 4) == 0) {
        strcpy(buf, buf+6);
        //printf("buf: %s\n", buf);

        struct dirent *de; 
        DIR *dr = opendir(buf);

        if (dr == NULL) {
            printf("Could not open current directory\n");
            close(client_socket);
            return;
        }

        char dir_list[1024];
        strcpy(dir_list, "");
        while ((de = readdir(dr)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
            printf("%s\n", de->d_name);
            snprintf(dir_list + strlen(dir_list), sizeof(dir_list), "%s\n", de->d_name);
        }
        snprintf(dir_list + strlen(dir_list), sizeof(dir_list), ".");

        closedir(dr);

        send(client_socket, dir_list, strlen(dir_list), 0);
        //close(client_socket);
        strcpy(buf, "");
        close(client_socket);
        //printf("closing connection\n");

    } else if (strncmp(buf, "PULL", 4) == 0) {
        strcpy(buf, buf+6);
        //printf("buf: %s\n", buf);

        if (realpath(buf, actualpath) == NULL) {
            printf("error bad path: %s\n", buf);
            char resp[BUFSIZ];
            sprintf(resp, "-1 error bad path: %s\n", buf);
            write(client_socket, resp, strlen(resp));
            close(client_socket);
            return;
        }

        FILE *fp = fopen(actualpath, "r");
        if (fp == NULL) {
            printf("error opening %s\n", buf);
            char resp[BUFSIZ];
            sprintf(resp, "-1 error opening %s\n", buf);
            write(client_socket, resp, strlen(resp));
            close(client_socket);
        }

        struct stat file_stat;

        if (lstat(actualpath, &file_stat) == -1) {
            perror("lstat failed");
            return;
        }

        //printf("Size of file '%s': %ld bytes\n", actualpath, file_stat.st_size);
        char filessize[10];
        sprintf(filessize, "%ld", file_stat.st_size);
        while ((bytes_read = fread(buf, 1, BUFSIZE, fp)) > 0) {
            printf("sending %zu bytes\n", bytes_read);
            char files_size[strlen(filessize) + file_stat.st_size + 2];
            snprintf(files_size, sizeof(files_size), "%zu %s", bytes_read, buf);
            write(client_socket, files_size, strlen(files_size));
            //write(client_socket, buf, bytes_read);
        }
        fclose(fp);
        close(client_socket);
        printf("closing connection\n");

    } else if (strncmp(buf, "PUSH", 4) == 0) {
        char filepath[256];
        int chunk_size;
        int created_msg;
        //printf("full cmd: %s\n full cmd end\n", buf);

        FILE *fp = NULL;

        if (sscanf(buf, "PUSH %s %d", filepath, &chunk_size) != 2) {
            printf("Invalid PUSH format\n");
            return;
        }
        strcpy(filepath, filepath + 1);
        //printf("parsed: %s %d\n", filepath, chunk_size);
        if (chunk_size == -1) {
            created_msg = 1;
            write(client_socket, &created_msg, sizeof(int));

            if (fp != NULL) fclose(fp);
            
            // if (realpath(filepath, actualpath) == NULL) {
            //     printf("error bad path: %s\n", buf);
            //     close(client_socket);
            //     return;
            // }

            fp = fopen(filepath, "wb");
            //printf("file opened\n");
            if (fp == NULL) {
                printf("error opening %s\n", buf);
                close(client_socket);
            }
            //printf("file opened\n");

            do {
                strcpy(buf, "");
            
                char newbuf[BUFSIZE];
                int newbytes_read;
                int fl = 0;
                msgsize = 0;
                //printf("trying to read\n");
                while(1) {
                    read(client_socket, newbuf+msgsize, 1);
                    //printf("inside\n");
                    //printf("msg sze %d\n", msgsize);
                    if (newbuf[msgsize] == ' ') {
                        if (fl != 2) {
                            //printf("out\n");
                            fl++;
                        } else {
                            //printf("out\n");
                            break;
                        }
                    }
                    msgsize += 1;
                    if (msgsize > BUFSIZE-1) break;
                }
                fl = 0;
                //printf("read\n");
                if (bytes_read == -1) {
                    perror("reiceve error");
                }
                newbuf[msgsize] = 0;
            
                printf("REQUEST: %s\n", newbuf);
                if (sscanf(newbuf, "PUSH %s %d", filepath, &chunk_size) != 2) {
                    printf("Invalid PUSH format\n");
                    return;
                }
                strcpy(filepath, filepath + 1);
                //printf("parsed: %s %d\n", filepath, chunk_size);
                

                strcpy(buf, "");
                read(client_socket, buf, chunk_size);
                buf[chunk_size] = 0;
                //printf("buf: %send\n", buf);
                fwrite(buf, 1, strlen(buf), fp);
                created_msg = 2;
                write(client_socket, &created_msg, sizeof(int));
                strcpy(newbuf, "");
            } while (chunk_size > 0);
            fclose(fp);
            
            
        } /*else if (chunk_size == 0) {
            printf("closing file\n");
            if (fp != NULL) {
                fclose(fp);
                fp = NULL;
            }
        } else if (chunk_size > 0) {
            created_msg = 2;
            write(client_socket, &created_msg, sizeof(int));
            if (!fp) {
                printf("File not open for writing\n");
                return;
            }
            char *data = malloc(chunk_size);
            if (!data) return;

            ssize_t total_read = 0;
            while (total_read < chunk_size) {
                ssize_t n = read(client_socket, data + total_read, chunk_size - total_read);
                if (n <= 0) break;
                total_read += n;
            }

            fwrite(data, 1, total_read, fp);
            free(data);
            
        }*/
    }
    
}