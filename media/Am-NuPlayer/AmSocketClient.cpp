/*
 * Copyright (C) 2010 Amlogic Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */



/************************************************
 * name : AmSocket.cpp
 * function : transfer data by socket
 * data : 2017.03.02
 * author : wxl
 * version  : 1.0.0
 *************************************************/
#define LOG_NDEBUG 0
#define LOG_TAG "AmSocket"
#include <utils/Log.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <unistd.h>

#include "AmSocketClient.h"
#include "cutils/properties.h"

namespace android {

AmSocketClient::AmSocketClient() {
    mSockFd = 0;
}

AmSocketClient::~AmSocketClient() {
    mSockFd = 0;
}

int AmSocketClient::socketConnect() {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    inet_aton("127.0.0.1", (struct in_addr *)&addr.sin_addr);
    addr.sin_port = htons(SERVER_PORT);
    mSockFd = socket(AF_INET, SOCK_STREAM, 0);
    if (mSockFd < 0) {
        ALOGE("%s:%d, create socket failed!mSockFd:%d, error=%d, err:%s\n", __FILE__, __LINE__, mSockFd, errno, strerror(errno));
        return -1;
    }
    ALOGI("%s:%d, create socket success!mSockFd:%d\n", __FILE__, __LINE__, mSockFd);
    if (connect(mSockFd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ALOGE("%s:%d, connect socket failed!, error=%d, err:%s\n", __FILE__, __LINE__, errno, strerror(errno));
        close(mSockFd);
        return -1;
    }

    ALOGI("%s:%d, connect socket success!mSockFd:%d\n", __FILE__, __LINE__, mSockFd);

    return 0;
}

void AmSocketClient::socketSend(char *buf, int size) {
    //ALOGI("[socketSend]buf:%s, size:%d\n", buf, size);
    char value[PROPERTY_VALUE_MAX] = {0};
    if (property_get("sys.amsocket.disable", value, "false") > 0) {
        if (!strcmp(value, "true")) {
            return;
        }
    }

    int sendLen = 0;
    int retLen = 0;
    int leftLen = size;
    char *sendBuf = buf;
    char recvBuf[32] = {0};

    if (mSockFd > 0) {
        do {
            //prepare send length
            if (leftLen > SEND_LEN) {
                sendLen = SEND_LEN;
            }
            else {
                sendLen = leftLen;
            }

            //start to send
            retLen = send(mSockFd, sendBuf, sendLen, 0);
            if (retLen < 0) {
                if (errno == EINTR) {
                    retLen = 0;
                }
                //ALOGE("%s:%d, send socket failed!retLen:%d\n", __FILE__, __LINE__, retLen);
                return;
            }

            //prepare left buffer pointer
            sendBuf += retLen;
            leftLen -= retLen;
        } while (leftLen > 0);
    }
}

int AmSocketClient::socketRecv(char *buf, int size) {
    int retlen = 0;
    if (mSockFd > 0) {
        retlen = recv(mSockFd, buf, size, 0);
        if (retlen < 0) {
            if (errno == EINTR)
                retlen = 0;
            else {
                ALOGE("%s:%d, receive socket failed!", __FILE__, __LINE__);
                return -1;
            }
        }
    }

    return retlen;
}

void AmSocketClient::socketDisconnect() {
    if (mSockFd > 0) {
        close(mSockFd);
        mSockFd = -1;
    }
}

}
