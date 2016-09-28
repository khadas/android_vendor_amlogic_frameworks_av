/*
 * Copyright (C) 2012 The Android Open Source Project
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

//#define LOG_NDEBUG 0
#define LOG_TAG "MediaExtractorPlugin"
#include <utils/Log.h>


#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/AmMediaDefsExt.h>
#include <media/stagefright/MediaExtractor.h>
#include <media/stagefright/MetaData.h>
#include <utils/String8.h>

#include "include/ADIFExtractor.h"
#include "include/ADTSExtractor.h"
#include "include/LATMExtractor.h"
#include "include/AsfExtractor.h"
#include "include/DDPExtractor.h"
#include "include/THDExtractor.h"
#include "include/DtshdExtractor.h"
#include "include/AIFFExtractor.h"


namespace android {

#ifdef __cplusplus
extern "C" {
#endif

int am_registerAmExSniffs(void) {
	DataSource::RegisterSniffer(SniffADTS);
	DataSource::RegisterSniffer(SniffADIF);
	DataSource::RegisterSniffer(SniffLATM);
	DataSource::RegisterSniffer(SniffAsf);
	DataSource::RegisterSniffer(SniffAIFF);
	DataSource::RegisterSniffer(SniffTHD);
	DataSource::RegisterSniffer(SniffDDP);
	DataSource::RegisterSniffer(SniffDcahd);
	return 0;
}

MediaExtractor *am_createAmExExtractor(
        const android::sp<android::DataSource> &source, const char *mime, const sp<AMessage> &meta) {
	android::MediaExtractor *ret = NULL;	
	if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_AAC_ADIF)) {
		ret = new ADIFExtractor(source);
	} else if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_AAC_LATM)) {
		ret = new LATMExtractor(source);
	} else if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_ADTS_PROFILE)) {
		ret = new ADTSExtractor(source);
	} else if(!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_WMA)||!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_WMAPRO)){
		ret = new AsfExtractor(source);
	}else if(!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_DTSHD)){
		ret = new DtshdExtractor(source);
	} else if(!strcasecmp(mime, MEDIA_MIMETYPE_CONTAINER_AIFF)){
		ret = new AIFFExtractor(source);
	} else if(!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_TRUEHD)){
		ret = new THDExtractor(source);
	} else if (!strcasecmp(mime, MEDIA_MIMETYPE_CONTAINER_DDP)) {
		ret = new DDPExtractor(source);
	}
	return ret;
}

#ifdef __cplusplus
}  // extern "C"
#endif
}

