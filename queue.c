#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "queue.h"

struct message *head = NULL;
struct message *tail = NULL;
uint32_t lastseqnum = 0;

int queue_add(char *message, int length) {
    if (head == NULL) {
        head = calloc(1, sizeof(struct message));
        head->buffer = calloc(length, sizeof(char));
        memcpy(head->buffer, message, length);
        head->seqnum = lastseqnum;
        lastseqnum++;
        head->length = length;
        head->ACKed = false;
        tail = head;
    } 

    else {
        tail->next = calloc(1, sizeof(struct message));
        tail = tail->next;
        tail->buffer = calloc(length, sizeof(char));
        memcpy(tail->buffer, message, length);
        tail->seqnum = lastseqnum;
        lastseqnum++;
        tail->length = length;
        tail->ACKed = false;
    }

    return lastseqnum-1;
}

// Returns the oldest message in the queue
struct message *queue_pop() {
    if (head == NULL)
        return NULL;

    struct message * msg = malloc(sizeof(struct message));
    memcpy(msg, head, sizeof(struct message));
    free(head);
    head = msg->next;
    return msg;
}

struct message *queue_peek() {
    return head;
}
