#include "queue.h"
#include <stdio.h>
#include <stdlib.h>

queue_t new_queue() {
    queue_t queue = {0};
    return queue;
}

void enqueue(queue_t *queue, const char *value) {
    struct node *node = malloc(sizeof(struct node));
    node->data = value;
    node->next = NULL;
    queue->length++;
    if (queue->head == NULL) {
        queue->head = queue->tail = node;
        return;
    }
    queue->head->next = node;
    queue->head = node;
}

const char *dequeue(queue_t *queue) {
    if (queue->length == 0) {
        printf("No element to dequeue\n");
        return NULL;
    }

    struct node *tmp_node = queue->tail;
    const char *tmp = tmp_node->data;
    queue->length--;

    if (queue->head == queue->tail) {
        queue->head = NULL;
    }
    queue->tail = queue->tail->next;

    free(tmp_node);
    return tmp;
}

void delete_queue(queue_t *queue) {
    struct node *current = queue->tail;
    struct node *tmp = current;
    while (current) {
        current = current->next;
        free(tmp);
        tmp = current;
    }
}