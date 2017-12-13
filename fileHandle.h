#ifndef FILE_HANDLE_H
#define FILE_HANDLE_H

#include <sys/types.h>
#include <stdint.h>

typedef struct FileHandle FileHandle_t;

enum
{
    FILE_HANDLE_OK = 0,
    FILE_HANDLE_ERR_MEM_ALLOC,
    FILE_HANDLE_ERR_MUTEX_INIT,
    FILE_HANDLE_ERR_MUTEX_NULL,
    FILE_HANDLE_ERR_MUTEX_LOCK,
    FILE_HANDLE_ERR_FILE_OPEN,
    FILE_HANDLE_ERR_FILE_NULL_HANDLE,
    FILE_HANDLE_ERR_GENERAL
};

int fileHandle_init();
int fileHandle_addNode(int fileID, const char *fileName);
int fileHandle_writeDataToFile(int fileID, const u_char *data, uint32_t dataLen);
int fileHandle_delNode(int fileID);

#endif