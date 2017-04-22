/*
 * Copyright (C) 2009 The Android Open Source Project
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

#define LOG_NDEBUG 0
#define LOG_TAG "NU-UDPSource"
#include "AmUDPSource.h"
#include <utils/Log.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/FileSource.h>

#include <string.h>
#include <sys/select.h>
#include <errno.h>


namespace android {
#define KSOCKETRECVBUFSIZE 256 * 1024


#define LOG_LEVEL(level,formats...)\
    do {\
        if (mDebugLevel >= level) {\
            ALOGI("UDP: " formats);}\
    } while(0)


#define UDP_LOGI(fmt...) LOG_LEVEL(0,##fmt)
#define UDP_LOGV(fmt...) LOG_LEVEL(2,##fmt)
#define UDP_LOGD(fmt...) LOG_LEVEL(4,##fmt)
#define UDP_LOGE(fmt...) ALOGE("UDP: " fmt)
#define UDP_MAX_PKT_SIZE 65536

AmUDPSource::AmUDPSource(const char *url)
    :mURL(url),
    mIsMulticast(false),
    mTotalBuffersQueued(0),
    mTotalBufferssize(0ll) {
    mIsInit = OK;
    mDebugLevel = 0;
    char value[PROPERTY_VALUE_MAX];
    int ret;
    if (property_get("media.udp.debug", value, NULL) > 0) {
        if ((sscanf(value, "%d", &ret)) > 0) {
            UDP_LOGE("media.udp.debug is set to %d\n", ret);
            mDebugLevel = ret;
        }
    }
    mDumpFile = NULL;
    mReadFile = NULL;
    if (property_get("media.udp.dump", value, NULL)
        && (!strcmp(value, "1") || !strcasecmp(value, "true"))) {
        mDumpFile= fopen("/data/tmp/udprevc.ts", "ab+");
        if (mDumpFile == NULL)
            UDP_LOGE("open /data/tmp/udprevc.ts failed");
        mReadFile = fopen("/data/tmp/udpread.ts", "ab+");
        if (mReadFile == NULL)
            UDP_LOGE("open /data/tmp/udpread.ts failed");
    }

    init();
}

/*static*/
void *AmUDPSource::circular_buffer_task( void *arg){
    AmUDPSource *source = (AmUDPSource *)arg;
    ALOGI("start [%s %d]", __FUNCTION__, __LINE__);
    int udpsize = -1;
    fd_set rfds;
    int ret;
    struct timeval tv;
    while (1) {
        int len;

        if (source->request_exit) {
            ALOGE( "[%s:%d]Eixt\n",__FUNCTION__,__LINE__);
            goto end;
        }
        FD_ZERO(&rfds);
        FD_SET(source->mUdpFd, &rfds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        ret = select(source->mUdpFd + 1, &rfds, NULL, NULL, &tv);
        if (ret < 0) {
           if (errno == EINTR)
               continue;
           ALOGE( "[%d]error select ",__LINE__);
            break;
        }
        sp<ABuffer> buffer = new ABuffer(udpsize > 0 ? udpsize : UDP_MAX_PKT_SIZE);
        len = recv(source->mUdpFd, buffer->data(), buffer->size(), 0);
        if (len > 0 && source->mDumpFile != NULL) {
            fwrite(buffer->data(), 1, len, source->mDumpFile);
            fflush(source->mDumpFile);
        }
        if (len < 0) {
            if (errno != EAGAIN && errno != EINTR) {
                ALOGE( "[%s:%d]error no $d\n",__FUNCTION__,__LINE__,errno);
                goto end;
            }
            ALOGE("len < 0");
            continue;
        }
        if (len == 0)
            continue;
        if (len > 0 && udpsize <= 0)
            udpsize = len;
        buffer->setRange(0,len);
        QueueEntry entry;
        entry.mBuffer = buffer;
        entry.mOffset = 0;
        pthread_mutex_lock(&source->mutex);
        if (source->mDebugLevel >= 4)
            ALOGI(">>>len %d bufferqueuedindex %d buffersize %lld",len,source->mTotalBuffersQueued,source->mTotalBufferssize);
        entry.mBufferOrdinal = ++source->mTotalBuffersQueued;
        source->mTotalBufferssize += len;
        entry.mBufferSize = source->mTotalBufferssize;
        source->mBufQueue.push_back(entry);
        pthread_mutex_unlock(&source->mutex);

        pthread_cond_signal(&source->cond);
    }
end:

    pthread_cond_signal(&source->cond);
    ALOGI("end [%s %d]", __FUNCTION__, __LINE__);
    return NULL;
}

void AmUDPSource::init() {
    char hostname[1024];   //  only ip address
    char proto[512];
    int recvbufsize =KSOCKETRECVBUFSIZE ;
    memset(&mDest_addr,0,sizeof(mDest_addr));
    bool isJoin = false;
    int reuse_socket = 1;
    int bind_ret = -1;
    SplitURL(NULL, 0, NULL, 0, hostname, sizeof(hostname), &mPort, NULL, 0, mURL.c_str());

    UDP_LOGI("hostname:%s port:%d",hostname,mPort);

    if (hostname[0] == '\0' || hostname[0] == '?') {

    } else {
        if (inet_aton(hostname,&mDest_addr.sin_addr)<0) {
            struct hostent *h = gethostbyname(hostname);
            memcpy(&mDest_addr.sin_addr, h->h_addr_list[0], sizeof(struct in_addr));
        }
        mDest_addr.sin_family = AF_INET;
        mDest_addr.sin_port = htons(mPort);
        mIsMulticast = is_multicast_address((struct sockaddr*) &mDest_addr);
        UDP_LOGI("mIsMulticast %d",mIsMulticast);
    }

    mUdpFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (mUdpFd < 0) {
        UDP_LOGE("socket create failed %s",strerror(errno));
        goto end;
    }

    if (setsockopt(mUdpFd, SOL_SOCKET, SO_RCVBUF, &recvbufsize, sizeof(recvbufsize)) < 0) {
        UDP_LOGE("getsockopt(SO_RCVBUF) %s",strerror(errno));
        goto end;
    }

    if (reuse_socket > 0 || (mIsMulticast && reuse_socket < 0)) {
        reuse_socket = 1;
        if (setsockopt (mUdpFd, SOL_SOCKET, SO_REUSEADDR, &(reuse_socket), sizeof(reuse_socket)) != 0)
            UDP_LOGI("getsockopt(SO_RCVBUF) %s",strerror(errno));
    }

    if (mIsMulticast) {
        bind_ret = bind(mUdpFd,(struct sockaddr *)&mDest_addr, sizeof(struct sockaddr));
        if (bind_ret < 0) {
            UDP_LOGE("mIsMulticast bind ret %s",strerror(errno));
            goto end;
        }
    }
    if (bind_ret < 0) {
        struct sockaddr_in my_addr;
        memset(&my_addr,0,sizeof(my_addr));
        my_addr.sin_family = AF_INET;
        my_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        my_addr.sin_port = htons(mPort);
        bind_ret = bind(mUdpFd,(struct sockaddr *)&my_addr, sizeof(struct sockaddr ));
        if (bind_ret < 0) {
            ALOGI("[%s %d] bind_ret %d %s", __FUNCTION__, __LINE__,bind_ret,strerror(errno));
            goto end;
        }
    }

    if (mIsMulticast) {
        ALOGI("[%s %d]", __FUNCTION__, __LINE__);
        isJoin = true;
        udp_join_multicast_group(mUdpFd,(struct sockaddr *)&mDest_addr);
    }

    set_socket_nonblock(mUdpFd,0);

    //init thread
    int ret;
    ret = pthread_mutex_init(&mutex, NULL);
    if (ret != 0) {
        UDP_LOGE("pthread_mutex_init failed : %s\n", strerror(ret));
    }

    ret = pthread_cond_init(&cond, NULL);
    if (ret != 0) {
        UDP_LOGE("pthread_cond_init failed : %s\n", strerror(ret));
    }

    request_exit = 0;
    ret = pthread_create(&circular_buffer_thread, NULL, circular_buffer_task, (void*)this);
    if (ret != 0) {
        UDP_LOGE("pthread_create failed : %s\n", strerror(ret));
        goto end;
    }
    pthread_setname_np(circular_buffer_thread, "udpsource");
    return;
end:

    if (mUdpFd > 0) {
        ::close(mUdpFd);
        mUdpFd = -1;
    }
    if (isJoin && mIsMulticast)
        udp_leave_multicast_group(mUdpFd,(struct sockaddr *)&mDest_addr);
    request_exit = 1;

    mIsInit = BAD_VALUE;
    ALOGI("init error[%s %d]", __FUNCTION__, __LINE__);
}

status_t AmUDPSource::initCheck() const{
    return mIsInit;
}



ssize_t AmUDPSource::readAt(off64_t offset, void *data, size_t size){
    if (size <= 0) return 0;

    size_t sizeDone = 0;
    while (sizeDone < size && request_exit != 1) {
        pthread_mutex_lock(&mutex);
#if 1
        while (mBufQueue.empty() && request_exit != 1 && sizeDone <= 188) {
            struct timeval now;
            struct timespec outtime;
            gettimeofday(&now, NULL);
            outtime.tv_sec = now.tv_sec;
            outtime.tv_nsec = now.tv_usec * 1000 + 100000*1000;//100ms
            pthread_mutex_unlock(&mutex);
            if (pthread_cond_timedwait(&cond, &mutex, &outtime) < 0) {
                UDP_LOGE("read timeout agin");
                continue;
            }
        }
#endif
        if (mBufQueue.empty()) {
            //ALOGI("mBufQueue is empty size %d sizeDone %d",size,sizeDone);
            pthread_mutex_unlock(&mutex);
            goto end;
        }
        QueueEntry entry = *mBufQueue.begin();
        if (0 && mTotalBufferssize - entry.mBufferSize > 7*4096*188) {//if buffsize > 7*4096*188 will drop an half of buf
            UDP_LOGE("drop some data %lld",(mTotalBufferssize - entry.mBufferSize)/2);
            while (1) {
                entry = *mBufQueue.begin();
                if (mTotalBufferssize - entry.mBufferSize > 7*4096*188 / 2 )
                    mBufQueue.erase(mBufQueue.begin());
                else
                    break;
            }
        }
        sp<ABuffer> buffer = entry.mBuffer;
        size_t copy = size - sizeDone;

        if (copy > buffer->size()) {
            copy = buffer->size();
        }
        if (entry.mOffset == 0)
            UDP_LOGD(">>>bufferqueuedindex %d buffersize %lld",mTotalBuffersQueued,mTotalBufferssize);

        memcpy((uint8_t *)data + sizeDone, buffer->data(), copy);

        sizeDone += copy;
        entry.mOffset += copy;
        buffer->setRange(buffer->offset() + copy, buffer->size() - copy);

        if (buffer->size() == 0) {
            mBufQueue.erase(mBufQueue.begin());
        }
        pthread_mutex_unlock(&mutex);
    }

end:
    if (mReadFile != NULL & sizeDone > 0) {
        fwrite(data, 1, sizeDone, mReadFile);
        fflush(mReadFile);
    }
    return sizeDone;

}

status_t AmUDPSource::getSize(off64_t *size){
    return -1;
}

AmUDPSource::~AmUDPSource(){
    ALOGI("[%s %d]", __FUNCTION__, __LINE__);
    request_exit = 1;
    pthread_cond_signal(&cond);
    pthread_join(circular_buffer_thread,NULL);
    if (mReadFile != NULL) {
        fclose(mReadFile);
        mReadFile = NULL;
    }
    if (mDumpFile!= NULL) {
        fclose(mDumpFile);
        mDumpFile = NULL;
    }

    if ( mIsMulticast )
        udp_leave_multicast_group(mUdpFd,(struct sockaddr *)&mDest_addr);
    if ( mUdpFd> 0 )
        ::close(mUdpFd);

    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond);
    if (!mBufQueue.empty()) {
        mBufQueue.erase(mBufQueue.begin());
    }
     ALOGI("[%s %d]", __FUNCTION__, __LINE__);
 }

bool AmUDPSource::ParseSingleUnsignedLong(
        const char *from, unsigned long *x) {
    char *end;
    *x = strtoul(from, &end, 10);

    if (end == from || *end != '\0') {
        return false;
    }

    return true;
}
#define MIN(a,b) ((a) > (b) ? (b) : (a))
static size_t strlcpy(char *dst, const char *src, size_t size)
{
    size_t len = 0;
    while (++len < size && *src)
        *dst++ = *src++;
    if (len <= size)
        *dst = 0;
    return len + strlen(src) - 1;
}

void AmUDPSource::SplitURL(char *proto, int proto_size,
                  char *authorization, int authorization_size,
                  char *hostname, int hostname_size,
                  int *port_ptr, char *path, int path_size, const char *url)
{
    const char *p, *ls, *ls2, *at, *at2, *col, *brk;

    if (port_ptr)
        *port_ptr = -1;
    if (proto_size > 0)
        proto[0] = 0;
    if (authorization_size > 0)
        authorization[0] = 0;
    if (hostname_size > 0)
        hostname[0] = 0;
    if (path_size > 0)
        path[0] = 0;

    /* parse protocol */
    if ((p = strchr(url, ':'))) {
        strlcpy(proto, url, MIN(proto_size, p + 1 - url));
        p++; /* skip ':' */
        if (*p == '/')
            p++;
        if (*p == '/')
            p++;
    } else {
        /* no protocol means plain filename */
        strlcpy(path, url, path_size);
        return;
    }

    /* separate path from hostname */
    ls = strchr(p, '/');
    ls2 = strchr(p, '?');
    if (!ls)
        ls = ls2;
    else if (ls && ls2)
        ls = MIN(ls, ls2);
    if (ls)
        strlcpy(path, ls, path_size);
    else
        ls = &p[strlen(p)];  // XXX

    /* the rest is hostname, use that to parse auth/port */
    if (ls != p) {
        /* authorization (user[:pass]@hostname) */
        at2 = p;
        while ((at = strchr(p, '@')) && at < ls) {
            strlcpy(authorization, at2,
                       MIN(authorization_size, at + 1 - at2));
            p = at + 1; /* skip '@' */
        }

        if (*p == '[' && (brk = strchr(p, ']')) && brk < ls) {
            /* [host]:port */
            strlcpy(hostname, p + 1,
                       MIN(hostname_size, brk - p));
            if (brk[1] == ':' && port_ptr)
                *port_ptr = atoi(brk + 2);
        } else if ((col = strchr(p, ':')) && col < ls) {
            strlcpy(hostname, p,
                       MIN(col + 1 - p, hostname_size));
            if (port_ptr)
                *port_ptr = atoi(col + 1);
        } else
            strlcpy(hostname, p,
                       MIN(ls + 1 - p, hostname_size));
    }
}


int AmUDPSource::set_socket_nonblock(int socket, int enable){
    if (enable)// 1->no block
        return fcntl(socket, F_SETFL, fcntl(socket, F_GETFL) | O_NONBLOCK);
    else// 0->block
        return fcntl(socket, F_SETFL, fcntl(socket, F_GETFL) & ~O_NONBLOCK);
}

int AmUDPSource::is_multicast_address(struct sockaddr *addr){
    if (addr->sa_family == AF_INET) {
        return ((((uint32_t)(ntohl(((struct sockaddr_in *)addr)->sin_addr.s_addr))) & 0xf0000000) == 0xe0000000);
    }
    if (addr->sa_family == AF_INET6) {
        return (((uint8_t *) (&((struct sockaddr_in6 *)addr)->sin6_addr))[0] == 0xff);
    }
    return 0;
}


int AmUDPSource::udp_join_multicast_group(int sockfd, struct sockaddr *addr){
    if (addr->sa_family == AF_INET) {
        struct ip_mreq mreq;
        mreq.imr_multiaddr.s_addr = ((struct sockaddr_in *)addr)->sin_addr.s_addr;
        mreq.imr_interface.s_addr= INADDR_ANY;
        if (setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const void *)&mreq, sizeof(mreq)) < 0) {
            ALOGI("setsockopt(IP_ADD_MEMBERSHIP): %s\n", strerror(errno));
            return -1;
        }
    }
    ALOGE("only ipv4");
    return -1;
}
int AmUDPSource::udp_leave_multicast_group(int sockfd, struct sockaddr *addr){
    if (addr->sa_family == AF_INET) {
        struct ip_mreq mreq;

        mreq.imr_multiaddr.s_addr = ((struct sockaddr_in *)addr)->sin_addr.s_addr;
        mreq.imr_interface.s_addr= htonl(INADDR_ANY);
        if (setsockopt(sockfd, IPPROTO_IP, IP_DROP_MEMBERSHIP, (const void *)&mreq, sizeof(mreq)) < 0) {
             ALOGI( "setsockopt(IP_DROP_MEMBERSHIP): %s\n", strerror(errno));
            return -1;
        }
    }
    ALOGE("only ipv4");
    return -1;
}


}  // namespace android

