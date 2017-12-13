#ifndef SDP_H
#define SDP_H

#include <sys/types.h>

typedef struct sdp_ sdp_t;

typedef enum {
    RECORD_SDP_MSG_PARSE_RESULT_OK,
    RECORD_SDP_MSG_PARSE_RESULT_FORMAT_ERR,
    RECORD_SDP_MSG_PARSE_RESULT_ALLOCATE_MEMORY_ERR,
    RECORD_SDP_MSG_PARSE_RESULT_BED_VALUE_ERR
}SDPMessageParseResult_t;

int record_sdp_init(sdp_t **sdp);
int record_sdp_parse(sdp_t *psdp, const u_char *sdp_packet, int sdp_packet_length);
int record_sdp_get_audio_port(const sdp_t *psdp);
int record_sdp_get_payload_type(const sdp_t *psdp);
int record_sdp_get_sample_rate(const sdp_t *psdp);
void record_sdp_free(sdp_t *sdp);
void record_sdp_print(const sdp_t *sdp);
#endif