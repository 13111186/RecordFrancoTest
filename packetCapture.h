#ifndef PACKET_CAPTURE_H
#define PACKET_CAPTURE_H

#include "record-types.h"
#include <pcap/pcap.h>

typedef enum {
    RECORD_PACKET_CAPTURE_OK = 0,
    RECORD_PACKET_CAPTURE_ERR_INIT_REDIS,
    RECORD_PACKET_CAPTURE_ERR_GET_DEV_LIST,
    RECORD_PACKET_CAPTURE_ERR_LOOKUP_NET,
    RECORD_PACKET_CAPTURE_ERR_OPEN_DEVICE,
    RECORD_PACKET_CAPTURE_ERR_DATA_LINK_TYPE,
    RECORD_PACKET_CAPTURE_ERR_FILTER_COMPILE,
    RECORD_PACKET_CAPTURE_ERR_FILTER_SET,
} recordPacketParse_t;

int packetCapture_initCaptureEnv(RECORD_PARAM_OUT pcap_if_t **devList, RECORD_PARAM_OUT char *errBuf);
int packetCapture_selectDevice(RECORD_PARAM_IN pcap_if_t *devList, RECORD_PARAM_OUT char *selectDeviceName);
int packetCapture_openDevice(RECORD_PARAM_IN char *deviceNamem, RECORD_PARAM_IN const char *strFilter, RECORD_PARAM_OUT pcap_t **handle, RECORD_PARAM_OUT char *errBuf);
int packetCapture_startup(RECORD_PARAM_IN pcap_t *handle, RECORD_PARAM_IN u_char *userArg);

#endif