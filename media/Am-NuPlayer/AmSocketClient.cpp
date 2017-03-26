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

#include<pthread.h>
#include <binder/Binder.h>
#include <binder/IServiceManager.h>
#include <ISystemControlService.h>
#include <utils/Mutex.h>
#include <cutils/properties.h>

int mSockFd;

struct SendContext {
    char* buf;
    int size;
};

int socketConnect() {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    inet_aton("127.0.0.1", (struct in_addr *)&addr.sin_addr);
    addr.sin_port = htons(SERVER_PORT);
    mSockFd = socket(AF_INET, SOCK_STREAM, 0);
    /*struct sockaddr_un ser_addr;
    ser_addr.sun_family = AF_UNIX;
    strcpy (ser_addr.sun_path, "/dev/socket/sub_socket");
    mSockFd = socket(AF_UNIX, SOCK_STREAM, 0);*/
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

    ALOGI("%s:%d, connect socket success!\n", __FILE__, __LINE__);

    return 0;
}

void* socketSendThread(void* arg) {
    SendContext* sc = (SendContext*)arg;
    int sendLen = 0;
    int retLen = 0;
    int leftLen = sc->size;
    char *sendBuf = sc->buf;
    char recvBuf[32] = {0};
    //ALOGI("[socketSendThread]size[%d]\n", sc->size);

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
            ALOGE("%s:%d, send socket failed!retLen:%d\n", __FILE__, __LINE__, retLen);
            return NULL;
        }

        //check server reveived buffer length
        /*memset(recvBuf, 0, 32);
        recv(mSockFd, recvBuf, 32, 0);
        ALOGI("%s:%d, recvBuf:%s\n", __FILE__, __LINE__, recvBuf);*/

        //prepare left buffer pointer
        sendBuf += retLen;
        leftLen -= retLen;
    } while (leftLen > 0);
    return NULL;
}

void socketSend(char *buf, int size) {
    //ALOGI("[socketSend]buf:%s, size:%d\n", buf, size);
    char value[PROPERTY_VALUE_MAX] = {0};
    if (property_get("sys.amsocket.disable", value, "false") > 0) {
        if (!strcmp(value, "true")) {
            return;
        }
    }
    pthread_t st;
    SendContext sc;
    sc.buf = buf;
    sc.size = size;
    pthread_create(&st, NULL, socketSendThread, &sc);
    pthread_join(st, NULL);
}

int socketRecv(char *buf, int size) {
    int retlen = recv(mSockFd, buf, size, 0);
    if (retlen < 0) {
        if (errno == EINTR)
            retlen = 0;
        else {
            ALOGE("%s:%d, receive socket failed!", __FILE__, __LINE__);
            return -1;
        }
    }
    return retlen;
}

void socketDisconnect() {
    if (mSockFd >= 0) {
        close(mSockFd);
        mSockFd = -1;
    }
}

namespace android {

class DeathNotifier: public IBinder::DeathRecipient
{
    public:
        DeathNotifier()
        {
        } void binderDied(const wp < IBinder > &who)
        {
            ALOGW("system_write died!");
        }
};
static sp < ISystemControlService > amSystemControlService;
static sp < DeathNotifier > amDeathNotifier;
static Mutex amgLock;
const sp < ISystemControlService > &getSystemControlService() {
    Mutex::Autolock _l(amgLock);
    if (amSystemControlService.get() == 0) {
        sp < IServiceManager > sm = defaultServiceManager();
        sp < IBinder > binder;
        do {
            binder = sm->getService(String16("system_control"));
            if (binder != 0)
                break;
            ALOGW("SystemControl not published, waiting...");
            usleep(500000); // 0.5 s
        } while (true);
        if (amDeathNotifier == NULL) {
            amDeathNotifier = new DeathNotifier();
        }
        binder->linkToDeath(amDeathNotifier);
        amSystemControlService =
            interface_cast < ISystemControlService > (binder);
    }
    ALOGE_IF(amSystemControlService == 0, "no System Control Service!?");
    return amSystemControlService;
}

int writeSysfs(const char *path, char *value) {
    const sp < ISystemControlService > &scs = getSystemControlService();
    if (scs != 0) {
        String16 v(value);
        if (scs->writeSysfs(String16(path), v))
            return 0;
    }
    return -1;
}

int readSysfs(const char *path, char *value) {
    const sp < ISystemControlService > &scs = getSystemControlService();
    if (scs != 0) {
        String16 v;
        if (scs->readSysfs(String16(path), v)) {
            strcpy(value, String8(v).string());
            return 0;
        }
    }
    return -1;
}

void setPcrscr(int64_t timeUs) {
    char valStr[128] = "";
    char pcrStr[1024] = "";
    readSysfs("/sys/class/subtitle/startpts", valStr);
    int64_t val = strtol(valStr, NULL, 10);
    int64_t pcr = (timeUs/1000*90) + val;
    sprintf(pcrStr, "0x%x", pcr);
    writeSysfs("/sys/class/tsync/pts_pcrscr", pcrStr);
}
}
