#include "record-util-queue.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

typedef struct node{
    void *data;
    struct node *next;
}record_queue_node_t;

struct record_queue {
    int length;
    struct node *header;
    struct node *tail;
    pthread_mutex_t *mutex;
};

int record_queue_init(RECORD_PARAM_OUT record_queue_t **queue)
{
    record_queue_t *tmpQue = NULL;
    tmpQue = (record_queue_t *)malloc(sizeof(record_queue_t));
    if (NULL == tmpQue) {
        fprintf(stderr, "Error! Can not alloc memory for record_queue_t!\n");
        return RECORD_QUEUE_E_MEM_ALLOC;
    }

    tmpQue->header = NULL;
    tmpQue->tail = NULL;
    tmpQue->length = 0;
    tmpQue->mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    if (NULL == tmpQue->mutex) {
        fprintf(stderr, "Error! Can not alloc memory for pthread_mutex_t!\n");
        free(tmpQue);
        return RECORD_QUEUE_E_MEM_ALLOC;
    }

    if (0 != pthread_mutex_init(tmpQue->mutex, NULL)) {
        fprintf(stderr, "Error! Can not init mutex [%s]\n", strerror(errno));
        free(tmpQue->mutex);
        free(tmpQue);
        return RECORD_QUEUE_E_MUTEX_INIT;
    }
    *queue = tmpQue;
    return RECORD_QUEUE_OK;
}

int record_queue_push(RECORD_PARAM_IN record_queue_t *queue, RECORD_PARAM_IN void *data)
{
    record_queue_node_t *tmpNode = NULL;
    tmpNode = (record_queue_node_t *)malloc(sizeof(record_queue_node_t));
    if (NULL == tmpNode) {
        fprintf(stderr, "Error! Can not alloc memory for record_queue_node_t!\n");
        return RECORD_QUEUE_E_MEM_ALLOC;
    }

    tmpNode->next = NULL;
    tmpNode->data = data;

    pthread_mutex_lock(queue->mutex);
    if (NULL == queue->header) {
        queue->header = tmpNode;
        queue->tail = tmpNode;
    } else {
        queue->tail->next = tmpNode;
        queue->tail = queue->tail->next;
    }
    queue->length += 1;
    pthread_mutex_unlock(queue->mutex);

    return RECORD_QUEUE_OK;
}

void *record_queue_pop(RECORD_PARAM_IN record_queue_t *queue)
{
    record_queue_node_t *node = NULL;
    void *data = NULL;

    if (NULL == queue) {
        fprintf(stderr, "Error! Queue is not exit!\n");
        return NULL;
    }

    pthread_mutex_lock(queue->mutex);
    if (NULL != queue->header) {
        node = queue->header;
        queue->header = queue->header->next;
        node->next = NULL;
        queue->length -= 1;
    }
    //队列为空
    if (NULL == queue->header) {
        queue->tail = NULL;
    }
    pthread_mutex_unlock(queue->mutex);
    if (NULL != node) {
        data = node->data;
        node->data = NULL;
        node->next = NULL;
        free(node);
    }
    return data;
}

int is_empty(record_queue_t *queue){
    return queue->length;
}

void record_queue_destory(RECORD_PARAM_IN record_queue_t *queue)
{
    record_queue_node_t *node = NULL;

    if (NULL == queue)
        return ;

    pthread_mutex_lock(queue->mutex);
    while (NULL != (node = queue->header)) {
        queue->header = queue->header->next;
        free(node->data);
        node->data = NULL;
        node->next = NULL;
        free(node);
    }
    pthread_mutex_unlock(queue->mutex);
    pthread_mutex_destroy(queue->mutex);
    queue->header = NULL;
    queue->tail = NULL;
    queue->mutex = NULL;
    free(queue);
}