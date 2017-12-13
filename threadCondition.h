//
// Created by root on 17-12-8.
//

#ifndef RECORDFRANCO_THREADCONDITION_H
#define RECORDFRANCO_THREADCONDITION_H
#include <pthread.h>
#include <zconf.h>

#include <pthread.h>
#include <zconf.h>

//创建互斥锁
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
//创建条件变量
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

#endif //RECORDFRANCO_THREADCONDITION_H
