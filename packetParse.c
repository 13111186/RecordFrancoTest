#include <pthread.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <assert.h>
#include <hiredis/hiredis.h>
#include <stdbool.h>

#include "sipmessage.h"
#include "sdp.h"
#include "channelinfo.h"
#include "packetParse.h"
#include "record-util-queue.h"
#include "record-types.h"
#include "fileHandle.h"
#include "threadCondition.h"
#include <time.h>
//
// Created by root on 17-10-17.
//
static record_queue_t *g_queSIPPacket = NULL;
static record_queue_t *g_queRTPPacket = NULL;

static int packetParse_createChildThread();
static int packetParse_packetClassify(int packetLen, u_char *packet);

static void *packetParse_packetRead(void *arg);
static void *packetParse_sipPacketHandle(void *arg);
static void *packetParse_rtpPacketHandle(void *arg);

static void udpHandle(const u_char *srcip, const u_char *dstip, const u_char *udpPacket);
static void ipHandle(const u_char *ipPacket);

static void rtpHandle(int srcport, int dstport, const u_char *srcip, const u_char *dstip, const char *rtpPacket, int packetLen);

extern int g_sipport;
extern int g_rtpStartPort;
extern int g_rtpEndPort;
extern char g_host[IPV4_MAX_LENGTH];
extern char g_hostRedis[IPV4_MAX_LENGTH];
extern char g_mqName[PATH_MAX];
extern int g_portRedis;

static redisContext *g_redisClient_ParsePacket = NULL;

int packetParse_initEnv()
{
    /*
     * 打开fifo
     * 创建SIP 对应的消息队列
     * 创建RTP 对应的消息队列
    */

    if (FILE_HANDLE_OK != fileHandle_init()) {
        fprintf(stderr, "Error! Init file handle struct!\n");
        return RECORD_PACKET_PARSE_E_GENERAL;
    }

    if (RECORD_QUEUE_OK != record_queue_init(&g_queSIPPacket)) {
        fprintf(stderr, "Error! Init SIP Packet Queue Error!\n");
        return RECORD_PACKET_PARSE_E_GENERAL;
    }

    if (RECORD_QUEUE_OK != record_queue_init(&g_queRTPPacket)) {
        fprintf(stderr, "Error! Init RTP Packet Queue Error!\n");
        record_queue_destory(g_queSIPPacket);
        return RECORD_PACKET_PARSE_E_GENERAL;
    }

    // redis Connector
    {
        struct timeval timeout = { 1, 500000 }; // 1.5 seconds
        extern char g_hostRedis[IPV4_MAX_LENGTH];
        extern int g_portRedis;
        g_redisClient_ParsePacket = redisConnectWithTimeout(g_hostRedis, g_portRedis, timeout);
        if (g_redisClient_ParsePacket == NULL || g_redisClient_ParsePacket->err) {
            if (g_redisClient_ParsePacket) {
                printf("Connection error: %s\n", g_redisClient_ParsePacket->errstr);
                redisFree(g_redisClient_ParsePacket);
            } else {
                printf("Connection error: can't allocate redis context\n");
            }
            return RECORD_PACKET_PARSE_E_INIT_REDIS;
        }
    }

    return RECORD_PACKET_PARSE_OK;
}

int packetParse_startup()
{
    /*
     * 创建1+n个线程，1个线程读取SIP Packet队列，其余n个读取RTP Packet队列
     * 从fd中读取数据，根据消息类型(SIP or RTP)，将消息存到不同的队列中
    */
    return packetParse_createChildThread();
}

static int packetParse_createChildThread()
{
    pthread_t thd_packetRead = 0;
    pthread_t thd_sipPacketParse = 0;
    pthread_t thd_rtpPacketParse = 0;
    void *packetReadThdRtn;
    void *sipPacketParseThdRtn;
    void *rtpPacketParseThdRtn;
    int err = 0;

    err = pthread_create(&thd_packetRead, NULL, packetParse_packetRead, NULL);
    if (0 != err) {
        fprintf(stderr, "Error! Can Not Create Packet Read Thread! [%s]\n", strerror(errno));
        return RECORD_PACKET_PARSE_E_GENERAL;
    }

    err = pthread_create(&thd_sipPacketParse, NULL, packetParse_sipPacketHandle, NULL);
    if (0 != err) {
        fprintf(stderr, "Error! Can Not Create SIP Packet Parse Thread! [%s]\n", strerror(errno));
        pthread_cancel(thd_packetRead);
        return RECORD_PACKET_PARSE_E_GENERAL;
    }

    err = pthread_create(&thd_rtpPacketParse, NULL, packetParse_rtpPacketHandle, NULL);
    if (0 != err) {
        fprintf(stderr, "Error! Can Not Create RTP Packet Parse Thread! [%s]\n", strerror(errno));
        pthread_cancel(thd_packetRead);
        pthread_cancel(thd_sipPacketParse);
        return RECORD_PACKET_PARSE_E_GENERAL;
    }

    fprintf(stdout, "thread packet read id [%ld]\n", thd_packetRead);
    fprintf(stdout, "thread sip packet parse id [%ld]\n", thd_sipPacketParse);
    fprintf(stdout, "thread rtp packet parse id [%ld]\n", thd_rtpPacketParse);

    pthread_join(thd_packetRead, &packetReadThdRtn);
    pthread_join(thd_sipPacketParse, &sipPacketParseThdRtn);
    pthread_join(thd_rtpPacketParse, &rtpPacketParseThdRtn);

    pthread_mutex_destroy(&mutex);
    //销毁条件变量
    pthread_cond_destroy(&cond);

    return RECORD_PACKET_PARSE_OK;
}

static void *packetParse_packetRead(void *arg)
{
    /*
     * 从 *arg指定的文件描述符中读取数据包，然后根据端口，存放到对应的队列中
    */
    int packetLen = 0;
    u_char *buf = NULL;
    int retry = 0;
    time_t tt;
    //tm sTime = {0};
    char tmpbuf[80];



    static char redisCmd[] = "BRPOP";
    static char redisListName[] = "record-packet-queue";
    static redisReply *tmpReply = NULL;

    printf("packet read thread id: %ld\n", pthread_self());

    while (true) {
        tmpReply = redisCommand(g_redisClient_ParsePacket, "%s %s 0", redisCmd, g_mqName);
        if (NULL != tmpReply && REDIS_REPLY_ARRAY == tmpReply->type && 2 == tmpReply->elements) {
            if (tmpReply->element[1]->len < RECORD_ETH_MIN_LENGTH) {
                continue;
            }
            packetLen = tmpReply->element[1]->len;
            buf = (u_char *)malloc(packetLen * sizeof(u_char));
            while (NULL == buf && retry < 50) {
                fprintf(stderr, "Error! Can not allocate memory for buf, Will try after 5 seconds\n");
                sleep(5);
                buf = (u_char *)malloc(packetLen * sizeof(u_char));
                retry += 1;
            }
            if (NULL == buf) {
                fprintf(stderr, "Error! Can not allocate memory for buf.Program will exit!\n");
                pthread_exit((void *)(-1));
            }
            memcpy(buf, tmpReply->element[1]->str, packetLen);
            if ( RECORD_PACKET_PARSE_OK != packetParse_packetClassify(packetLen, buf)) {
                free(buf);
                buf = NULL;
            }
        }
        freeReplyObject(tmpReply);
    }

    return ((void*)0);
}

static int packetParse_packetClassify(int packetLen, u_char *packet)
{

    struct ether_header *etheader = NULL;
    struct iphdr *ipheader = NULL;
    struct udphdr *udpheader = NULL; //(struct udphdr *)udpPacket;
    u_char srcip[16] = {0};
    u_char dstip[16] = {0};
    int srcport = 0;
    int dstport = 0;

    if (packetLen < (sizeof(struct ether_header)) + sizeof(struct iphdr)) {
        return RECORD_PACKET_PARSE_E_BAD_PACKET;
    }

    etheader = (struct ether_header *)packet;
    if (ETHERTYPE_IP != ntohs(etheader->ether_type)) {
        return RECORD_PACKET_PARSE_E_NOT_IP_PACKET;
    }

    ipheader = (struct iphdr *) (packet + sizeof(struct ether_header));
    strcpy(srcip, inet_ntoa(*((struct in_addr *)(&(ipheader->saddr)))));
    strcpy(dstip, inet_ntoa(*((struct in_addr *)(&(ipheader->daddr)))));

    if (packetLen < (sizeof(struct ether_header) + sizeof(struct iphdr) + ipheader->ihl * 4)) {
        return RECORD_PACKET_PARSE_E_BAD_PACKET;
    }

    udpheader = (struct udphdr *)(packet + sizeof(struct ether_header) + ipheader->ihl * 4);
    srcport = ntohs(udpheader->uh_sport);
    dstport = ntohs(udpheader->uh_dport);

    if (IPPROTO_UDP != ipheader->protocol) {
        return RECORD_PACKET_PARSE_E_NOT_UDP_PACKET;
    }
    if ( (0 == strcmp(g_host, srcip) && srcport == g_sipport)
         || (0 == strcmp(g_host, dstip) && dstport == g_sipport) ) {
        //SIP Packet
        pthread_mutex_lock(&mutex);
        //usleep(1);
        int res = record_queue_push(g_queSIPPacket, packet);
        pthread_mutex_unlock(&mutex);
        //printf("Producer sell : %d\n",data);
        pthread_cond_signal(&cond);
        if (RECORD_QUEUE_OK == res) {
            return RECORD_PACKET_PARSE_OK;
        } else {
            return RECORD_PACKET_PARSE_E_PUSH_SIP_QUEUE;
        }
    } else if (srcport >= g_rtpStartPort && srcport <= g_rtpEndPort) {
        //RTP Packet
        pthread_mutex_lock(&mutex);
        //usleep(1);
        int res = record_queue_push(g_queRTPPacket, packet);
        pthread_mutex_unlock(&mutex);
        //printf("Producer sell : %d\n",data);
        pthread_cond_signal(&cond);
        if (RECORD_QUEUE_OK == res) {
            return RECORD_PACKET_PARSE_OK;
        } else {
            return RECORD_PACKET_PARSE_E_PUSH_RTP_QUEUE;
        }
    } else {
        return RECORD_PACKET_PARSE_E_GENERAL;
    }
}

static void *packetParse_sipPacketHandle(void *arg)
{
    /*
     * 从g_queSIPPacket中获取获取数据包，然后解析
     * !!数据包处理完成后，必须free释放内存
    */
    u_char *packet = NULL;

    /*while (1) {
        while (packet = record_queue_pop(g_queSIPPacket)) {
            ipHandle(packet + sizeof(struct ethhdr));
            free(packet);
        }
    }*/
    while (1) {
        pthread_mutex_lock(&mutex);
        while (is_empty(g_queSIPPacket) == 0) {
            pthread_cond_wait(&cond,&mutex);
        }
        packet = record_queue_pop(g_queSIPPacket);
        ipHandle(packet + sizeof(struct ethhdr));
        free(packet);
        pthread_mutex_unlock(&mutex);
    }
    return ((void*)0);
}

static void *packetParse_rtpPacketHandle(void *arg)
{
    /*
     * 从g_queRTPPacket中获取获取数据包，然后解析
     * !!数据包处理完成后，必须free释放内存
    */

    u_char *packet = NULL;

    /*while (1) {
        while (packet = record_queue_pop(g_queRTPPacket)) {
            ipHandle(packet + sizeof(struct ethhdr));
            free(packet);
        }
    }*/
    while (1) {
        pthread_mutex_lock(&mutex);
        while (is_empty(g_queRTPPacket) == 0) {
            pthread_cond_wait(&cond,&mutex);
        }
        packet = record_queue_pop(g_queRTPPacket);
        ipHandle(packet + sizeof(struct ethhdr));
        free(packet);
        pthread_mutex_unlock(&mutex);
    }
    return ((void*)0);
}

void ipHandle(const u_char *ipPacket)
{
    const struct iphdr *header = NULL;
    u_char srcip[16] = {0};
    u_char dstip[16] = {0};

    header = (const struct iphdr *) ipPacket;
    strcpy(srcip, inet_ntoa(*((const struct in_addr *)(&(header->saddr)))));
    strcpy(dstip, inet_ntoa(*((const struct in_addr *)(&(header->daddr)))));

    switch (header->protocol)
    {
        case IPPROTO_TCP:
            printf("this is tcp packet!\n");
            break;
        case IPPROTO_UDP:
            // printf("this is udp packet!\n");
            udpHandle(srcip, dstip, ipPacket + (header->ihl * 4));
            break;
        case IPPROTO_IGMP:
            printf("this is IGMP packet!\n");
            break;
        case IPPROTO_ICMP:
            printf("this is ICMP packet!\n");
            break;
    }
}

void udpHandle(const u_char *srcip, const u_char *dstip, const u_char *udpPacket)
{
    /*
     * UDP 头部长度字段，包含UDP头长度
    */
    struct udphdr *header = (struct udphdr *)udpPacket;
    int srcport = ntohs(header->uh_sport);
    int dstport = ntohs(header->uh_dport);
    int datalen = ntohs(header->uh_ulen) - 8;	//udp packet length is fixed 8Bytes
    int index = 0;
    u_char firstLine[256] = {0};

    // printf("src port [%d]\n", srcport);
    // printf("dst port [%d]\n", dstport);
    // printf("udp length [%d]\n", datalen);

    if ((srcport == g_sipport && 0 == strcmp(srcip, g_host))
        || (dstport == g_sipport && 0 == strcmp(dstip, g_host))) {

        memcpy(firstLine, udpPacket + 8, sizeof(firstLine) - 1);
        if (NULL != strstr(firstLine, "SIP/2.0"))
        {
            printf("===============================================================================\n");
            printf("SIP Content:\n");
            fwrite(udpPacket + 8, 1, datalen, stdout);
            sipParse(udpPacket + 8, datalen, dstip, dstport);
            printf("===============================================================================\n");
        }
    } else if ((srcport >= g_rtpStartPort) && (srcport <= g_rtpEndPort) && (0 == strcmp(srcip, g_host))) {
        rtpHandle(srcport, dstport, srcip, dstip, udpPacket+8, datalen);
    }
}

static int sip_request_message_handle(sip_message_t *sipMessage, const u_char *dstip, int dstport);
static int sip_request_message_invite_handle(sip_message_t *sipMessage, const u_char *dstip, int dstport);

static int sip_response_message_handle(sip_message_t *sipMessage);
static int sip_response_message_200_handle(sip_message_t *channelInfo);

int sipParse(const u_char *sipPacket, int packetLen, const u_char *dstip, int dstport)
{
    // char tmpsipMessage[RECORD_SIP_MESSAGE_MAX_LENGTH] = {0};
    sip_message_t *sipMessage = NULL;
    int result = 0;

    // memcpy(tmpsipMessage, sipPacket, packetLen);

    result = record_sip_message_init(&sipMessage);

    if (result) {
        fprintf(stderr, "Error! record_sip_message_init, return value [%d]\n", result);
        return result;
    }

    result = record_sip_message_parse(sipMessage, sipPacket, packetLen);
    printf("result = record_sip_message_parse(&sipMessage) : %d \n",result);
    if ( RECORD_SIP_MSG_PARSE_RESULT_OK != result) {
        //printf("===================================================================");
        fprintf(stderr, "Error! record_sip_message_parse, return value [%d]\n", result);
        record_sip_message_free(sipMessage);
        return result;
    }

    switch (record_sip_message_get_message_type(sipMessage))
    {
        case SIP_MT_REQUEST:
            sip_request_message_handle(sipMessage, dstip, dstport);
            break;
        case SIP_MT_RESPONSE:
            sip_response_message_handle(sipMessage);
            break;
        default:
            fprintf(stderr, "Error, SIP Message Type Cannot Be Recognised!\n");
    }
    record_sip_message_free(sipMessage);
    return 0;
}

static int sip_request_message_handle(sip_message_t *sipMessage, const u_char *dstip, int dstport)
{
    const u_char *sip_method = NULL;
    const u_char *callid = NULL;
    channelInfo_t *channelInfo = NULL;

    channelState_t currentChannelState = CS_NONE;

    assert(sipMessage);
    sip_method = record_sip_message_get_sip_method(sipMessage);
    callid = record_sip_message_get_callid(sipMessage);
    assert(sip_method);
    assert(callid);
    channelInfo = record_channelinfo_get_by_callid(callid);

    if (NULL != channelInfo) {
        currentChannelState = record_channelinfo_get_current_state(channelInfo);
    }

    if (0 == strcasecmp("INVITE", sip_method) && CS_NONE == currentChannelState) {
        sip_request_message_invite_handle(sipMessage, dstip, dstport);
    } else if (0 == strcasecmp("BYE", sip_method) && CS_ESTABLISHED == currentChannelState) {
        if (NULL == channelInfo) {
            fprintf(stderr, "Error! Can not find channel info call id [%s]\n", callid);
            return RECORD_SIP_MSG_HANDLE_RESULT_BED_VALUE;
        }
        record_channelinfo_set_current_state(channelInfo, CS_BYE);
    } else if (0 == strcasecmp("CANCEL", sip_method) && CS_INVITE == currentChannelState) {
        if (NULL == channelInfo) {
            fprintf(stderr, "Error! Can not find channel info call id [%s]\n", callid);
            return RECORD_SIP_MSG_HANDLE_RESULT_BED_VALUE;
        }
        record_channelinfo_set_current_state(channelInfo, CS_CANCEL);
    } else {
        fprintf(stdout, "Don't Need to Handle Method [%s]\n", sip_method);
    }
    return RECORD_SIP_MSG_HANDLE_RESULT_OK;
}

static int sip_response_message_handle(sip_message_t *sipMessage)
{
    int status_code = 0;
    const u_char *psip_method = NULL;
    status_code = record_sip_message_get_status_code(sipMessage);
    psip_method = record_sip_message_get_sip_method(sipMessage);
    //只处理INVITE、CANCEL、BYE对应的200 OK
    if (psip_method && strcasecmp(psip_method, "INVITE")
        && strcasecmp(psip_method, "CANCEL")
        && strcasecmp(psip_method, "BYE")) {
        fprintf(stdout, "Response Message Foucs on INVITE 、CANCEL、BYE\n");
        return RECORD_SIP_MSG_HANDLE_RESULT_OK;
    }

    if (200 == status_code) {
        sip_response_message_200_handle(sipMessage);
    } else {
        fprintf(stdout, "Don't Need to Handle Status Code [%d]\n", status_code);
    }
    return RECORD_SIP_MSG_HANDLE_RESULT_OK;
}

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
    u_int16_t	alignUnit;		//2 Bytes 数据块对齐单位
    u_int16_t	sampleBit;		//2 Bytes 采样位数，每个采样值，所用的二进制位数
    u_char 		dataTag[4];		//4 Bytes "data"
    uint32_t	dataLen;		//4 Bytes 音频数据长度，不包含waveFileHeader的长度
}waveFileHeader_t;

typedef struct alawWaveFileHeader {
    u_char 		riffID[4];		//4 Bytes RIFF
    uint32_t	fileLen;		//4 Bytes 整个文件长度(不包含riffID的四个字节和本字段的四个字节)
    u_char		fmtType[4];		//4 Bytes "WAVE"
    u_char		fmtTag[4];		//4 Bytes fmt和一个空格, "fmt "
    uint32_t	chunkLen;		//4 Bytes 格式块长度， alaw 为 0x12
    uint16_t	codecID;		//2 Bytes 编码格式代码，alaw 为 0x06
    uint16_t	channelNum;		//2 Bytes 声道数目，1或2
    uint32_t	sampleRate;		//4 Bytes 采样率
    uint32_t	dataTransRate;	//4 Bytes 传输速率，数值为：通道数×采样频率×每样本的数据位数/8
    u_int16_t	alignUnit;		//2 Bytes 数据块对齐单位
    u_int32_t	sampleBit;		//4 Bytes 采样位数，每个采样值，所用的二进制位数
    u_char 		factType[4];	//4 Bytes "fact"
    u_int32_t	temp1;			//4 Bytes 0x04000000H
    u_int32_t	temp2;			//4 Bytes 0x00530700H
    u_char 		dataTag[4];		//4 Bytes "data"
    uint32_t	dataLen;		//4 Bytes 音频数据长度，不包含waveFileHeader的长度
} alawWaveFileHeader_t;

static int writeAlawWaveFileHeaderToFile(const alawWaveFileHeader_t *header, FILE *file)
{
    fwrite(header->riffID, 1, 4, file);
    fwrite(&(header->fileLen), 4, 1, file);
    fwrite(header->fmtType, 1, 4, file);
    fwrite(header->fmtTag, 1, 4, file);
    fwrite(&(header->chunkLen), 4, 1, file);
    fwrite(&(header->codecID), 2, 1, file);
    fwrite(&(header->channelNum), 2, 1, file);
    fwrite(&(header->sampleRate), 4, 1, file);
    fwrite(&(header->dataTransRate), 4, 1, file);
    fwrite(&(header->alignUnit), 2, 1, file);
    fwrite(&(header->sampleBit), 4, 1, file);
    fwrite(header->factType, 1, 4, file);
    fwrite(&(header->temp1), 4, 1, file);
    fwrite(&(header->temp2), 4, 1, file);
    fwrite(header->dataTag, 1, 4, file);
    fwrite(&(header->dataLen), 4, 1, file);
    return 0;
}

typedef struct FileHandle{
    int port;
    FILE *file;
    struct FileHandle *next;
} FileHandle_t;

static FileHandle_t *g_pfileHandle[FILE_HANDLE_BUF_MAX_SIZE] = {0};

static int sip_response_message_200_handle(sip_message_t *sipMessage)
{
    const u_char *callid = NULL;
    channelState_t channelState = CS_INVALID_CHANNEL_STATE;
    channelInfo_t *channelInfo = NULL;
    callDirection_t callDirection = 0;
    FileHandle_t *ptmpFileHandle = NULL;
    const sdp_t *sdp = NULL;
    FILE *ptmpFile = NULL;
    char fileNameBuf[RECORD_PATH_LEN] = {0};
    int fromport = 0;
    int toport = 0;
    int fileID = 0;

    assert(sipMessage);

    callid = record_sip_message_get_callid(sipMessage);
    assert(callid);
    channelInfo = record_channelinfo_get_by_callid(callid);

    if (NULL == channelInfo) {
        fprintf(stderr, "Error! Can not find channel info about callid [%s]\n", callid);
        return RECORD_SIP_MSG_HANDLE_RESULT_BED_VALUE;
    }
    channelState = record_channelinfo_get_current_state(channelInfo);
    callDirection =  record_channelinfo_get_call_direction(channelInfo);

    if (CS_INVITE == channelState) {

        int bport = 0;
        sdp = record_sip_message_get_sdp(sipMessage);
        assert(sdp);
        bport = record_sdp_get_audio_port(sdp);

        record_channelinfo_set_audio_b_port(channelInfo, bport);
        //CallDirection，获取服务端对应的RTP端口，根据该端口值设置音频文件句柄
        if (CD_INBOUND == callDirection) {
            fromport = record_channelinfo_get_audio_b_port(channelInfo);
            toport = record_channelinfo_get_audio_a_port(channelInfo);
        } else if (CD_OUTBOUND == callDirection) {
            fromport = record_channelinfo_get_audio_a_port(channelInfo);
            toport = record_channelinfo_get_audio_b_port(channelInfo);
        }

        sprintf(fileNameBuf, "/home/franco/RecordWav/%s-%s-%d.wav",
                record_channelinfo_get_caller_name(channelInfo), record_channelinfo_get_callee_name(channelInfo), fromport);
        fprintf(stdout, "Wave File [%s]\n", fileNameBuf);
        fileHandle_addNode(fromport, fileNameBuf);
        record_channelinfo_set_current_state(channelInfo, CS_ESTABLISHED);

    } else if (CS_BYE == channelState || CS_CANCEL == channelState) {
        //如果是BYE或者CANCEL的200 OK，则不包含SDP信息
        if (CD_INBOUND == callDirection) {
            fileID = record_channelinfo_get_audio_b_port(channelInfo);
        } else if (CD_OUTBOUND == callDirection) {
            fileID = record_channelinfo_get_audio_a_port(channelInfo);
        }
        record_channelinfo_set_current_state(channelInfo, CS_INVALID_CHANNEL_STATE);

        fileHandle_delNode(fileID);

        record_channelinfo_del_channel(callid);
    }
}

static int sip_request_message_invite_handle(sip_message_t *sipMessage, const u_char *dstip, int dstport)
{
    const u_char *callid = NULL;
    channelInfo_t *channelInfo = NULL;
    callDirection_t callDirection = CD_INVALID;
    const sdp_t *sdp = NULL;

    callid = record_sip_message_get_callid(sipMessage);
    assert(callid);

    sdp = record_sip_message_get_sdp(sipMessage);
    assert(sdp);

    record_channelinfo_init(&channelInfo);
    assert(channelInfo);

    if (dstport == g_sipport && 0 == strcmp(dstip, g_host)) {
        callDirection = CD_INBOUND;
    } else {
        callDirection = CD_OUTBOUND;
    }

    record_channelinfo_set_audio_a_port(channelInfo, record_sdp_get_audio_port(sdp));
    record_channelinfo_set_call_direction(channelInfo, callDirection);
    record_channelinfo_set_callid(channelInfo, callid);
    record_channelinfo_set_current_state(channelInfo, CS_INVITE);
    record_channelinfo_set_from(channelInfo, record_sip_message_get_from(sipMessage));
    record_channelinfo_set_to(channelInfo, record_sip_message_get_to(sipMessage));

    record_channelinfo_add_new_channel(channelInfo);

    return RECORD_SIP_MSG_HANDLE_RESULT_OK;
}

typedef struct rtphdr_
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
    u_int16_t cc : 4;
    u_int16_t x : 1;
    u_int16_t p : 1;
    u_int16_t v : 2;
    u_int16_t pt : 7;
    u_int16_t m : 1;
#elif __BYTE_ORDER == __BIG_ENDIAN
    u_int16_t v : 2;
	u_int16_t p : 1;
	u_int16_t x : 1;
	u_int16_t cc : 4;
	u_int16_t m : 1;
	u_int16_t pt : 7;
#endif
    u_int16_t seq;
} rtphdr;

static void rtpHandle(int srcport, int dstport, const u_char *srcip, const u_char *dstip, const char *rtpPacket, int packetLen)
{
    rtphdr tmprtphdr;
    int fileID = 0;

    memcpy(&tmprtphdr, rtpPacket, sizeof(rtphdr));
    // printf("rtp packet info\nversion: [%d] pandding: [%d] extension: [%d] CSRC: [%d] m: [%d] pt: [%d]\n", \
	// 	tmprtphdr.v, tmprtphdr.p, tmprtphdr.x, tmprtphdr.cc, tmprtphdr.m, tmprtphdr.pt);
    // printf("rtp seq [%d]\n", ntohs(tmprtphdr.seq));

    if (0 == tmprtphdr.cc)
    {
        //RTP header length is 12 if cc is 0
        if (0 == strcmp(srcip, g_host))	{
            fileID = srcport;
        } else {
            return ;
        }
        fileHandle_writeDataToFile(fileID, rtpPacket + 12, packetLen - 12);
    }
    else
    {
        // the Contributing Source (CSRC) Identifier List is NULL, need to consider how to handle this field
    }
}