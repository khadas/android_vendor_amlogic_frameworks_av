/*
 * Copyright (C) 2010 The Android Open Source Project
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

#define LOG_TAG "SStreamingExtractor"
#include <utils/Log.h>
#include <dlfcn.h>

#include <arpa/inet.h>
#include <utils/String8.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/Utils.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/MediaSource.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MediaBuffer.h>

#include "AmlogicPlayerExtractorDemux.h"

#include "SStreamingExtractor.h"



void *gSSLibHandle = NULL;


namespace android {

static Mutex gSSMutex;


const char *PR_MIME_TYPE = "application/vnd.ms-playready";


SStreamingExtractor::SStreamingExtractor(const sp<DataSource> &source)
{
    Mutex::Autolock autoLock(gSSMutex);

    ALOGE("Create SStreamingExtractor\n");

    if (gSSLibHandle == NULL) {
#ifdef SS_MSPLAYREADY_TEST
        gSSLibHandle = dlopen("libsmoothstreaming_test.so", RTLD_NOW);
#else
        gSSLibHandle = dlopen("libsmoothstreaming.so", RTLD_NOW);
#endif
        if ( NULL == gSSLibHandle ) {
            ALOGE("SStreamingExtractor loading lib failure %s",dlerror());
            return;
        }
    }
    typedef RealSmoothStreamingExtractor *(*GetInstanceFunc)(sp<DataSource>);
    GetInstanceFunc getInstanceFunc =(GetInstanceFunc) dlsym(gSSLibHandle,"GetInstance");
    if ( !getInstanceFunc)  getInstanceFunc =(GetInstanceFunc) dlsym(gSSLibHandle,
                "_ZN7android11GetInstanceENS_2spINS_10DataSourceEEE");
    if (getInstanceFunc) {
            mRealExtractor = (*getInstanceFunc)(source);
            CHECK(mRealExtractor != NULL);
    }
}

SStreamingExtractor::~SStreamingExtractor() {

}

size_t SStreamingExtractor::countTracks() {
    return (mRealExtractor != NULL) ? mRealExtractor->countTracks() : 0;
}

sp<MetaData> SStreamingExtractor::getTrackMetaData(
        size_t index, uint32_t flags) {
    if (mRealExtractor == NULL) {
        return NULL;
    }
    return mRealExtractor->getTrackMetaData(index, flags);
}

sp<MetaData> SStreamingExtractor::getMetaData() {
    if (mRealExtractor == NULL) {
        return NULL;
    }
    return mRealExtractor->getMetaData();
}

sp<MediaSource> SStreamingExtractor::getTrack(size_t index) {
    if (mRealExtractor == NULL ) {
        return NULL;
    }
    return mRealExtractor->getTrack(index);
}


bool isManifestUrl(AString const &url) {
    AString lowerCaseUrl(url);
    lowerCaseUrl.tolower();
    int len = lowerCaseUrl.size();
    return lowerCaseUrl.endsWith("/manifest");
}


bool SniffSmoothStreaming(
    const sp<DataSource> &source, String8 *mimeType, float *confidence,
        sp<AMessage> *) {

    static char url[1024];
    int status=true;

    Mutex::Autolock autoLock(gSSMutex);

    AmlogicPlayerExtractorDataSource* pDataSrc = reinterpret_cast<AmlogicPlayerExtractorDataSource *>(source.get());
    if (pDataSrc  != NULL)
    {
         if ( pDataSrc->mAVProbeData && pDataSrc->mAVProbeData->filename && isManifestUrl(pDataSrc->mAVProbeData->filename) )
         {
             int url_len = strlen(pDataSrc->mAVProbeData->filename);
             *mimeType = PR_MIME_TYPE;
             *confidence = 10.0f;

             memcpy(url, pDataSrc->mAVProbeData->filename, url_len);
             url[url_len] = 0;
             if  ( gSSLibHandle == NULL) {
#ifdef SS_MSPLAYREADY_TEST
                 gSSLibHandle = dlopen("libsmoothstreaming_test.so", RTLD_NOW);
#else
                 gSSLibHandle = dlopen("libsmoothstreaming.so", RTLD_NOW);
#endif
                 if (NULL == gSSLibHandle) {
                     ALOGE("SStreamingExtractor loading lib failure %s",dlerror());
                     return false;
                 }
             }
             typedef RealSmoothStreamingExtractor *(*SnifferFunc)(char *);
             SnifferFunc snifferFunc =(SnifferFunc) dlsym(gSSLibHandle,"SStreamingUrl");
             if ( !snifferFunc )
                 snifferFunc=(SnifferFunc) dlsym(gSSLibHandle,"_ZN7android13SStreamingUrlEPc");

             if ( snifferFunc ) {
                 if ( ! ( * snifferFunc ) ( url ) ) {
                     ALOGE("SStreamingUrl not found in smoothstreaming lib\n");
                     return false;
                 }
             } else {
                 ALOGE("SStreamingUrl not found in smoothstreaming lib\n");
                 return false;
             }
             return true;
         }
    }
    return false;
}

} //namespace android

