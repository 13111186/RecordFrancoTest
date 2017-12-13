#ifndef RECORD_UTIL_QUEUE_H
#define RECORD_UTIL_QUEUE_H

#include "record-types.h"

typedef struct record_queue record_queue_t;

enum {
    RECORD_QUEUE_OK,
    RECORD_QUEUE_E_MEM_ALLOC,
    RECORD_QUEUE_E_MUTEX_INIT,
    RECORD_QUEUE_E_EMPTY
};

int record_queue_init(RECORD_PARAM_OUT record_queue_t **queue);
int record_queue_push(RECORD_PARAM_IN record_queue_t *queue, RECORD_PARAM_IN void *data);
void *record_queue_pop(RECORD_PARAM_IN record_queue_t *queue);
void record_queue_destory(RECORD_PARAM_IN record_queue_t *queue);
int is_empty(record_queue_t *queue);
#endif