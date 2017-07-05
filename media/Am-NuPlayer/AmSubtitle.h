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



#ifndef AM_SUBTITLE_H
#define AM_SUBTITLE_H

#include "AmSocketClient.h"

#include <media/stagefright/MediaBuffer.h>
#include <media/stagefright/MetaData.h>
#include <mediainfo.h>

namespace android {
//------------------------------------------------------------------------------
#define UNIT_FREQ   96000
#define PTS_FREQ    90000
#define str2ms(s) (((s[1]-0x30)*3600*10 \
                +(s[2]-0x30)*3600 \
                +(s[4]-0x30)*60*10 \
                +(s[5]-0x30)*60 \
                +(s[7]-0x30)*10 \
                +(s[8]-0x30))*1000 \
                +(s[10]-0x30)*100 \
                +(s[11]-0x30)*10 \
                +(s[12]-0x30))

class AmSubtitle
{
public:
    AmSubtitle();
    ~AmSubtitle();
    void init();
    void reset();
    void exit();
    void setSubFlag(bool flag);
    bool getSubFlag();
    void setStartPtsUpdateFlag(bool flag);
    bool getStartPtsUpdateFlag();
    void setTypeUpdateFlag(bool flag);
    bool getTypeUpdateFlag();
    void sendTime(int64_t timeUs);
    void setSubType(int type);
    void setSubStartPts(MediaBuffer *mbuf);
    void setSubTotal(int total);
    void sendToSubtitleService(MediaBuffer *mbuf);

private:
    bool mDebug;

    AmSocketClient *mClient;
    int mLastDuration;
    bool mIsAmSubtitle;
    bool mStartPtsUpdate;
    bool mTypeUpdate;
};
//------------------------------------------------------------------------------
}
#endif