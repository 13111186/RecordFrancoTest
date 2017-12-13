#include "channelinfo.h"
#include "record-types.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

struct ChannelInfo_
{
    u_char callid[64];
    u_char from[64];
    u_char to[64];
    u_char calleeName[32];
    u_char callerName[32];
    callDirection_t callDirection;
    channelState_t currentState;
    u_int16_t endPointAAudioRtpPort;	//发送INVITE端的audio port；
    u_int16_t endPointBAudioRtpPort;	//回复200 OK的audio port；
    struct ChannelInfo_ *next;
};

channelInfo_t       *g_channelinfo_list = NULL;
pthread_mutex_t     g_channelInfoList_mutex = PTHREAD_MUTEX_INITIALIZER;

int record_channelinfo_init(channelInfo_t **channelinfo)
{
    *channelinfo = (channelInfo_t *) malloc(sizeof(channelInfo_t));
    if (NULL == channelinfo)
    {
        return RECORD_CHANNEL_INFO_ALLOCATE_MEMORY_ERR;
    }
    memset(*channelinfo, 0, sizeof(channelInfo_t));
    return RECORD_CHANNEL_INFO_OK;
}

const char *record_channelinfo_get_callid(const channelInfo_t *channelinfo)
{
    assert(channelinfo);
    return channelinfo->callid;
}

const char *record_channelinfo_get_from(const channelInfo_t *channelinfo)
{
    assert(channelinfo);
    return channelinfo->from;
}

const char *record_channelinfo_get_to(const channelInfo_t *channelinfo)
{
    assert(channelinfo);
    return channelinfo->to;
}

const char *record_channelinfo_get_caller_name(const channelInfo_t *channelinfo)
{
    assert(channelinfo);
    return channelinfo->callerName;
}

const char *record_channelinfo_get_callee_name(const channelInfo_t *channelinfo)
{
    assert(channelinfo);
    return channelinfo->calleeName;
}

u_int record_channelinfo_get_call_direction(const channelInfo_t *channelinfo)
{
    assert(channelinfo);
    return channelinfo->callDirection;
}

u_int record_channelinfo_get_current_state(const channelInfo_t *channelinfo)
{
    assert(channelinfo);
    return channelinfo->currentState;
}

u_int16_t record_channelinfo_get_audio_a_port(const channelInfo_t *channelinfo)
{
    assert(channelinfo);
    return channelinfo->endPointAAudioRtpPort;
}

u_int16_t record_channelinfo_get_audio_b_port(const channelInfo_t *channelinfo)
{
    assert(channelinfo);
    return channelinfo->endPointBAudioRtpPort;
}

void record_channelinfo_set_callid(channelInfo_t *channelinfo, const u_char *callid)
{
    assert(channelinfo);
    strcpy(channelinfo->callid, callid);
}

void record_channelinfo_set_from(channelInfo_t *channelinfo, const u_char *from)
{
    const u_char *ptmp_s = NULL;
    const u_char *ptmp_e = NULL;

    //from域格式为：[DisplayName] URI[;Tag]
    assert(channelinfo);
    ptmp_s = strchr(from, '<');
    if (NULL == ptmp_s) {
        strcpy(channelinfo->from, "null");
    }

    ptmp_e = strchr(ptmp_s, '>');
    if (NULL == ptmp_e) {
        strcpy(channelinfo->from, "null");
    }

    memset(channelinfo->from, 0, sizeof(channelinfo->from));
    strncpy(channelinfo->from, ptmp_s, ptmp_e - ptmp_s + 1);

    ptmp_s = strchr(ptmp_s, ':');
    if (NULL == ptmp_s) {
        strcpy(channelinfo->callerName, "null");
    }

    ptmp_e = strchr(ptmp_s, '@');
    if (NULL == ptmp_e) {
        strcpy(channelinfo->callerName, "null");
    }

    memset(channelinfo->callerName, 0, sizeof(channelinfo->callerName));
    strncpy(channelinfo->callerName, ptmp_s+1, ptmp_e - ptmp_s - 1);
}

void record_channelinfo_set_to(channelInfo_t *channelinfo, const u_char *to)
{
    const u_char *ptmp_s = NULL;
    const u_char *ptmp_e = NULL;

    //to域格式为：[DisplayName] URI[;Tag]
    assert(channelinfo);
    ptmp_s = strchr(to, '<');
    if (NULL == ptmp_s) {
        strcpy(channelinfo->to, "null");
    }

    ptmp_e = strchr(ptmp_s, '>');
    if (NULL == ptmp_e) {
        strcpy(channelinfo->to, "null");
    }

    memset(channelinfo->to, 0, sizeof(channelinfo->to));
    strncpy(channelinfo->to, ptmp_s, ptmp_e - ptmp_s + 1);

    ptmp_s = strchr(ptmp_s, ':');
    if (NULL == ptmp_s) {
        strcpy(channelinfo->calleeName, "null");
    }

    ptmp_e = strchr(ptmp_s, '@');
    if (NULL == ptmp_e) {
        strcpy(channelinfo->calleeName, "null");
    }

    memset(channelinfo->calleeName, 0, sizeof(channelinfo->calleeName));
    strncpy(channelinfo->calleeName, ptmp_s+1, ptmp_e - ptmp_s - 1);
}

void record_channelinfo_set_call_direction(channelInfo_t *channelinfo, callDirection_t direction)
{
    assert(channelinfo);
    channelinfo->callDirection = direction;
}

void record_channelinfo_set_current_state(channelInfo_t *channelinfo, channelState_t newState)
{
    assert(channelinfo);
    channelinfo->currentState = newState;
}

void record_channelinfo_set_audio_a_port(channelInfo_t *channelinfo, u_int16_t port)
{
    assert(channelinfo);
    channelinfo->endPointAAudioRtpPort = port;
}

void record_channelinfo_set_audio_b_port(channelInfo_t *channelinfo, u_int16_t port)
{
    assert(channelinfo);
    channelinfo->endPointBAudioRtpPort = port;
}

channelInfo_t *record_channelinfo_get_by_callid(const u_char *callid)
{
    channelInfo_t *ptmp = NULL;

    channelInfo_t *result = NULL;

    pthread_mutex_lock(&g_channelInfoList_mutex);
    ptmp = g_channelinfo_list;
    while (ptmp)
    {
        if (0 == strcmp(callid, ptmp->callid)) {
            result = ptmp;
            goto endLab;
        }
        ptmp = ptmp->next;
    }

    endLab:
    pthread_mutex_unlock(&g_channelInfoList_mutex);
    return result;
}

int record_channelinfo_add_new_channel(channelInfo_t *channelinfo)
{
    assert(channelinfo);

    pthread_mutex_lock(&g_channelInfoList_mutex);
    channelinfo->next = g_channelinfo_list;
    g_channelinfo_list = channelinfo;
    pthread_mutex_unlock(&g_channelInfoList_mutex);

    return RECORD_CHANNEL_INFO_OK;
}

int record_channelinfo_del_channel(const u_char *callid)
{
    channelInfo_t *ptmp = NULL;     //g_channelinfo_list;
    channelInfo_t *ptmp2 = NULL;    //g_channelinfo_list;
    int result = RECORD_CHANNEL_INFO_OK;

    pthread_mutex_lock(&g_channelInfoList_mutex);
    ptmp = g_channelinfo_list;
    ptmp2 = g_channelinfo_list;

    if (!ptmp) {
        result = RECORD_CHANNEL_INFO_EMPTY_LIST;
        goto endLab;
    }

    //第一个结点
    if (0 == strcmp(callid, ptmp->callid))
    {
        g_channelinfo_list = ptmp->next;
        free(ptmp);
        goto endLab;
    }

    ptmp = ptmp->next;
    while ( ptmp )
    {
        if (0 == strcmp(ptmp->callid, callid))
        {
            ptmp2->next = ptmp->next;
            ptmp->next = NULL;
            free(ptmp);
            goto endLab;
        }
        ptmp2 = ptmp;
        ptmp = ptmp->next;
    }

    endLab:
    pthread_mutex_unlock(&g_channelInfoList_mutex);
    return result;
}