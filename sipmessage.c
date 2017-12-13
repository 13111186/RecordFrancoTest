#include "sipmessage.h"
#include "record-types.h"
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

#define Filter_space(_p) while ((*_p) && ((' ' == *_p) || ('\t' == *_p))) _p += 1;

struct sip_message_
{
    char *request_uri;
    char *from;
    char *to;
    char *callid;
    char *via;
    char *contact;
    char *supported;
    char *content_type;
    char *allow;
    char *user_agent;
    char *sip_method;
    sdp_t *sdp;
    int cseq;
    int max_forwards;
    int status_code;
    int content_length;
    SIPMessageType_t sip_message_type;
};

static int record_sip_message_startline_request_parse(sip_message_t *psip_msg, const u_char *psip_packet, const u_char **next_header);
static int record_sip_message_startline_response_parse(sip_message_t *psip_msg, const u_char *psip_packet, const u_char **next_header);
static int record_sip_message_header_parse(sip_message_t *psip_msg, const u_char *start_of_header, const u_char **body);
static int record_sip_message_find_next_crlf(const u_char *start_of_header, const u_char **end_of_header);
static void record_sip_message_free_header(sip_message_t *psip_msg);
static int record_sip_message_fill_field(sip_message_t *psip_msg, const u_char *pname, int name_len, const void *pvalue);

int record_sip_message_init(sip_message_t **psip_message)
{
    int result = 0;
    *psip_message = NULL;
    *psip_message = (sip_message_t *)malloc(sizeof(sip_message_t));
    if (NULL != *psip_message)
    {
        memset(*psip_message, 0, sizeof(sip_message_t));
        result = record_sdp_init(&((*psip_message)->sdp));

        if (RECORD_SDP_MSG_PARSE_RESULT_OK != result)
        {
            free(*psip_message);
            return RECORD_SIP_MSG_PARSE_RESULT_SDP_INIT_ERR;
        }

        return RECORD_SIP_MSG_PARSE_RESULT_OK;
    }
    else
    {
        return RECORD_SIP_MSG_PARSE_RESULT_ALLOCATE_MEMORY_ERR;
    }
}

int record_sip_message_parse(sip_message_t *psip_msg, const u_char *psip_packet, int sip_packet_len)
{
    const u_char *p1 = psip_packet;
    const u_char *p2 = p1;
    const u_char *next_header = NULL;
    const u_char *body = NULL;
    int result = RECORD_SIP_MSG_PARSE_RESULT_OK;

    //滤除空白符
    while ((*p1) && ((' ' == *p1) || ('\t' == *p1 )))
        p1++;

    if (0 == strncasecmp(p1, RESPONSE_HEADER, strlen(RESPONSE_HEADER)))
    {
        //响应消息解析
        psip_msg->sip_message_type = SIP_MT_RESPONSE;
        result = record_sip_message_startline_response_parse(psip_msg, p1, &next_header);
    }
    else
    {
        //请求消息解析
        psip_msg->sip_message_type = SIP_MT_REQUEST;
        result = record_sip_message_startline_request_parse(psip_msg, p1, &next_header);
    }

    if (RECORD_SIP_MSG_PARSE_RESULT_OK != result)
        return result;

    result = record_sip_message_header_parse(psip_msg, next_header, &body);
    if (RECORD_SIP_MSG_PARSE_RESULT_OK == result && 0 != psip_msg->content_length)
    {
        result = record_sdp_parse(psip_msg->sdp, body, psip_msg->content_length);
        if (RECORD_SDP_MSG_PARSE_RESULT_OK != result) {
            result = RECORD_SIP_MSG_PARSE_RESULT_SDP_PARSE_ERR;
        }
    }
    return result;
}

const u_char *record_sip_message_get_callid(sip_message_t *psip_msg)
{
    assert(psip_msg);
    return psip_msg->callid;
}

const u_char *record_sip_message_get_from(sip_message_t *psip_msg)
{
    assert(psip_msg);
    return psip_msg->from;
}

const u_char *record_sip_message_get_to(sip_message_t *psip_msg)
{
    assert(psip_msg);
    return psip_msg->to;
}

const u_char *record_sip_message_get_sip_method(sip_message_t *psip_msg)
{
    assert(psip_msg);
    return psip_msg->sip_method;
}

const sdp_t *record_sip_message_get_sdp(sip_message_t *psip_msg)
{
    assert(psip_msg);
    return psip_msg->sdp;
}

SIPMessageType_t record_sip_message_get_message_type(sip_message_t *psip_msg)
{
    assert(psip_msg);
    return psip_msg->sip_message_type;
}

int record_sip_message_get_status_code(sip_message_t *psip_msg)
{
    assert(psip_msg);
    return psip_msg->status_code;
}

void record_sip_message_free(sip_message_t *psip_msg)
{
    assert(psip_msg);

    record_sip_message_free_header(psip_msg);

    if (NULL != psip_msg->sdp)
    {
        record_sdp_free(psip_msg->sdp);
        psip_msg->sdp = NULL;
    }
    free(psip_msg);
}

static int record_sip_message_header_parse(sip_message_t *psip_msg, const u_char *start_of_header, const u_char **body)
{
    const u_char *soh = start_of_header;
    const u_char *end_of_header = NULL;
    const u_char *pcolon = NULL;
    int result = RECORD_SIP_MSG_PARSE_RESULT_OK;

    while (!RECORD_SIP_MSG_PARSE_RESULT_OK)
    {
        if (!(*soh))
            return RECORD_SIP_MSG_PARSE_RESULT_OK;

        Filter_space(soh)

        result = record_sip_message_find_next_crlf(soh, &end_of_header);
        if (result)
            return result;

        // body 与 header之间使用CRLF 或 CRCR 或 LFLF分割
        if ( (('\r' == soh[0]) && ('\n' == soh[1]))
             || (('\r' == soh[0]) && ('\r' == soh[1]))
             || (('\n' == soh[0]) && ('\n' == soh[1]))
                )
        {
            *body = soh + 2;
            return RECORD_SIP_MSG_PARSE_RESULT_OK;
        }

        pcolon = strchr(soh, ':');
        if ( (NULL == pcolon)
             || (end_of_header <= pcolon)
             || (pcolon - soh < 2)   // To，长度是2
                )
        {
            return RECORD_SIP_MSG_PARSE_RESULT_FORMAT_ERR;
        }

        result = record_sip_message_fill_field(psip_msg, soh, pcolon - soh, pcolon + 1);
        soh = end_of_header;

    }

    return result;
}

static int record_sip_message_startline_response_parse(sip_message_t *psip_msg, const u_char *psip_packet, const u_char **next_header)
{
    const u_char *status_code = NULL;
    const u_char *tmpP = NULL;

    psip_msg->request_uri = NULL;
    psip_msg->sip_method = NULL;

    tmpP = psip_packet;
    while ((*tmpP) && (' ' == *tmpP || '\t' == *tmpP))
        tmpP += 1;

    //Response start line "SIP/2.0 StatusCode ReasonParse"
    status_code = strchr(tmpP, ' ');
    if (NULL == status_code)
        return RECORD_SIP_MSG_PARSE_RESULT_FORMAT_ERR;

    psip_msg->status_code = 0;

    status_code += 1;
    while (*status_code >= '0' && *status_code <= '9')
    {
        psip_msg->status_code *= 10;
        psip_msg->status_code += (*status_code - '0');
        status_code += 1;
    }

    if (0 == psip_msg->status_code)
        return RECORD_SIP_MSG_PARSE_RESULT_BED_VALUE_ERR;
    tmpP = status_code;
    while ((*tmpP) && '\r' != *tmpP && '\n' != *tmpP)
        tmpP += 1;
    tmpP += 1;
    if ((*tmpP) && '\r' == tmpP[-1] && '\n' == tmpP[0])
    {
        *next_header = tmpP + 1;
        return RECORD_SIP_MSG_PARSE_RESULT_OK;
    }
    else
        return RECORD_SIP_MSG_PARSE_RESULT_FORMAT_ERR;
}

static int record_sip_message_startline_request_parse(sip_message_t *psip_msg, const u_char *psip_packet, const u_char **next_header)
{
    //Request Start Line "Method Request-URI Version CRLF"
    char *method = NULL;
    char *request_uri = NULL;
    const char *ptmp = psip_packet;
    const char *ptmp1 = ptmp;

    while ((*ptmp1) && ((' ' == *ptmp1)) || ('\t' == *ptmp1))
        ptmp1 += 1;

    //method
    ptmp = strchr(ptmp1, ' ');
    if (NULL == ptmp)
        return RECORD_SIP_MSG_PARSE_RESULT_FORMAT_ERR;
    method = (char *)malloc(ptmp - ptmp1 + 1);
    if (NULL == method)
        return RECORD_SIP_MSG_PARSE_RESULT_ALLOCATE_MEMORY_ERR;

    strncpy(method, ptmp1, ptmp - ptmp1);
    method[ptmp-ptmp1] = 0;

    ptmp1 = ptmp + 1;
    while ((*ptmp1) && ((' ' == *ptmp1) || ('\t' == *ptmp1)))
        ptmp1 += 1;

    //request-uri
    ptmp = strchr(ptmp1, ' ');
    if (NULL == ptmp)
    {
        free(method);
        return RECORD_SIP_MSG_PARSE_RESULT_FORMAT_ERR;
    }
    request_uri = (char *)malloc(ptmp - ptmp1 + 1);
    if (NULL == request_uri)
    {
        free(method);
        return RECORD_SIP_MSG_PARSE_RESULT_ALLOCATE_MEMORY_ERR;
    }
    strncpy(request_uri, ptmp1, ptmp - ptmp1);
    request_uri[ptmp-ptmp1] = 0;

    ptmp1 = ptmp + 1;
    Filter_space(ptmp1)

    while ((*ptmp) && ('\r' != *ptmp) && ('\n' != *ptmp))
        ptmp += 1;

    if ((*ptmp) && ('\r' == ptmp[0]) && ('\n' == ptmp[1]))
    {
        *next_header = ptmp + 2;
        psip_msg->sip_method = method;
        psip_msg->request_uri = request_uri;
        return RECORD_SIP_MSG_PARSE_RESULT_OK;
    }
    else
    {
        free(method);
        free(request_uri);
        return RECORD_SIP_MSG_PARSE_RESULT_FORMAT_ERR;
    }
}

static int record_sip_message_find_next_crlf(const u_char *start_of_header, const u_char **end_of_header)
{
    const u_char *soh = start_of_header;

    while (*soh && '\r' != *soh && '\n' != *soh)
        soh += 1;

    if (!(*soh))
        return RECORD_SIP_MSG_PARSE_RESULT_FORMAT_ERR;

    if ( ('\r' == soh[0]) && ('\n' == soh[1]) )
        soh += 1;

    *end_of_header = soh + 1;

    return RECORD_SIP_MSG_PARSE_RESULT_OK;
}

static void record_sip_message_free_header(sip_message_t *psip_msg)
{
    assert(psip_msg);

    if (NULL != psip_msg->request_uri)
    {
        free(psip_msg->request_uri);
        psip_msg->request_uri = NULL;
    }
    if (NULL != psip_msg->from)
    {
        free(psip_msg->from);
        psip_msg->from = NULL;
    }
    if (NULL != psip_msg->to)
    {
        free(psip_msg->to);
        psip_msg->to = NULL;
    }
    if (NULL != psip_msg->callid)
    {
        free(psip_msg->callid);
        psip_msg->callid = NULL;
    }
    if (NULL != psip_msg->via)
    {
        free(psip_msg->via);
        psip_msg->via = NULL;
    }
    if (NULL != psip_msg->contact)
    {
        free(psip_msg->contact);
        psip_msg->contact = NULL;
    }
    if (NULL != psip_msg->supported)
    {
        free(psip_msg->supported);
        psip_msg->supported = NULL;
    }
    if (NULL != psip_msg->content_type)
    {
        free(psip_msg->content_type);
        psip_msg->content_type = NULL;
    }
    if (NULL != psip_msg->allow)
    {
        free(psip_msg->allow);
        psip_msg->allow = NULL;
    }
    if (NULL != psip_msg->user_agent)
    {
        free(psip_msg->user_agent);
        psip_msg->user_agent = NULL;
    }
    if (NULL != psip_msg->sip_method)
    {
        free(psip_msg->sip_method);
        psip_msg->sip_method = NULL;
    }
}

static int record_sip_message_fill_field(sip_message_t *psip_msg, const u_char *pname, int name_len, const void *pvalue)
{
    int value_len = 0;
    const u_char *pvalue_end = (const char *)pvalue;
    const u_char *pvalue_start = (const char *)pvalue;
    u_char *buf = NULL;

    Filter_space(pvalue_end);
    pvalue_start = pvalue_end;
    while (*pvalue_end && ('\r' != *pvalue_end) && ('\n' != *pvalue_end) )
    {
        pvalue_end += 1;
        value_len += 1;
    }

    buf = (u_char *)malloc(value_len + 1);
    if (NULL == buf)
    {
        return RECORD_SIP_MSG_PARSE_RESULT_ALLOCATE_MEMORY_ERR;
    }
    memcpy(buf, pvalue_start, value_len);
    buf[value_len] = 0;
    if (!strncasecmp("Call-ID", pname, 6))
    {
        psip_msg->callid = buf;
    } else if (!strncasecmp("From", pname, 4)){
        psip_msg->from = buf;
    } else if (!strncasecmp("To", pname, 2)){
        psip_msg->to = buf;
    } else if (!strncasecmp("Cseq", pname, 4)){
        psip_msg->cseq = atoi(buf);
        if (SIP_MT_RESPONSE == psip_msg->sip_message_type) {
            //响应消息，提取METHOD
            u_char *ptmp = strchr(buf, ' ');
            u_char *method_buf = NULL;
            if (NULL != ptmp) {

                Filter_space(ptmp)

                method_buf = (u_char *)malloc(value_len - (ptmp - buf) + 1);
                if (NULL != method_buf) {
                    strcpy(method_buf, ptmp);
                    psip_msg->sip_method = method_buf;
                }
            }
        }
        free(buf);
        buf = NULL;
    } else if (!strncasecmp("Via", pname, 3)){
        psip_msg->via = buf;
    } else if (!strncasecmp("Contact", pname, 7)){
        psip_msg->contact = buf;
    } else if (!strncasecmp("Max-Forwards", pname, 12)){
        psip_msg->max_forwards = atoi(buf);
        free(buf);
        buf = NULL;
    } else if (!strncasecmp("Allow", pname, 5)){
        psip_msg->allow = buf;
    } else if (!strncasecmp("Content-Length", pname, 14)){
        psip_msg->content_length = atoi(buf);
        free(buf);
        buf = NULL;
    } else if (!strncasecmp("Supported", pname, 9)){
        psip_msg->supported = buf;
    } else if (!strncasecmp("User-Agent", pname, 10)){
        psip_msg->user_agent = buf;
    } else if (!strncasecmp("Content-Type", pname, 12)){
        psip_msg->content_type = buf;
    } else {
        //用户自定义字段，But I Don't Care!!
        free(buf);
        buf = NULL;
    }
    return RECORD_SIP_MSG_PARSE_RESULT_OK;
}

void record_sip_message_print(sip_message_t *psip_message)
{
/*
    struct sip_message_
    {
        char *request_uri;
        char *from;
        char *to;
        char *callid;
        char *via;
        char *contact;
        char *supported;
        char *content_type;
        char *allow;
        char *user_agent;
        char *sip_method;
        sdp_t *sdp;
        int cseq;
        int max_forwards;
        int status_code;
        int content_length;
        SIPMessageType_t sip_message_type;    
    };
*/
    printf("\t\t================SIP Info================\t\t\n");
    printf("\tRequest-URI:%s\n", psip_message->request_uri);
    printf("\tFrom:%s\t\n", psip_message->from);
    printf("\tTrom:%s\t\n", psip_message->to);
    printf("\tCall-ID:%s\t\n", psip_message->callid);
    printf("\tVia:%s\t\n", psip_message->via);
    printf("\tContact:%s\t\n", psip_message->contact);
    printf("\tSupported:%s\t\n", psip_message->supported);
    printf("\tContent-Type:%s\t\n", psip_message->content_type);
    printf("\tAllow:%s\t\n", psip_message->allow);
    printf("\tUser-Agent:%s\t\n", psip_message->user_agent);
    printf("\tSip-Method:%s\t\n", psip_message->sip_method);
    printf("\tCSeq:%d\t\n", psip_message->cseq);
    printf("\tMax-Forwards:%d\t\n", psip_message->max_forwards);
    printf("\tStatus-Code:%d\t\n", psip_message->status_code);
    printf("\tContent-Length:%d\t\n", psip_message->content_length);
    printf("\t\t========================================\t\t\n");
    record_sdp_print(psip_message->sdp);
}