#ifndef __QUEUE_H
#define __QUEUE_H

#include <stdbool.h>
#include <stddef.h>

struct node {
    const char *data;
    struct node *next;
};

typedef struct {
    struct node *head;
    struct node *tail;
    size_t length;
} queue_t;

queue_t new_queue();
void enqueue(queue_t *queue, const char *value);
const char *dequeue(queue_t *queue);
void delete_queue(queue_t *queue, bool should_free_elems);

#endif // __QUEUE_H