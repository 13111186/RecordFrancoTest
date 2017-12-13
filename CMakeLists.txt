cmake_minimum_required(VERSION 3.8)
project(RecordFranco)

include_directories(/usr/include)
find_library(libpcap libpcap /usr/local/lib/)

set(SOURCE_FILES
        main.c
        channelinfo.c
        channelinfo.h
        record-types.h
        record-util-queue.h
        fileHandle.h
        packetCapture.h
        packetParse.c
        packetParse.h
        sdp.h
        sipmessage.h
        record-util-queue.c
        fileHandle.c
        packetCapture.c
        sdp.c
        sipmessage.c
        route.c
        route.h
        threadCondition.h)

add_executable(RecordFranco ${SOURCE_FILES})

target_link_libraries(RecordFranco
        /usr/local/lib/libpcap.a
        /usr/local/lib/libpcap.so
        /usr/local/lib/libhiredis.so
        /usr/local/lib/libhiredis.a
        pthread
        )