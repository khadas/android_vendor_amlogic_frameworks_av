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
 * name : AmSubtitle.cpp
 * function : amlogic subtitle send managerment
 * data : 2017.04.20
 * author : wxl
 * version  : 1.0.0
 *************************************************/
#define LOG_NDEBUG 0
#define LOG_TAG "AmSubtitle"
#include <utils/Log.h>

#include "cutils/properties.h"
#include <inttypes.h>
#include <media/stagefright/AmMetaDataExt.h>

#include "AmSubtitle.h"

namespace android {

AmSubtitle::AmSubtitle() {
    mClient = NULL;
}

AmSubtitle::~AmSubtitle() {
    if (mClient != NULL) {
        mClient->socketSend((char *)"exit\n", 5);
        mClient->socketDisconnect();
        delete mClient;
        mClient = NULL;
    }
}

void AmSubtitle::init() {
    mDebug = false;
    char value[PROPERTY_VALUE_MAX] = {0};
    if (property_get("sys.amsubtitle.debug", value, "false") > 0) {
        if (!strcmp(value, "true")) {
            mDebug = true;
        }
    }

    if (mDebug) {
        ALOGI("[init]\n");
    }

    if (mClient == NULL) {
        mClient = new AmSocketClient();
        mClient->socketConnect();
    }
}

void AmSubtitle::exit() {
    if (mClient != NULL) {
        mClient->socketSend((char *)"exit\n", 5);
    }
}

void AmSubtitle::reset() {
    if (mClient != NULL) {
        mClient->socketSend((char *)"reset\n", 6);
    }
}

void AmSubtitle::setSubFlag(bool flag) {
    mIsAmSubtitle = flag;
}

bool AmSubtitle::getSubFlag() {
    if (mDebug) {
        ALOGI("[getSubFlag]mIsAmSubtitle:%d\n", (int)mIsAmSubtitle);
    }

    return mIsAmSubtitle;
}

void AmSubtitle::setStartPtsUpdateFlag(bool flag) {
    mStartPtsUpdate = flag;
}

bool AmSubtitle::getStartPtsUpdateFlag() {
    if (mDebug) {
        ALOGI("[getStartPtsUpdateFlag]mStartPtsUpdate:%d\n", (int)mStartPtsUpdate);
    }

    return mStartPtsUpdate;
}

void AmSubtitle::setTypeUpdateFlag(bool flag) {
    mTypeUpdate = flag;
}

bool AmSubtitle::getTypeUpdateFlag() {
    if (mDebug) {
        ALOGI("[getTypeUpdateFlag]mTypeUpdate:%d\n", (int)mTypeUpdate);
    }

    return mTypeUpdate;
}

void AmSubtitle::sendTime(int64_t timeUs) {
    if (mDebug) {
        ALOGI("[sendTime]timeUs:%" PRId64"\n", timeUs);
    }

    char buf[8] = {0x53, 0x52, 0x44, 0x54};//SRDT //subtitle render time
    buf[4] = (timeUs >> 24) & 0xff;
    buf[5] = (timeUs >> 16) & 0xff;
    buf[6] = (timeUs >> 8) & 0xff;
    buf[7] = timeUs & 0xff;
    if (mClient != NULL) {
        mClient->socketSend(buf, 8);
    }
}

void AmSubtitle::setSubTotal(int total) {
    if (mDebug) {
        ALOGI("[sendTime]total:%d\n", total);
    }

    char buf[8] = {0x53, 0x54, 0x4F, 0x54};//STOT
    buf[4] = (total >> 24) & 0xff;
    buf[5] = (total >> 16) & 0xff;
    buf[6] = (total >> 8) & 0xff;
    buf[7] = total & 0xff;
    if (mClient != NULL) {
        mClient->socketSend(buf, 8);
    }
}

void AmSubtitle::setSubStartPts(MediaBuffer *mbuf) {
    if (mbuf == NULL) {
        return;
    }

    if (!mStartPtsUpdate) {
        int64_t pts;
        mbuf->meta_data()->findInt64(kKeyPktFirstVPts, &pts);
        if (pts >= 0) {
            if (mDebug) {
                ALOGI("[setSubStartPts]pts:%" PRId64 "\n", pts);
            }

            mStartPtsUpdate = true;

            char buf[8] = {0x53, 0x50, 0x54, 0x53};//SPTS
            buf[4] = (pts >> 24) & 0xff;
            buf[5] = (pts >> 16) & 0xff;
            buf[6] = (pts >> 8) & 0xff;
            buf[7] = pts & 0xff;
            if (mClient != NULL) {
                mClient->socketSend(buf, 8);
            }
        }
    }
}

void AmSubtitle::setSubType(int type) {
    if (!mTypeUpdate) {
        mTypeUpdate = true;

        if (mDebug) {
            ALOGI("[setSubType]type:%d\n", type);
        }

        char buf[8] = {0x53, 0x54, 0x59, 0x50};//STYP
        buf[4] = (type >> 24) & 0xff;
        buf[5] = (type >> 16) & 0xff;
        buf[6] = (type >> 8) & 0xff;
        buf[7] = type & 0xff;
        if (mClient != NULL) {
            mClient->socketSend(buf, 8);
        }
    }
}

void AmSubtitle::sendToSubtitleService(MediaBuffer *mbuf) {
    if (mbuf == NULL) {
        return;
    }

    int32_t streamTimeBaseNum;
    int32_t streamTimeBaseDen;
    int64_t streamStartTime;
    int32_t streamCodecID;
    int32_t streamCodecTag;
    int32_t pktSize;
    int64_t pktPts;
    int64_t pktDts;
    int32_t pktDuration;
    int64_t pktConvergenceDuration;

    mbuf->meta_data()->findInt32(kKeyStreamTimeBaseNum, &streamTimeBaseNum);
    mbuf->meta_data()->findInt32(kKeyStreamTimeBaseDen, &streamTimeBaseDen);
    mbuf->meta_data()->findInt64(kKeyStreamStartTime, &streamStartTime);
    mbuf->meta_data()->findInt32(kKeyStreamCodecID, &streamCodecID);
    mbuf->meta_data()->findInt32(kKeyStreamCodecTag, &streamCodecTag);
    mbuf->meta_data()->findInt32(kKeyPktSize, &pktSize);
    mbuf->meta_data()->findInt64(kKeyPktPts, &pktPts);
    mbuf->meta_data()->findInt64(kKeyPktDts, &pktDts);
    mbuf->meta_data()->findInt32(kKeyPktDuration, &pktDuration);
    mbuf->meta_data()->findInt64(kKeyPktConvergenceDuration, &pktConvergenceDuration);

    if (mDebug) {
        ALOGI("[sendToSubtitleService]packet->size:%d, packet->pts:%" PRId64 "\n", pktSize, pktPts);
    }

    unsigned char sub_header[20] = {0x41, 0x4d, 0x4c, 0x55, 0x77, 0};
    unsigned int sub_type;
    float duration = 0;
    int64_t sub_pts = 0;
    int64_t start_time = 0;
    int data_size = pktSize;
    if (data_size <= 0) {
        if (mDebug) {
            ALOGE("[sendToSubtitleService]not enough data.data_size:%d\n", data_size);
        }
        return;
    }

    if (streamTimeBaseNum && (0 != streamTimeBaseDen)) {
        duration = PTS_FREQ * ((float)streamTimeBaseNum / streamTimeBaseDen);
        start_time = streamStartTime * streamTimeBaseNum * PTS_FREQ / streamTimeBaseDen;
        mLastDuration = 0;
    } else {
        start_time = streamStartTime * PTS_FREQ;
    }

    /* get pkt pts */
    if (0 != pktPts) {
        sub_pts = pktPts * duration;
        if (sub_pts < start_time) {
            sub_pts = sub_pts * mLastDuration;
        }
    } else if (0 != pktDts) {
        sub_pts = pktDts * duration * mLastDuration;
        mLastDuration = pktDuration;
    } else {
        sub_pts = 0;
    }

    sub_type = streamCodecID;
    if (sub_type == CODEC_ID_DVD_SUBTITLE) {
        setSubType(0);
    } else if (sub_type == CODEC_ID_HDMV_PGS_SUBTITLE) {
        setSubType(1);
    } else if (sub_type == CODEC_ID_XSUB) {
        setSubType(2);
    } else if (sub_type == CODEC_ID_TEXT
        || sub_type == CODEC_ID_SSA
        || sub_type == CODEC_ID_ASS) {
        setSubType(3);
    } else if (sub_type == CODEC_ID_DVB_SUBTITLE) {
        setSubType(5);
    } else if (sub_type == 0x17005) {
        setSubType(7);//SUBTITLE_TMD_TXT
    } else {
        setSubType(4);
    }

    if (mDebug) {
        ALOGE("[sendToSubtitleService]sub_type:0x%x, data_size:%d, sub_pts:%" PRId64 "\n", sub_type, data_size, sub_pts);
    }

    if (sub_type == 0x17000) {
        sub_type = 0x1700a;
    }
    else if (sub_type == 0x17002) {
        mLastDuration = (unsigned)pktConvergenceDuration * 90;
    }
    else if (sub_type == 0x17003) {
        char *buf = (char *)mbuf->data();
        sub_pts = str2ms(buf) * 90;

        // add flag for xsub to indicate alpha
        unsigned int codec_tag = streamCodecTag;
        if (codec_tag == MKTAG('D','X','S','A')) {
            sub_header[4] = sub_header[4] | 0x01;
        }
    }

    sub_header[5] = (sub_type >> 16) & 0xff;
    sub_header[6] = (sub_type >> 8) & 0xff;
    sub_header[7] = sub_type & 0xff;
    sub_header[8] = (data_size >> 24) & 0xff;
    sub_header[9] = (data_size >> 16) & 0xff;
    sub_header[10] = (data_size >> 8) & 0xff;
    sub_header[11] = data_size & 0xff;
    sub_header[12] = (sub_pts >> 24) & 0xff;
    sub_header[13] = (sub_pts >> 16) & 0xff;
    sub_header[14] = (sub_pts >> 8) & 0xff;
    sub_header[15] = sub_pts & 0xff;
    sub_header[16] = (mLastDuration >> 24) & 0xff;
    sub_header[17] = (mLastDuration >> 16) & 0xff;
    sub_header[18] = (mLastDuration >> 8) & 0xff;
    sub_header[19] = mLastDuration & 0xff;

    int size = 20 + data_size;
    char * data = (char *)malloc(size);
    memcpy(data, sub_header, 20);
    memcpy(data + 20, (char *)mbuf->data(), data_size);
    if (mClient != NULL) {
        if (mDebug) {
            for (int i = 0; i < size; i++) {
                ALOGE("[sendToSubtitleService]data[%d]:0x%x\n", i, data[i]);
            }
        }
        mClient->socketSend(data, size);
    }
    free(data);
}

}
