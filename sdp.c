#include "sdp.h"
#include "record-types.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

struct sdp_
{
    u_int16_t audio_port;
    u_char payload_type;
    u_int32_t sample_rate;
};

int record_sdp_parse_media(sdp_t *psdp, const u_char *pmedia);

int record_sdp_init(sdp_t **sdp)
{
    *sdp = NULL;
    *sdp = (sdp_t *)malloc(sizeof(sdp_t));

    if (NULL != *sdp)
    {
        memset(*sdp, 0, sizeof(sdp_t));
        return RECORD_SDP_MSG_PARSE_RESULT_OK;
    }
    else
    {
        return RECORD_SDP_MSG_PARSE_RESULT_ALLOCATE_MEMORY_ERR;
    }
}

int record_sdp_parse(sdp_t *psdp, const u_char *sdp_packet, int sdp_packet_length)
{
    const char *p = NULL;
    p = strstr(sdp_packet, "m=audio");
    if (NULL == p )
    {
        return RECORD_SDP_MSG_PARSE_RESULT_FORMAT_ERR;
    }
    else
    {
        return record_sdp_parse_media(psdp, p);
    }
}

int record_sdp_get_audio_port(const sdp_t *psdp)
{
    assert(psdp);

    return psdp->audio_port;
}

int record_sdp_get_payload_type(const sdp_t *psdp)
{
    assert(psdp);

    return psdp->payload_type;
}

int record_sdp_get_sample_rate(const sdp_t *psdp)
{
    assert(psdp);

    return psdp->sample_rate;
}

void record_sdp_free(sdp_t *psdp)
{
    assert(psdp);
    free(psdp);
}

int record_sdp_parse_media(sdp_t *psdp, const u_char *pmedia)
{
    const char *p = pmedia + strlen("m=audio");
    int audio_port = 0;
    int payload_type = 0;

    //滤掉端口号前面的空格
    while (*p && (' ' == *p || '\t' == *p))
        p += 1;

    //提取端口
    while (*p >= '0' && *p <= '9')
    {
        audio_port *= 10;
        audio_port += (*p - '0');
        p += 1;
    }

    //滤掉media中的proto前的空格 和 proto
    //滤掉端口号前面的空格
    while ((' ' == *p) || (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || ('/' == *p))
        p += 1;
    //提取第一个payload type
    while (*p >= '0' && *p <= '9')
    {
        payload_type *= 10;
        payload_type += (*p - '0');
        p += 1;
    }

    if (0 != audio_port && 0 != payload_type)
    {
        // fprintf(stdout, "File [%s] Line [%d], get audio_port: [%d] payload_type: [%d]\n", __FILE__, __LINE__, audio_port, payload_type);
        psdp->audio_port = audio_port;
        psdp->payload_type = payload_type;
        psdp->sample_rate = 8000;
        return RECORD_SDP_MSG_PARSE_RESULT_OK;
    }
    else
    {
        return RECORD_SDP_MSG_PARSE_RESULT_FORMAT_ERR;
    }
}

void record_sdp_print(const sdp_t *psdp)
{
    /*
        u_int16_t audio_port;
        u_char payload_type;
        u_int32_t sample_rate;
    */
    printf("============Simple SDP Info============\n");
    printf("+\taudio port:%05d\t+\n", psdp->audio_port);
    printf("+\tpayload type:%04d\t+\n", psdp->payload_type);
    printf("+\tsample_rate:%06d\t+\n", psdp->sample_rate);
    printf("======================================\n");
}