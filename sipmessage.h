#ifndef SIP_MESSAGE_H
#define SIP_MESSAGE_H

#include "sdp.h"

#include <sys/types.h>

typedef struct sip_message_  sip_message_t;

typedef enum MessageType_
{
    SIP_MT_REQUEST = 1,
    SIP_MT_RESPONSE,
    SIP_MT_REQUEST_INVITE,
    SIP_MT_REQUEST_BYE,
    SIP_MT_REQUEST_CANCEL,
    SIP_MT_REQUEST_TERMINATE,
    SIP_MT_RESPONSE_200,
    INVALID_SIP_MT
}SIPMessageType_t;

typedef enum {
    RECORD_SIP_MSG_PARSE_RESULT_OK,
    RECORD_SIP_MSG_PARSE_RESULT_SDP_INIT_ERR,
    RECORD_SIP_MSG_PARSE_RESULT_SDP_PARSE_ERR,
    RECORD_SIP_MSG_PARSE_RESULT_FORMAT_ERR,
    RECORD_SIP_MSG_PARSE_RESULT_ALLOCATE_MEMORY_ERR,
    RECORD_SIP_MSG_PARSE_RESULT_BED_VALUE_ERR
}SIPMessageParseResult_t;

int record_sip_message_init(sip_message_t **psip_message);
int record_sip_message_parse(sip_message_t *psip_msg, const u_char *psip_packet, int sip_packet_len);
void record_sip_message_free(sip_message_t *psip_message);
void record_sip_message_print(sip_message_t *psip_message);

const u_char *record_sip_message_get_callid(sip_message_t *psip_msg);
const u_char *record_sip_message_get_from(sip_message_t *psip_msg);
const u_char *record_sip_message_get_to(sip_message_t *psip_msg);
const u_char *record_sip_message_get_sip_method(sip_message_t *psip_msg);
SIPMessageType_t record_sip_message_get_message_type(sip_message_t *psip_msg);
int record_sip_message_get_status_code(sip_message_t *psip_msg);
const sdp_t *record_sip_message_get_sdp(sip_message_t *psip_msg);

#endif