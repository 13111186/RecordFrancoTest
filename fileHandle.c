#include "fileHandle.h"
#include "record-types.h"

#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct waveFileHeader{
    u_char 		riffID[4];		//4 Bytes RIFF
    uint32_t	fileLen;		//4 Bytes 整个文件长度(不包含riffID的四个字节和本字段的四个字节)
    u_char		fmtType[4];		//4 Bytes "WAVE"
    u_char		fmtTag[4];		//4 Bytes fmt和一个空格, "fmt "
    uint32_t	chunkLen;		//4 Bytes 格式块长度， 数值取决于编码格式，可以是16、18、20、40等，FS保存的文件为16
    uint16_t	codecID;		//2 Bytes 编码格式代码，PCM脉冲编码调制格式时，该值为 1
    uint16_t	channelNum;		//2 Bytes 声道数目，1或2
    uint32_t	sampleRate;		//4 Bytes 采样率
    uint32_t	dataTransRate;	//4 Bytes 传输速率，数值为：通道数×采样频率×每样本的数据位数/8
    uint16_t	alignUnit;		//2 Bytes 数据块对齐单位
    uint16_t	sampleBit;		//2 Bytes 采样位数，每个采样值，所用的二进制位数
    u_char 		dataTag[4];		//4 Bytes "data"
    uint32_t	dataLen;		//4 Bytes 音频数据长度，不包含waveFileHeader的长度
} waveFileHeader_t;

struct FileHandleNode {
    uint32_t fileID;
    FILE *file;
    pthread_mutex_t *mutexFile;
    struct FileHandleNode *next;
};

struct FileHandle {
    struct FileHandleNode *fileHandleNode;
    pthread_mutex_t *mutexFileHandleNode;
};

static FileHandle_t g_fileHandle[FILE_HANDLE_BUF_MAX_SIZE] = {0};

static int16_t ALaw_Decode(int8_t number);
static void writeWavFileHeaderToFile(FILE *pfile, uint32_t dataLen);
static int getFileHandleNode(int fileID, struct FileHandleNode **pfhNode);
static int writeDataToFile(int fileID, struct FileHandleNode *ptmpFileHandle, const u_char *data, uint32_t dataLen);

int fileHandle_init()
{
    int index = 0;
    pthread_mutex_t *pmutex = NULL;
    uint32_t result = FILE_HANDLE_OK;
    for (index = 0; index < FILE_HANDLE_BUF_MAX_SIZE; ++index, pmutex = NULL)
    {
        g_fileHandle[index].fileHandleNode = NULL;
        pmutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
        if (NULL != pmutex && 0 == pthread_mutex_init(pmutex, NULL)) {
            g_fileHandle[index].mutexFileHandleNode = pmutex;
            continue ;
        }
        if (NULL != pmutex) {
            free(pmutex);
            result = FILE_HANDLE_ERR_MUTEX_INIT;
        } else {
            result = FILE_HANDLE_ERR_MEM_ALLOC;
        }
        {
            int tmpIndex = 0;
            for (tmpIndex = 0; tmpIndex < index; ++tmpIndex){
                pthread_mutex_destroy(g_fileHandle[tmpIndex].mutexFileHandleNode);
                free(g_fileHandle[tmpIndex].mutexFileHandleNode);
            }
        }
    }
    return result;
}

int fileHandle_addNode(int fileID, const char *fileName)
{
    struct FileHandleNode *ptmpFileHandle = (struct FileHandleNode *)malloc(sizeof(struct FileHandleNode));
    FILE *ptmpFile = NULL;
    pthread_mutex_t *pmutex = NULL;

    if (NULL == ptmpFileHandle) {
        fprintf(stderr, "Error！Error!Error! Can Not Allocate Memory For FileHandle_t!\n");
        return FILE_HANDLE_ERR_MEM_ALLOC;
    }

    ptmpFile = fopen(fileName, "wb+");
    if (NULL == ptmpFile)
    {
        fprintf(stderr, "Error! Can Not Open File [%s]! Error Info [%s]\n", fileName, strerror(errno));
        free(ptmpFileHandle);
        return FILE_HANDLE_ERR_FILE_OPEN;
    }

    pmutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    if (NULL != pmutex && 0 == pthread_mutex_init(pmutex, NULL)) {
        ptmpFileHandle->fileID = fileID;
        ptmpFileHandle->file = ptmpFile;
        ptmpFileHandle->mutexFile = pmutex;

        pthread_mutex_lock(pmutex);
        writeWavFileHeaderToFile(ptmpFile, 0);	//Write WAVE Header, dataLen is 0
        pthread_mutex_unlock(pmutex);
    } else if (NULL != pmutex) {
        fprintf(stderr, "Error! Init mutex error! [%s]\n", strerror(errno));
        free(pmutex);
        fclose(ptmpFile);
        free(ptmpFileHandle);
        return FILE_HANDLE_ERR_MUTEX_INIT;
    } else {
        fprintf(stderr, "Error! Allocate memory error! [%s]\n", strerror(errno));
        fclose(ptmpFile);
        free(ptmpFileHandle);
        return FILE_HANDLE_ERR_MEM_ALLOC;
    }

    pmutex = g_fileHandle[fileID % FILE_HANDLE_BUF_MAX_SIZE].mutexFileHandleNode;
    if (NULL != pmutex) {
        pthread_mutex_lock(pmutex);
        ptmpFileHandle->next = g_fileHandle[fileID % FILE_HANDLE_BUF_MAX_SIZE].fileHandleNode;
        g_fileHandle[fileID % FILE_HANDLE_BUF_MAX_SIZE].fileHandleNode = ptmpFileHandle;
        pthread_mutex_unlock(pmutex);
        return FILE_HANDLE_OK;
    } else {
        pthread_mutex_destroy(ptmpFileHandle->mutexFile);
        free(ptmpFileHandle->mutexFile);
        fclose(ptmpFile);
        free(ptmpFileHandle);
        return FILE_HANDLE_ERR_MUTEX_NULL;
    }
}

int fileHandle_writeDataToFile(int fileID, const u_char *data, uint32_t dataLen)
{
    struct FileHandleNode *ptmpFileHandle = NULL;
    FILE *pfile = NULL;
    int result = FILE_HANDLE_OK;

    result = getFileHandleNode(fileID, &ptmpFileHandle);
    if (FILE_HANDLE_OK != result) {
        return result;
    }
    result = writeDataToFile(fileID, ptmpFileHandle, data, dataLen);
    return result;
}

int fileHandle_delNode(int fileID)
{
    //补充音频文件的长度信息，关闭音频文件，删除FileHandle_t结构
    struct FileHandleNode *ptmpFileHandle = NULL;
    int result = FILE_HANDLE_OK;
    pthread_mutex_t *pmutex = NULL;
    long dataLen = 0;

    result = getFileHandleNode(fileID, &ptmpFileHandle);
    if (FILE_HANDLE_OK != result)
        return result;

    pthread_mutex_lock(ptmpFileHandle->mutexFile);
    fseek(ptmpFileHandle->file, 0, SEEK_END);
    dataLen = ftell(ptmpFileHandle->file);
    writeWavFileHeaderToFile(ptmpFileHandle->file, dataLen - sizeof(waveFileHeader_t));
    fflush(ptmpFileHandle->file);
    fclose(ptmpFileHandle->file);
    ptmpFileHandle->file = NULL;
    pthread_mutex_unlock(ptmpFileHandle->mutexFile);

    {
        struct FileHandleNode *ptmp = g_fileHandle[fileID % FILE_HANDLE_BUF_MAX_SIZE].fileHandleNode;

        pmutex = g_fileHandle[fileID % FILE_HANDLE_BUF_MAX_SIZE].mutexFileHandleNode;
        pthread_mutex_lock(pmutex);
        //the first node is desired!
        if (ptmpFileHandle == g_fileHandle[fileID % FILE_HANDLE_BUF_MAX_SIZE].fileHandleNode) {
            g_fileHandle[fileID % FILE_HANDLE_BUF_MAX_SIZE].fileHandleNode = ptmpFileHandle->next;
        } else {
            while (ptmp && ptmp->next != ptmpFileHandle) {
                ptmp = ptmp->next;
            }
            if (NULL != ptmp) {
                ptmp->next = ptmpFileHandle->next;
            }
        }
        ptmpFileHandle->next = NULL;
        pthread_mutex_unlock(pmutex);

        pthread_mutex_destroy(ptmpFileHandle->mutexFile);
        free(ptmpFileHandle->mutexFile);
        free(ptmpFileHandle);
        result = FILE_HANDLE_OK;
    }
    return result;
}

static int16_t ALaw_Decode(int8_t number)
{
    uint8_t sign = 0x00;
    uint8_t position = 0;
    int16_t decoded = 0;
    number ^= 0x55;
    if (number & 0x80)
    {
        number &= ~(1 << 7);
        sign = -1;
    }
    position = ((number & 0xF0) >> 4) + 4;
    if (position != 4)
    {
        decoded = ((1 << position) | ((number & 0x0F) << (position - 4))
                   | (1 << (position - 5)));
    }
    else
    {
        decoded = (number << 1) | 1;
    }
    return (sign == 0) ? (decoded) : (-decoded);
}

static int getFileHandleNode(int fileID, struct FileHandleNode **pfhNode)
{
    struct FileHandleNode *ptmpFileHandle = NULL;
    pthread_mutex_t *pmutex = NULL;

    pmutex = g_fileHandle[fileID % FILE_HANDLE_BUF_MAX_SIZE].mutexFileHandleNode;
    if (NULL != pmutex && 0 == pthread_mutex_lock(pmutex)) {
        ptmpFileHandle = g_fileHandle[fileID % FILE_HANDLE_BUF_MAX_SIZE].fileHandleNode;
        while (ptmpFileHandle && fileID != ptmpFileHandle->fileID) {
            ptmpFileHandle = ptmpFileHandle->next;
        }
        pthread_mutex_unlock(pmutex);
    } else if (NULL != pmutex) {
        fprintf(stderr, "Error! Mutex lock error!\n");
        return FILE_HANDLE_ERR_MUTEX_LOCK;
    } else {
        fprintf(stderr, "Error! Mute is null for file id [%d]\n", fileID);
        return FILE_HANDLE_ERR_MUTEX_NULL;
    }

    *pfhNode = ptmpFileHandle;
    return FILE_HANDLE_OK;
}

int writeDataToFile(int fileID, struct FileHandleNode *ptmpFileHandle, const u_char *data, uint32_t dataLen)
{
    FILE *pfile = NULL;
    pthread_mutex_t *pmutex = NULL;

    if (NULL != ptmpFileHandle) {
        pmutex = ptmpFileHandle->mutexFile;
        if  ((NULL != pmutex) && (0 == pthread_mutex_lock(pmutex))) {
            if (pfile = ptmpFileHandle->file) {
                uint32_t index = 0;
                uint8_t tmpData = 0;
                uint16_t tmpDataDecoded = 0;
                for (index = 0; index < dataLen; ++index) {
                    tmpData = (uint8_t)data[index];
                    tmpDataDecoded = ALaw_Decode(tmpData);
                    fwrite(&tmpDataDecoded, sizeof(tmpDataDecoded), 1, pfile);
                }
            } else {
                fprintf(stderr, "Error! No FILE handle for file id [%d] Or has closed!\n", fileID);
                pthread_mutex_unlock(pmutex);
                return FILE_HANDLE_ERR_FILE_NULL_HANDLE;
            }
            pthread_mutex_unlock(pmutex);
            return FILE_HANDLE_OK;
        } else if (NULL != pmutex) {
            fprintf(stderr, "Error! Mutex lock error!\n");
            return FILE_HANDLE_ERR_MUTEX_LOCK;
        } else {
            fprintf(stderr, "Error! FILE Mutex is null for file id [%d]\n", fileID);
            return FILE_HANDLE_ERR_MUTEX_NULL;
        }
    } else {
        //fprintf(stderr, "Error! FileHandleNode is null for fileID [%d]\n", fileID);
        return FILE_HANDLE_ERR_GENERAL;
    }
}

static void writeWavFileHeaderToFile(FILE *pfile, uint32_t dataLen)
{
    waveFileHeader_t wavHeader;

    strncpy(wavHeader.riffID, "RIFF", strlen("RIFF"));
    wavHeader.fileLen = dataLen + 36;
    strncpy(wavHeader.fmtType, "WAVE", strlen("WAVE"));
    strncpy(wavHeader.fmtTag, "fmt ", strlen("fmt "));
    wavHeader.chunkLen = 16;
    wavHeader.codecID = 1;
    wavHeader.channelNum = 1;
    wavHeader.sampleRate = 8000;
    wavHeader.dataTransRate = 0;
    wavHeader.alignUnit = 0;
    wavHeader.sampleBit = 16;
    strncpy(wavHeader.dataTag, "data", strlen("data"));
    wavHeader.dataLen = (uint32_t)dataLen;

    wavHeader.dataTransRate = wavHeader.channelNum * wavHeader.sampleRate * wavHeader.sampleBit / 8;
    wavHeader.alignUnit = wavHeader.channelNum * wavHeader.sampleBit / 8;

    fseek(pfile, 0, SEEK_SET);
    fwrite(&(wavHeader.riffID), 1, 4, pfile);
    fwrite(&(wavHeader.fileLen), 4, 1, pfile);
    fwrite(&(wavHeader.fmtType), 1, 4, pfile);
    fwrite(&(wavHeader.fmtTag), 1, 4, pfile);
    fwrite(&(wavHeader.chunkLen), 4, 1, pfile);
    fwrite(&(wavHeader.codecID), 2, 1, pfile);
    fwrite(&(wavHeader.channelNum), 2, 1, pfile);
    fwrite(&(wavHeader.sampleRate), 4, 1, pfile);
    fwrite(&(wavHeader.dataTransRate), 4, 1, pfile);
    fwrite(&(wavHeader.alignUnit), 2, 1, pfile);
    fwrite(&(wavHeader.sampleBit), 2, 1, pfile);
    fwrite(&(wavHeader.dataTag), 1, 4, pfile);
    fwrite(&(wavHeader.dataLen), 4, 1, pfile);
}