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
#define LOG_TAG "AmThumbnail"
#include <utils/Log.h>

#include <AmThumbnail.h>
#include <cutils/properties.h>

namespace android
{

#define TRY_DECODE_MAX (50)
#define READ_FRAME_MAX (10*25)
#define READ_FRAME_MIN (2*25)
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

static URLProtocol android_protocol;

status_t AmThumbnailInt::BasicInit()
{
    av_register_all();
    avformat_network_init();
    return 0;
}

AmThumbnailInt::AmThumbnailInt()
{
    ALOGV("AmThumbnailInt\n");

    mVwidth = 0 ;
    mVheight = 0;
    mDuration = 0;
    mThumbnailTime = 0;
    mThumbnailOffset = 0;
    mDataSize = 0;
    mData = NULL;
    mMaxframesize = 0;
    mDisplayAspectRatio.den = 0;
    mDisplayAspectRatio.num = 0;
    memset(&mStream, 0, sizeof(struct stream));
    mStream.videoStream = -1;

    BasicInit();
}

AmThumbnailInt::~AmThumbnailInt()
{
    ALOGV("~AmThumbnailInt\n");
    if (mData) {
        free(mData);
        mData = NULL;
    }

    if (mStream.pFrameRGB) {
        av_free(mStream.pFrameRGB);
        mStream.pFrameRGB = NULL;
    }
    if (mStream.pFrameYUV) {
        av_free(mStream.pFrameYUV);
        mStream.pFrameYUV = NULL;
    }
    if (mStream.pCodecCtx) {
        avcodec_close(mStream.pCodecCtx);
    }
    if (mStream.pFormatCtx) {
        avformat_close_input(&mStream.pFormatCtx);
        //av_close_input_file(mStream.pFormatCtx);
    }
}

float AmThumbnailInt::amPropGetFloat(const char * str, float def)
{
    char value[92];
    float ret = def;
    if (property_get(str, value, NULL) > 0) {
        if ((sscanf(value, "%f", &ret)) > 0) {
            ALOGV("%s is set to %f\n", str, ret);
            return ret;
        }
    }
    ALOGV("%s is not set used def=%f\n", str, ret);
    return ret;
}

void AmThumbnailInt::calc_aspect_ratio(rational *ratio, struct stream *stream)
{
    int num, den;

    av_reduce(&num, &den,
              stream->pCodecCtx->width * stream->pCodecCtx->sample_aspect_ratio.num,
              stream->pCodecCtx->height * stream->pCodecCtx->sample_aspect_ratio.den,
              1024 * 1024);
    ratio->num = num;
    ratio->den = den;
}

int AmThumbnailInt::av_read_next_video_frame(AVFormatContext *pFormatCtx, AVPacket *pkt, int vindex)
{
    int r = -1;
    int retry = 500;
    do {
        r = av_read_frame(pFormatCtx, pkt);
        if (r == 0 && pkt->stream_index == vindex) {
            break;
        }
        av_free_packet(pkt);
        if (r < 0) {
            break;
        } else {
            r = -2;    /*a audio or other frame.*/
        }
    } while (retry-- > 0);
    return r;
}

void AmThumbnailInt::find_best_keyframe(AVFormatContext *pFormatCtx, int video_index, int count, int64_t *time, int64_t *offset, int *maxsize)
{
    int i = 0;
    int maxFrameSize = 0;
    int64_t thumbTime = 0;
    int64_t thumbOffset = 0;
    AVPacket packet;
    int r = 0;
    int find_ok = 0;
    int keyframe_index = 0;
    AVStream *st = pFormatCtx->streams[video_index];
    int havepts = 0;
    int nopts = 0;
    *maxsize = 0;
    if (count <= 0) {
        float newcnt = amPropGetFloat("libplayer.thumbnail.scan.count");
        count = 100;
        if (newcnt >= 1) {
            count = (int)newcnt;
        }
    }

    do {
        r = av_read_next_video_frame(pFormatCtx, &packet, video_index);
        if (r < 0) {
            break;
        }
        ALOGV("[find_best_keyframe][%d]read frame packet.size=%d,pts=%" PRId64 "\n", i, packet.size, packet.pts);
        havepts = (packet.pts > 0 || havepts);
        nopts = (i > 10) && !havepts;
        if (packet.size > maxFrameSize && (packet.pts >= 0 || nopts)) { //packet.pts >= 0 can used for seek.
            maxFrameSize = packet.size;
            thumbTime = packet.pts;
            thumbOffset = avio_tell(pFormatCtx->pb) - packet.size;
            keyframe_index = i;
            find_ok = 1;
        }

        av_free_packet(&packet);
        if (i > 5 && find_ok && maxFrameSize > 100 * 1024 / (1 + i / 20)) {
            break;
        }
    } while (i++ < count);

    if (find_ok) {
        ALOGV("[%s]return thumbTime=%" PRId64 " thumbOffset=%llx\n", __FUNCTION__, thumbTime, thumbOffset);
        if (i <= 5) {
            ALOGV("[%s:%d]not so much frames %d, set single thread decode\n", __FUNCTION__, __LINE__, i);
            av_opt_set_int(mStream.pCodecCtx, "threads", 1, 0);
        }
        if (thumbTime >= 0 && thumbTime != AV_NOPTS_VALUE) {
            *time = av_rescale_q(thumbTime, st->time_base, AV_TIME_BASE_Q);
        } else {
            *time = AV_NOPTS_VALUE;
        }
        *offset = thumbOffset;
        *maxsize = maxFrameSize;
        r = 0;
    } else {
        ALOGV("[%s]find_best_keyframe failed\n", __FUNCTION__);
    }

    return;
}

int AmThumbnailInt::amthumbnail_decoder_open(const sp<DataSource>& source, bool is_slow_media)
{
    unsigned int i;
    int video_index, audio_index;
    mIsSlowMedia = is_slow_media;


    mSourceAdapter = new AmFFmpegByteIOAdapter();
    mSourceAdapter->init(source);

    mInputFormat = probeFormat(source);
    if (mInputFormat == NULL) {
        ALOGE("Failed to probe the input stream.");
        goto err;
    }

    if (!(mStream.pFormatCtx = openAVFormatContext(
            mInputFormat, mSourceAdapter.get()))) {
        ALOGE("Failed to open FFmpeg context.");
        goto err;
    }

    if (mStream.pFormatCtx->pb) {
        mStream.pFormatCtx->pb->mediascan_flag = 1;
    }

#ifdef DUMP_INDEX
{
    int i, j;
    AVStream *pStream;
    av_dump_format(AVFormatContext * ic,int index,const char * url,int is_output)(mStream.pFormatCtx, 0, NULL, 0);
    ALOGV("*********************************************\n");
    for (i = 0; i < mStream.pFormatCtx->nb_streams; i ++) {
        pStream = mStream.pFormatCtx->streams[i];
        if (pStream) {
            for (j = 0; j < pStream->nb_index_entries; j++) {
                ALOGV("stream[%d]:idx[%d] pos:%llx time:%llx\n", i, j, pStream->index_entries[j].pos, pStream->index_entries[j].timestamp);
            }
        }
    }
    ALOGV("*********************************************\n");
}
#endif

    for (i = 0; i < mStream.pFormatCtx->nb_streams; i++) {
        if (mStream.pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            mStream.videoStream = i;
            break;
        }
    }

    video_index = mStream.videoStream;
    if (video_index == -1) {
        ALOGV("Didn't find a video stream!\n");
        audio_index = -1;
        for (i = 0; i < mStream.pFormatCtx->nb_streams; i++) {
            if (mStream.pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
                audio_index = i;
                break;
            }
        }

        if (audio_index == -1) {
            ALOGV("Didn't find a audio stream, too!\n");
            goto err1;
        } else {
            return 0;
        }
    } else {

        mStream.pCodecCtx = mStream.pFormatCtx->streams[video_index]->codec;
        if (mStream.pCodecCtx == NULL) {
            ALOGV("pCodecCtx is NULL !\n");
            goto err1;
        } else {
            mVwidth = mStream.pCodecCtx->width;
            mVheight = mStream.pCodecCtx->height;
        }

        const char* file_format = mStream.pFormatCtx->iformat->name;
        bool isMp3 = false;

        if (file_format && !strcmp(file_format, "mp3")) {
            isMp3 = true;
        }
        if ((mVwidth * mVheight > 1920 * 1088) && !isMp3) {
            ALOGV("Can't support 4k\n");
            goto err1;
        }

        mStream.pCodec = avcodec_find_decoder(mStream.pCodecCtx->codec_id);
        if (mStream.pCodec == NULL) {
            ALOGV("Didn't find codec!\n");
            goto err1;
        }

        /* detect frames */
        if (!mIsSlowMedia)
            find_best_keyframe(mStream.pFormatCtx, video_index, 0, &mThumbnailTime, &mThumbnailOffset, &mMaxframesize);

        if (avcodec_open2(mStream.pCodecCtx, mStream.pCodec, NULL) < 0) {
            ALOGV("Couldn't open codec!\n");
            goto err1;
        }

        mDuration = mStream.pFormatCtx->duration;

        mStream.pFrameYUV = av_frame_alloc();
        if (mStream.pFrameYUV == NULL) {
            ALOGV("alloc YUV frame failed!\n");
            goto err2;
        }

        mStream.pFrameRGB = av_frame_alloc();
        if (mStream.pFrameRGB == NULL) {
            ALOGV("alloc RGB frame failed!\n");
            goto err3;
        }

        mDataSize = avpicture_get_size(DEST_FMT, mVwidth, mVheight);
        mData = (uint8_t *)malloc(mDataSize);
        if (mData == NULL) {
            ALOGV("alloc buffer failed!\n");
            goto err4;
        }

        avpicture_fill((AVPicture *)mStream.pFrameRGB, mData, DEST_FMT, mVwidth, mVheight);

        return 0;
    }

err4:
    av_frame_free(&mStream.pFrameRGB);
    mStream.pFrameRGB = NULL;
err3:
    av_frame_free(&mStream.pFrameYUV);
    mStream.pFrameYUV = NULL;
err2:
    avcodec_close(mStream.pCodecCtx);
err1:
    avformat_close_input(&mStream.pFormatCtx);
    //av_close_input_file(mStream.pFormatCtx);
err:
    memset(&mStream, 0, sizeof(struct stream));
    return -1;
}

int AmThumbnailInt::amthumbnail_extract_video_frame(int64_t time, int flag)
{
    int frameFinished = 0;
    int tryNum = 0;
    int i = 0;
    int64_t ret ;
    int64_t timestamp = 1;

    struct stream *stream = &mStream;
    AVFormatContext *pFormatCtx = stream->pFormatCtx;

    ALOGV("[%s:%d]video streamindex %d", __FUNCTION__, __LINE__, stream->videoStream);
    if ((stream->videoStream < 0) || (stream->videoStream >= (int)pFormatCtx->nb_streams)) {
        ALOGV("[%s:%d]Illigle video streamindex %d", __FUNCTION__, __LINE__, stream->videoStream);
        return -1;
    }

    AVPacket        packet;
    AVStream *pStream = pFormatCtx->streams[stream->videoStream];
    AVCodecContext *pCodecCtx = pFormatCtx->streams[stream->videoStream]->codec;

    float starttime = amPropGetFloat("libplayer.thumbnail.starttime");
    int duration = pFormatCtx->duration / AV_TIME_BASE;

    if (time >= 0) {
        timestamp = time;
        if (pFormatCtx->start_time != (int64_t)AV_NOPTS_VALUE) {
            timestamp += pFormatCtx->start_time;
        }
    } else {
        if (mIsSlowMedia) {
            timestamp = 0;
        } else if (starttime >= 0) {
            timestamp = (int64_t)starttime;
            if (timestamp >= duration) {
                timestamp = duration - 1;
            }
            if (timestamp <= 0) {
                timestamp = 0;
            }
        } else {
            if (duration > 360) {
                timestamp = 120;/*long file.don't do more seek..*/
            } else if (duration > 6) {
                timestamp = duration / 3;
            } else {
                timestamp = 0;/*file is too short,try from start.*/
            }
        }

        timestamp = timestamp * AV_TIME_BASE + pFormatCtx->start_time;
    }

    ALOGV("[%s:%d]time %" PRId64 ", timestamp %" PRId64 ", starttime %f, duration %d %" PRId64 "\n",
        __FUNCTION__, __LINE__, time, timestamp, starttime, duration, pFormatCtx->duration);

    if (!strcmp(pFormatCtx->iformat->name, "mpegts") || !strcmp(pFormatCtx->iformat->name, "mpeg"))
    {
        int64_t file_offset = 0;
        if (timestamp == pFormatCtx->start_time) {
            ALOGV("[%s:%d]%s file seek to file head\n", __FUNCTION__, __LINE__, pFormatCtx->iformat->name);
            avio_seek(pFormatCtx->pb, file_offset, SEEK_SET); // just seek to head if from 0 for ts/ps
        } else {
            int64_t file_size = avio_size(pFormatCtx->pb);
            if ((pFormatCtx->duration > 0) && (file_size > 0)) {
                file_offset = 1.0 * timestamp * file_size / pFormatCtx->duration;
                ALOGV("[%s:%d]%s file seek to 0x%llx\n", __FUNCTION__, __LINE__, pFormatCtx->iformat->name, file_offset);
                avio_seek(pFormatCtx->pb, file_offset, SEEK_SET);
            } else {
                timestamp = av_rescale(timestamp, pStream->time_base.den, AV_TIME_BASE * (int64_t)pStream->time_base.num);
                ALOGV("[thumbnail_extract_video_frame:%d]time=%" PRId64 " time=%" PRId64 "  offset=%" PRId64 " timestamp=%" PRId64 "!\n",
                      __LINE__, time, mThumbnailTime, mThumbnailOffset, timestamp);

                if (av_seek_frame(pFormatCtx, stream->videoStream, timestamp, AVSEEK_FLAG_BACKWARD) < 0) {
                    ALOGV("[%s:%d]av_seek_frame failed!", __FUNCTION__, __LINE__);
                }
            }
        }
    } else {
        timestamp = av_rescale(timestamp, pStream->time_base.den, AV_TIME_BASE * (int64_t)pStream->time_base.num);
        ALOGV("[thumbnail_extract_video_frame:%d]time=%" PRId64 " time=%" PRId64 "  offset=%" PRId64 " timestamp=%" PRId64 "!\n",
              __LINE__, time, mThumbnailTime, mThumbnailOffset, timestamp);

        if (av_seek_frame(pFormatCtx, stream->videoStream, timestamp, AVSEEK_FLAG_BACKWARD) < 0) {
            ALOGV("[%s:%d]av_seek_frame failed!", __FUNCTION__, __LINE__);
        }
    }

    avcodec_flush_buffers(stream->pCodecCtx);

    i = 0;
    while (av_read_next_video_frame(pFormatCtx, &packet, stream->videoStream) >= 0) {
        AVFrame *pFrame = NULL;
        int temp_ret;
        ALOGV("[%s] av_read_frame frame size=%d,pts=%" PRId64 "\n", __FUNCTION__, packet.size, packet.pts);
        i++;
        if (!mIsSlowMedia && packet.size < MAX(mMaxframesize / 10, packet.size) && i < READ_FRAME_MIN) {
            continue;/*skip small size packets,it maybe a black frame*/
        }

        temp_ret = avcodec_decode_video2(stream->pCodecCtx, stream->pFrameYUV, &frameFinished, &packet);
        pFrame = stream->pFrameYUV;
        ALOGV("[%s]decode video frame, finish=%d key=%d offset=%llx type=%dcodec_id=%x,quality=%d tryNum=%d ret %d\n",
              __FUNCTION__, frameFinished, pFrame->key_frame, avio_tell(pFormatCtx->pb), pFrame->pict_type, pCodecCtx->codec_id, pFrame->quality, tryNum, temp_ret);
        if (frameFinished &&
            ((pFrame->key_frame && pFrame->pict_type == AV_PICTURE_TYPE_I) ||
             (pFrame->key_frame && pFrame->pict_type == AV_PICTURE_TYPE_SI) ||
             (pFrame->key_frame && pFrame->pict_type == AV_PICTURE_TYPE_BI) ||
             (tryNum > 4 && pFrame->key_frame) ||
             (tryNum > 5 && pFrame->pict_type == AV_PICTURE_TYPE_I) ||
             (tryNum > 6 && pFrame->pict_type == AV_PICTURE_TYPE_SI) ||
             (tryNum > 7 && pFrame->pict_type == AV_PICTURE_TYPE_BI) ||
             (tryNum > (TRY_DECODE_MAX - 1) && i > READ_FRAME_MAX && pFrame->pict_type == AV_PICTURE_TYPE_P) ||
             (tryNum > (TRY_DECODE_MAX - 1) && i > READ_FRAME_MAX && pFrame->pict_type == AV_PICTURE_TYPE_B) ||
             (tryNum > (TRY_DECODE_MAX - 1) && i > READ_FRAME_MAX && pFrame->pict_type == AV_PICTURE_TYPE_S))) { /*not find a I FRAME too long,try normal frame*/
            ALOGV("[%s]pCodecCtx->codec_id=%x tryNum=%d\n", __FUNCTION__, pCodecCtx->codec_id, tryNum);

            struct SwsContext *img_convert_ctx;
            img_convert_ctx = sws_getContext(stream->pCodecCtx->width, stream->pCodecCtx->height, stream->pCodecCtx->pix_fmt,
                                             mVwidth, mVheight, DEST_FMT, SWS_BICUBIC, NULL, NULL, NULL);
            if (img_convert_ctx == NULL) {
                ALOGV("can not initialize the coversion context!\n");
                av_free_packet(&packet);
                break;
            }

            sws_scale(img_convert_ctx, stream->pFrameYUV->data, stream->pFrameYUV->linesize, 0,
                      mVheight, stream->pFrameRGB->data, stream->pFrameRGB->linesize);
            sws_freeContext(img_convert_ctx);
            av_free_packet(&packet);
            goto ret;
        }
        av_free_packet(&packet);

        if (tryNum++ > TRY_DECODE_MAX && i > READ_FRAME_MAX) {
            break;
        }
        if (tryNum % 10 == 0) {
            usleep(100);
        }
    }

    if (mData) {
        free(mData);
        mData = NULL;
    }
    if (stream->pFrameRGB) {
        av_free(stream->pFrameRGB);
        stream->pFrameRGB = NULL;
    }
    if (stream->pFrameYUV) {
        av_free(stream->pFrameYUV);
        stream->pFrameYUV = NULL;
    }
    avcodec_close(stream->pCodecCtx);
    memset(&mStream, 0, sizeof(struct stream));
    return -1;

ret:
    return 0;
}

int AmThumbnailInt::amthumbnail_read_frame(char* buffer)
{
    int i;
    int index = 0;

    for (i = 0; i < mVheight; i++) {
        memcpy(buffer + index, mStream.pFrameRGB->data[0] + i * mStream.pFrameRGB->linesize[0], mVwidth * 2);
        index += mVwidth * 2;
    }

    return 0;
}

void AmThumbnailInt::amthumbnail_get_video_size(int* width, int* height)
{
    *width = mVwidth;
    *height = mVheight;
}

float AmThumbnailInt::amthumbnail_get_aspect_ratio()
{
    calc_aspect_ratio(&mDisplayAspectRatio, &mStream);

    if (!mDisplayAspectRatio.num || !mDisplayAspectRatio.den) {
        return (float)mVwidth / mVheight;
    } else {
        return (float)mDisplayAspectRatio.num / mDisplayAspectRatio.den;
    }
}

void AmThumbnailInt::amthumbnail_get_duration(int64_t *duration)
{
    *duration = mStream.pFormatCtx->duration;
    ALOGV("amthumbnail_get_duration duration:%" PRId64 " \n", *duration);
}

int AmThumbnailInt::amthumbnail_get_key_metadata(char* key, const char** value)
{
    AVDictionaryEntry *tag = NULL;
    bool hasMetadata = false;

    if (mStream.pFormatCtx->metadata) {
        hasMetadata = true;
    }
    for (int i = 0; i < mStream.pFormatCtx->nb_streams; i++) {
        if (mStream.pFormatCtx->streams[i]->metadata) {
            hasMetadata = true;
            break;
        }
    }
    if (!hasMetadata) {
        ALOGI("[%s:%d]======metadata is null", __FUNCTION__, __LINE__);
        return 0;
    }

    if (!memcmp(key, "rotate", 6)) {
        int rot;
        if (mStream.videoStream == -1) { /*no video.*/
            return 0;
        }
        amthumbnail_get_video_rotation(&rot);
        char *tmp = (char*)av_malloc(32);
        memset(tmp, 0x00, 32);
        sprintf(tmp, "%d", rot);
        *value = tmp;
        return 1;
    }
    tag = av_dict_get(mStream.pFormatCtx->metadata, key, tag, 0);
    if (tag) {
        *value = tag->value;
        return 1;
    }

    for (int i = 0; i < mStream.pFormatCtx->nb_streams; i++) {
        tag = av_dict_get(mStream.pFormatCtx->streams[i]->metadata, key, tag, 0);
        if (tag) {
            *value = tag->value;
            return 1;
        }

    }
    return 0;
}

int AmThumbnailInt::amthumbnail_get_key_data(char* key, const void** data, int* data_size)
{
    if (mStream.videoStream == -1) {
            ALOGV("no video data");
            return 0;
    }
    AVStream *video_st = mStream.pFormatCtx->streams[mStream.videoStream];

    if (video_st && video_st->disposition & AV_DISPOSITION_ATTACHED_PIC) {
        if (video_st->attached_pic.size > 0) {
            ALOGV("amthumbnail_get_key_data:pic_size:%d\n", data_size);
            *data = video_st->attached_pic.data;
            *data_size = video_st->attached_pic.size;
            return 1;
         }
   }

    return 0;
}

int AmThumbnailInt::amthumbnail_get_tracks_info(int *vtracks, int *atracks, int *stracks)
{
    AVStream **avs = mStream.pFormatCtx->streams;
    unsigned int i;
    *vtracks = 0;
    *atracks = 0;
    *stracks = 0;

    for (i = 0; i < mStream.pFormatCtx->nb_streams; i++) {
        if (avs[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            (*vtracks)++;
        } else if (avs[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            (*atracks)++;
        } else if (avs[i]->codec->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            (*stracks)++;
        }
    }
    ALOGV("thumbnail_get_tracks_info v:%d a:%d s:%d \n", *vtracks, *atracks, *stracks);
    return 0;
}

void AmThumbnailInt::amthumbnail_get_video_rotation(int* rotation)
{
    *rotation = 0;
    AVDictionaryEntry *lang =
                av_dict_get(mStream.pFormatCtx->streams[mStream.videoStream]->metadata, "rotate", NULL, 0);
    if (lang != NULL && lang->value != NULL) {
        *rotation = atoi(lang->value);
    }

    return;
}

int AmThumbnailInt::amthumbnail_decoder_close()
{
    if (mData) {
        free(mData);
        mData = NULL;
    }
    if (mStream.pFrameRGB) {
        av_free(mStream.pFrameRGB);
        mStream.pFrameRGB = NULL;
    }
    if (mStream.pFrameYUV) {
        av_free(mStream.pFrameYUV);
        mStream.pFrameYUV = NULL;
    }

    avcodec_close(mStream.pCodecCtx);

    return 0;
}

}
