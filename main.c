#include "packetCapture.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <pcap/pcap.h>
#include <fcntl.h>
#include <wait.h>
#include "route.h"
#include <stdlib.h>
#include "record-types.h"
#include "packetParse.h"

int g_sipport = 5060;
int g_rtpStartPort = 16384;
int g_rtpEndPort = 17586;
int g_portRedis = 6379;
char g_host[IPV4_MAX_LENGTH] = {0};
char g_hostRedis[IPV4_MAX_LENGTH] = {0};
char g_mqName[PATH_MAX] = {0};
int g_mqNameLen = 0;

static void printUsage()
{
    printf("--sipport\tsip port, default is 5060\n");
    printf("--rtpstartport\tRTP Port Range, start. Default is 16384\n");
    printf("--rtpendport\tRTP Port Range, end. Default is 17586\n");
    printf("--hostip\tsip server ip. Default is 192.168.1.100\n");
    printf("--mqname\tredis message queue name. Default is record-packet-queue\n");
    printf("--hostredis\tredis server ip. Default is 192.168.1.3\n");
    printf("--portredis\tredis server port. Default is 6379\n");
}

static int parseParam(int argc, char  const *argv[])
{
    int index = 0;
    for (index = 0; index < argc; ++index) {
        if (0 == strcmp("--sipport", argv[index])) {
            g_sipport = atoi(argv[index+1]);
            index += 1;
        }
        else if (0 == strcmp("--hostip", argv[index])) {
            strcpy(g_host, argv[index+1]);
            index += 1;
        } else if (0 == strcmp("--rtpstartport", argv[index])) {
            g_rtpStartPort = atoi(argv[index + 1]);
            index += 1;
        } else if (0 == strcmp("--rtpendport", argv[index])) {
            g_rtpEndPort = atoi(argv[index + 1]);
            index += 1;
        } else if (0 == strcmp("--mqname", argv[index])) {
            strcpy(g_mqName, argv[index + 1]);
            g_mqNameLen = strlen(g_mqName);
            index += 1;
        } else if (0 == strcmp("--hostredis", argv[index])) {
            strcpy(g_hostRedis, argv[index + 1]);
            index += 1;
        } else if (0 == strcmp("--portredis", argv[index])) {
            g_portRedis = atoi(argv[index + 1]);
            index += 1;
        }
    }

    if (0 == strlen(g_mqName)) {
        strcpy(g_mqName, "record-packet-queue0");
        g_mqNameLen = strlen(g_mqName);
    }
    if (0 == strlen(g_host)) {
        strcpy(g_host, "192.168.1.13");
    }
    if (0 == strlen(g_hostRedis)) {
        strcpy(g_hostRedis, "192.168.1.13");
    }

    return 0;
}

int main(int argc, char const *argv[])
{
    char errBuf[PCAP_ERRBUF_SIZE];
    char filterBuf[FILTER_BUF_MAX_LENGTH] = {0};
    char selectInterfaceName[PCAP_ERRBUF_SIZE];
    int index = 0;
    pid_t pid = 0;

    // parse option param
    if (-1 == parseParam(argc, argv))
    {
        printf("Usage:\n");
        printUsage();
        return -1;
    }

    if ( RECORD_PACKET_PARSE_OK != packetParse_initEnv(g_mqName)) {
        fprintf(stderr, "Error! Initialize packet parse environment Error!\n");
        return -1;
    }
    packetParse_startup();
}