#ifndef CHANNEL_INFO_H
#define CHANNEL_INFO_H

#include <sys/types.h>

typedef enum CallDirection_
{
    CD_INBOUND = 1,
    CD_OUTBOUND,
    CD_INVALID
} callDirection_t;

typedef enum ChannelState_
{
    CS_NONE	= 1,
    CS_INVITE,
    CS_ESTABLISHED,
    CS_BYE,
    CS_CANCEL,
    CS_INVALID_CHANNEL_STATE
} channelState_t;

typedef struct ChannelInfo_ channelInfo_t;

typedef enum {
    RECORD_CHANNEL_INFO_OK = 0,
    RECORD_CHANNEL_INFO_EMPTY_LIST,
    RECORD_CHANNEL_INFO_ALLOCATE_MEMORY_ERR
}ChannelInfoResult_t;

int record_channelinfo_init(channelInfo_t **channelinfo);
const char *record_channelinfo_get_callid(const channelInfo_t *channelinfo);
const char *record_channelinfo_get_from(const channelInfo_t *channelinfo);
const char *record_channelinfo_get_to(const channelInfo_t *channelinfo);
const char *record_channelinfo_get_caller_name(const channelInfo_t *channelinfo);
const char *record_channelinfo_get_callee_name(const channelInfo_t *channelinfo);
u_int record_channelinfo_get_call_direction(const channelInfo_t *channelinfo);
u_int record_channelinfo_get_current_state(const channelInfo_t *channelinfo);
u_int16_t record_channelinfo_get_audio_a_port(const channelInfo_t *channelinfo);
u_int16_t record_channelinfo_get_audio_b_port(const channelInfo_t *channelinfo);

void record_channelinfo_set_callid(channelInfo_t *channelinfo, const u_char *callid);
void record_channelinfo_set_from(channelInfo_t *channelinfo, const u_char *from);
void record_channelinfo_set_to(channelInfo_t *channelinfo, const u_char *to);
void record_channelinfo_set_call_direction(channelInfo_t *channelinfo, callDirection_t direction);
void record_channelinfo_set_current_state(channelInfo_t *channelinfo, channelState_t newState);
void record_channelinfo_set_audio_a_port(channelInfo_t *channelinfo, u_int16_t port);
void record_channelinfo_set_audio_b_port(channelInfo_t *channelinfo, u_int16_t port);

channelInfo_t *record_channelinfo_get_by_callid(const u_char *callid);
int record_channelinfo_add_new_channel(channelInfo_t *channelinfo);
int record_channelinfo_del_channel(const u_char *callid);

#endif