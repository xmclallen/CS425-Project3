#ifndef QUEUE_H
#define QUEUE_H

#include <stdbool.h>

// Adds a message to the end of the queue, return sequence number
int queue_add(char *message, int length);

// Returns & removes the oldest message in the queue
struct message *queue_pop();

// Peek at the oldest message in the queue
struct message *queue_peek();

struct message {
    struct message *next;
    char *buffer;
    uint32_t seqnum;
    uint32_t length;
    bool ACKed;
};

#endif
