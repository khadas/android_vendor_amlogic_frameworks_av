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

#ifndef UDP_SOURCE_H_

#define UDP_SOURCE_H_

#include <stdio.h>

#include <media/stagefright/DataSource.h>
#include <media/stagefright/MediaErrors.h>
#include <utils/threads.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <cutils/properties.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>


namespace android {

class AmUDPSource : public DataSource {
public:
    AmUDPSource(const char *url);

    virtual status_t initCheck() const;

    virtual ssize_t readAt(off64_t offset, void *data, size_t size);

    virtual status_t getSize(off64_t *size);



protected:
    virtual ~AmUDPSource();

private:
    struct QueueEntry {
        sp<ABuffer> mBuffer;
        size_t mOffset;
        int32_t mBufferOrdinal;
        int64_t mBufferSize;
    };
    int mUdpFd;
    int mPort;
    struct sockaddr_in mDest_addr;  //struct sockaddr

    AString mURL;
    int mIsMulticast;

    List<QueueEntry> mBufQueue; // push_back(entry),empty()  (*mAudioQueue.begin())  mBufQueue.erase(mBufQueue.begin());
    int32_t mTotalBuffersQueued;
    int64_t mTotalBufferssize;


    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_t circular_buffer_thread;
    int request_exit;
    int thread_started;

    int mDebugLevel;
    FILE* mDumpFile;
    FILE* mReadFile;
    status_t mIsInit;
    void init( );
    int set_socket_nonblock(int socket, int enable);
    int is_multicast_address(struct sockaddr *addr);
    int udp_join_multicast_group(int sockfd, struct sockaddr *addr);
    int udp_leave_multicast_group(int sockfd, struct sockaddr *addr);
    void SplitURL(char *proto, int proto_size,
                  char *authorization, int authorization_size,
                  char *hostname, int hostname_size,
                  int *port_ptr, char *path, int path_size, const char *url);
    bool ParseSingleUnsignedLong(const char *from, unsigned long *x);
    static void *circular_buffer_task( void *arg);
    AmUDPSource(const AmUDPSource &);
    AmUDPSource &operator=(const AmUDPSource &);
};

}  // namespace android

#endif  // UDP_SOURCE_H_
