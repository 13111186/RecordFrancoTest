#include "packetCapture.h"
#include "route.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <pcap.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <stdlib.h>
#include <hiredis/hiredis.h>

//static void capturePacketCallback(u_char *userArg, const struct pcap_pkthdr *pkthdr, const u_char *packet);

static redisContext *g_redisClient_CapturePacket = NULL;

int packetCapture_initCaptureEnv(RECORD_PARAM_OUT pcap_if_t **devList, RECORD_PARAM_OUT char *errBuf)
{

    if (-1 == pcap_findalldevs(devList, errBuf))
    {
        printf("Error for find device list.[%s]\n", errBuf);
        return RECORD_PACKET_CAPTURE_ERR_GET_DEV_LIST;
    }
    // redis Connector
    {
        struct timeval timeout = { 1, 500000 }; // 1.5 seconds
        extern char g_hostRedis[IPV4_MAX_LENGTH];
        extern int g_portRedis;
        g_redisClient_CapturePacket = redisConnectWithTimeout(g_hostRedis, g_portRedis, timeout);
        if (g_redisClient_CapturePacket == NULL || g_redisClient_CapturePacket->err) {
            if (g_redisClient_CapturePacket) {
                printf("Connection error: %s\n", g_redisClient_CapturePacket->errstr);
                redisFree(g_redisClient_CapturePacket);
            } else {
                printf("Connection error: can't allocate redis context\n");
            }
            return RECORD_PACKET_CAPTURE_ERR_INIT_REDIS;
        }
    }
    return RECORD_PACKET_CAPTURE_OK;
}

int packetCapture_selectDevice(RECORD_PARAM_IN pcap_if_t *devList, RECORD_PARAM_OUT char *selectDeviceName)
{
    int index = 0;
    pcap_if_t *pDev = NULL;

    for (pDev = devList, index = 0; pDev; pDev = pDev->next, ++index)
    {
        printf("Device Num [%d] Name [%s]\n", index, pDev->name);
    }

    while (1)
    {
        int selectIndex = 0;

        printf("Select A Device to Capture Packet!\n");
        //scanf("%d", &selectIndex);
        if (selectIndex < 0 || selectIndex > index)
        {
            printf("Input Error. Only [0 ~ %d] is valid\n", index - 1);
        }
        else
        {
            for (index = 0, pDev = devList; pDev; pDev = pDev->next, ++index)
            {
                if (index == selectIndex)
                {
                    strcpy(selectDeviceName, pDev->name);
                }
            }
            break;
        }
    }

    return RECORD_PACKET_CAPTURE_OK;
}

int packetCapture_openDevice(RECORD_PARAM_IN char *deviceName, RECORD_PARAM_IN const char *strFilter, RECORD_PARAM_OUT pcap_t **handle, RECORD_PARAM_OUT char *errBuf)
{
    int result = RECORD_PACKET_CAPTURE_OK;
    bpf_u_int32 netip = 0;
    bpf_u_int32 netmask = 0;

    printf("Select Interface For Capture Packet [%s]\n", deviceName);

    if (-1 == pcap_lookupnet(deviceName, &netip, &netmask, errBuf))
    {
        printf("Error For Lookup Net Info on Device [%s]\n", deviceName);
        return RECORD_PACKET_CAPTURE_ERR_LOOKUP_NET;
    }

    *handle = pcap_open_live(deviceName, PACKET_MAX_LENGTH, 0, 0, errBuf);
    if (NULL == *handle)
    {
        printf("Error Open device [%s], Error Info [%s]\n", deviceName, errBuf);
        return RECORD_PACKET_CAPTURE_ERR_OPEN_DEVICE;
    }
    if (DLT_EN10MB != pcap_datalink(*handle))
    {
        printf("Error! Device [%s] Not Support Ethernet!\n", deviceName);
        pcap_close(*handle);
        return RECORD_PACKET_CAPTURE_ERR_DATA_LINK_TYPE;
    }

    {
        // add filter to the sniff
        struct bpf_program tmpfilter;

        if (-1 == pcap_compile(*handle, &tmpfilter, strFilter, 0, netmask))
        {
            printf("Error For Compile filter string [%s]. Error Info [%s]\n", strFilter, pcap_geterr(*handle));
            pcap_close(*handle);
            return RECORD_PACKET_CAPTURE_ERR_FILTER_COMPILE;
        }
        if (-1 == pcap_setfilter(*handle, &tmpfilter))
        {
            printf("Error For Set filter [%s]. Error Info [%s]\n", strFilter, pcap_geterr(*handle));
            pcap_close(*handle);
            return RECORD_PACKET_CAPTURE_ERR_FILTER_SET;
        }
        printf("Has Set Filter [%s] for device [%s]\n", strFilter, deviceName);
    }

    return RECORD_PACKET_CAPTURE_OK;
}

/*int packetCapture_startup(RECORD_PARAM_IN pcap_t *handle, RECORD_PARAM_IN u_char *userArg)
{
    return pcap_loop(handle, -1, capturePacketCallback, userArg);
}*/

extern char g_mqName[PATH_MAX];
extern int g_mqNameLen;

/*
char* join3(char *s1, int *s2)
{
    char *result = malloc(strlen(s1)+strlen(s2)+1);//+1 for the zero-terminator
    //in real code you would check for errors in malloc here
    if (result == NULL) exit (1);

    strcpy(result, s1);
    strcat(result, s2);

    return result;
}

char* itostr(char *str, int i) //将i转化位字符串存入str
{
    sprintf(str, "%d", i);
    return str;
}

static void capturePacketCallback(u_char *userArg, const struct pcap_pkthdr *pkthdr, const u_char *packet)
{
    struct ether_header *etheader = NULL;
    u_int32_t pktLen = pkthdr->len;
    static const char *redisCmd[3] = {"LPUSH", g_mqName, NULL};
    static size_t redisParamLen[3] = {5, 19, 0*/
/*strlen("LPUSH"), strlen("record-packet-queue"), 0*//*
};
    static redisReply *tmpReply = NULL;
    etheader = (struct ether_header *)packet;

    if ( ETHERTYPE_IP == ntohs(etheader->ether_type)) {
        // Write To Redis

        int sum = 0;
        for (int i = 0; i < ETH_ALEN; ++i) {
            sum += etheader->ether_dhost[i];
            sum += etheader->ether_shost[i];
        }

        int routeId = sum % node_num;

        char *str = itostr(str,routeId);
        char *c = join3(g_mqName,str);
        //printf("Concatenated String is %s\n", c);

        //strcpy(g_mqName,c);
        redisCmd[1] = c;
        redisParamLen[1] = strlen(c);

        redisCmd[2] = (const char *)packet;
        redisParamLen[2] = pkthdr->len;

        tmpReply = redisCommandArgv(g_redisClient_CapturePacket, sizeof(redisCmd)/sizeof(redisCmd[0]), redisCmd, redisParamLen);
        if (NULL != tmpReply && REDIS_REPLY_ERROR == tmpReply->type) {
            fprintf(stderr, "Error! Push to queue [%s]\n", tmpReply->str);
        }
        if (NULL != tmpReply) {
            freeReplyObject(tmpReply);
        }
        free(c);
        c = NULL;

    } else {
        fprintf(stderr, "Error! Is not a IP Packet!\n");
    }
}*/
