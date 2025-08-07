#include "mqueue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h> 
#include <unistd.h>

void get_time(char *buffer) {
    time_t now;
    struct tm *timeinfo;

    time(&now);
    timeinfo = localtime(&now);
    strftime(buffer, 64, "[%Y-%m-%d %H:%M:%S]", timeinfo);
}

node_t *head = NULL;
node_t *tail = NULL;

void enqueue(struct bufn *info) {
    node_t *newnode = malloc(sizeof(node_t));
    newnode->info = malloc(sizeof(buf_info));
    //newnode->client_socket = client_socket;
    strcpy(newnode->info->file_name, info->file_name);
    strcpy(newnode->info->src_host, info->src_host);
    strcpy(newnode->info->src_port, info->src_port);
    strcpy(newnode->info->tar_host, info->tar_host);
    strcpy(newnode->info->tar_port, info->tar_port);
    strcpy(newnode->info->tar_file_name, info->tar_file_name);
    newnode->next = NULL;
    if (tail == NULL) {
        head = newnode;
    } else {
        tail->next = newnode;
    }
    tail = newnode;
}

struct bufn *dequeue() {
    if (head == NULL) {
        return NULL;
    } else {
        //int *result = head->info;
        struct bufn *result = malloc(sizeof(buf_info));
        strcpy(result->file_name, head->info->file_name);
        strcpy(result->src_host, head->info->src_host);
        strcpy(result->src_port, head->info->src_port);
        strcpy(result->tar_host, head->info->tar_host);
        strcpy(result->tar_port, head->info->tar_port);
        strcpy(result->tar_file_name, head->info->tar_file_name);

        node_t *temp = head;
        head = head->next;
        if (head == NULL) { tail = NULL; }
        free(temp->info);
        free(temp);
        return result;
    }
}

int exists(const char *file_name, const char *tar_file_name) {
    node_t *current = head;
    node_t *previous = NULL;

    //printf("not null\n");
    while (current != NULL) {
        if (strcmp(current->info->file_name, file_name) == 0 && strcmp(current->info->tar_file_name, tar_file_name) == 0) {
            return 1;
        }
        previous = current;
        current = current->next;
    }
    return 0;
}

// int exists_cancel(const char *file_name, const char *tar_file_name, FILE * manlog_fp, int console_socket) {
//     node_t *current = head;
//     node_t *previous = NULL;
//     char time_str[256];
//     int flag = 0;
//     if (tar_file_name == NULL) {
//         //printf("null\n");
//         char junk[256];
//         strncpy(junk, current->info->file_name, strlen(file_name));
//         while (current != NULL) {
//             printf("junk is: %s\n", junk);
//             if (strncmp(current->info->file_name, file_name, strlen(file_name)) == 0) {
//                 if (previous == NULL) {
//                     head = current->next;
//                     if (head == NULL) {
//                         tail = NULL;
//                     }
//                 } else {
//                     previous->next = current->next;
//                     if (current == tail) {
//                         tail = previous;
//                     }
//                 }
//                 get_time(time_str);
//                 printf("%s Synchronization stopped for: %s@%s:%s\n", time_str, current->info->file_name, current->info->src_host, current->info->src_port);
//                 fprintf(manlog_fp, "%s Synchronization stopped for: %s@%s:%s\n", time_str, current->info->file_name, current->info->src_host, current->info->src_port);
//                 char send_console[BUFSIZ];
//                 sprintf(send_console, "%s Synchronization stopped for: %s@%s:%s\n", time_str, current->info->file_name, current->info->src_host, current->info->src_port);
//                 write(console_socket, send_console, strlen(send_console) + 1);
//                 read(console_socket, send_console, sizeof(send_console));
//                 memset(send_console, 0, sizeof(send_console));

//                 flag = 1;
//                 free(current->info);
//                 free(current);
//             }
//             previous = current;
//             current = current->next;
//         }
//     }
//     return flag;
// }

struct bufn *dequeue_by_filename(const char *filename) {
    node_t *curr = head;
    node_t *prev = NULL;

    while (curr != NULL) {
        if (strncmp(curr->info->file_name, filename, strlen(filename)) == 0) {
            printf("found match\n");
            struct bufn *result = malloc(sizeof(buf_info));
            strcpy(result->file_name, curr->info->file_name);
            strcpy(result->src_host, curr->info->src_host);
            strcpy(result->src_port, curr->info->src_port);
            strcpy(result->tar_host, curr->info->tar_host);
            strcpy(result->tar_port, curr->info->tar_port);
            strcpy(result->tar_file_name, curr->info->tar_file_name);

            if (prev == NULL) {
                head = curr->next;
            } else {
                prev->next = curr->next;
            }

            if (curr == tail) {
                tail = prev;
            }

            free(curr->info);
            free(curr);
            return result;
        }

        prev = curr;
        curr = curr->next;
    }
    return NULL;
}

int queue_size() {
    int count = 0;
    node_t *current = head;
    while (current != NULL) {
        count++;
        current = current->next;
    }
    return count;
}
