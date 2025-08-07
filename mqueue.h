#include <stdio.h>
#ifndef MQUEUE_H_
#define QUEUE_H_

struct bufn {
    char file_name[128];
    char src_host[128];
    char src_port[128];
    char tar_host[128];
    char tar_port[128];
    char tar_file_name[128];
};

typedef struct bufn buf_info;

struct node {
    struct node* next;
    struct bufn *info;
};

typedef struct node node_t;

void get_time(char *buffer);
void enqueue(struct bufn *info);
struct bufn *dequeue();
int exists(const char *file_name, const char *tar_file_name);
struct bufn *dequeue_by_filename(const char *filename);
int queue_size();

#endif