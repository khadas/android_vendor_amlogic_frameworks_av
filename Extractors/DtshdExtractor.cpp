
#define LOG_TAG "DtshdExtractor"
#include <utils/Log.h>
#include "./include/DtshdExtractor.h"

#include <cutils/properties.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/MediaBuffer.h>
#include <media/stagefright/MediaBufferGroup.h>
//#include <media/stagefright/MediaDebug.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MediaSource.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/Utils.h>
#include <utils/String8.h>

#include <stdlib.h>
#include <string.h>
#include <ctype.h>


namespace android {

struct DtshdSource : public MediaSource {
    DtshdSource(const sp<DtshdExtractor> &extractor);

    virtual sp<MetaData> getFormat();

    virtual status_t start(MetaData *params = NULL);
    virtual status_t stop();

    virtual status_t read(
            MediaBuffer **buffer, const ReadOptions *options = NULL);

protected:
    virtual ~DtshdSource();

private:
    sp<DtshdExtractor> mExtractor;
    bool mStarted;

    DtshdSource(const DtshdSource &);
    DtshdSource &operator=(const DtshdSource &);
};


/***********************************************************************/
DtshdSource::DtshdSource(const sp<DtshdExtractor> &extractor)
    : mExtractor(extractor),
      mStarted(false) {
}

DtshdSource::~DtshdSource() {
    if (mStarted) {
        stop();
    }
}

sp<MetaData> DtshdSource::getFormat() {
    return mExtractor->getMetaData();
}

status_t DtshdSource::start(MetaData *params) {
    if (mStarted) {
        return INVALID_OPERATION;
    }

    mStarted = true;

    return OK;
}

status_t DtshdSource::stop() {
    mStarted = false;

    return OK;
}



status_t DtshdSource::read (
    MediaBuffer **out, const ReadOptions *options) {
    *out = NULL;
    //ALOGI("[%s %d] -> start",__FUNCTION__,__LINE__);
    int64_t seekTimeUs;
    ReadOptions::SeekMode mode;
    if (options && options->getSeekTo(&seekTimeUs, &mode)) {
        if (mExtractor->seekToTime(seekTimeUs) != OK) {
            return ERROR_END_OF_STREAM;
        }
    }

    MediaBuffer *packet;
    int i;
    //---------------------------
    int BytesNeed=DATA_LEN_READ_PER_TIME;
    int64_t pts;
    packet = new MediaBuffer(BytesNeed);

    mExtractor->read_frame((uint8_t*)packet->data(), &BytesNeed,&pts);
    if (BytesNeed < DATA_LEN_READ_PER_TIME)
    {
        return ERROR_END_OF_STREAM;
    }

    packet->set_range(0, BytesNeed);
    //---------------------------
    packet->meta_data()->setInt64(kKeyTime, pts);
    packet->meta_data()->setInt32(kKeyIsSyncFrame, 1);
    *out = packet;
    return OK;
}

/***********************************************************************/
/* Bit Mask Look up Table */
const uint32_t aui32DemuxGetMask[33] = {
    0x00000000, 0x00000001, 0x00000003, 0x00000007, 0x0000000f,
    0x0000001f, 0x0000003f, 0x0000007f, 0x000000ff, 0x000001ff,
    0x000003ff, 0x000007ff, 0x00000fff, 0x00001fff, 0x00003fff,
    0x00007fff, 0x0000ffff, 0x0001ffff, 0x0003ffff, 0x0007ffff,
    0x000fffff, 0x001fffff, 0x003fffff, 0x007fffff, 0x00ffffff,
    0x01ffffff, 0x03ffffff, 0x07ffffff, 0x0fffffff, 0x1fffffff,
    0x3fffffff, 0x7fffffff, 0xffffffff
};


/*****************************************************************************
* Function Name     : DTSDemuxBSInit
* Description       : Function to initialise Bit Stream Parsing Module

* Arguments         : stBit-Bitstream context
*                     pui8InputBuffer - InputBuffer from where to extract bits
*                     ui32InputBufferLength - InputBuffer Length
*                     ui32CDFormat - Flag to indicate whether the bitsream is
                      in CD Fromat
*                     ui32BigEndian - Flag to indicate whether the bitstream is
                      bigendian or not

* Called functions  : DTSDemuxGetStreamInfo, DTSDemuxGetNextFrame,
                      DTSDemuxSubStreamInit
                      DTSDemuxDecodeExSSHeader
* Global Data       : None
* Return Value      : '1' On success '0' On Failure
* Exceptions        :
*****************************************************************************/

 uint32_t AmlDcaDmuxBSInit(AmlDcaDemuxBs * pstrmBit,       /* Bitstream context */
                     uint8_t* pui8InputBuffer,         /* InputBuffer from where to extract bits */
                     uint32_t ui32InputBufferLength,   /* InputBuffer Length */
                     uint32_t ui32CDFormat,            /* Flag to indicate whether the bitsream is in CD Fromat */
                     uint32_t ui32BigEndian )          /* Flag to indicate whether the bitstream is bigendian or not */
{
    //ALOGI("[%s %d] -> start",__FUNCTION__,__LINE__);
     if( NULL == pstrmBit ||
         NULL == pui8InputBuffer)
        return (0);

     pstrmBit->pui8IpBuff = pui8InputBuffer;
     pstrmBit->ui32IpBuffSize = ui32InputBufferLength;
     pstrmBit->ui32BitsLeft = 0;
     pstrmBit->ui32BuffOffset = 0;
     pstrmBit->ui32EOS = 0;

    /* Intialise Load Cache Function,
       a word of data will be loaded to cache
       based on its format */
     if (ui32CDFormat)
     {
        if (ui32BigEndian)
             pstrmBit->pFpGetWord = AmlDcaDmuxReadWordCDFBigEndian;
        else
             pstrmBit->pFpGetWord = AmlDcaDmuxReadWordCDFLittleEndian;
     }
     else
     {
        if (ui32BigEndian)
             pstrmBit->pFpGetWord = AmlDcaDmuxReadWordBigEndian;
        else
             pstrmBit->pFpGetWord = AmlDcaDmuxReadWordLittleEndian;
    }
    return (1);
}

/*****************************************************************************
* Function Name     : DTSDemuxBSGetBits
* Description       : Function to read specified number of bits from the
                       stream.
* Arguments         : pstBit - Bitstream context
                       ui32BitsReq - Bits Required
* Called functions  : Bit Stream Parser Modules
* Global Data       : None
* Return Value      : Requested Number of Bits, Will be Zero if end of stream
                      is reached. On end of stream, end of stream flag will be
                      set.
* Exceptions        : None
*****************************************************************************/

 uint32_t AmlDcaDmuxBSGetBits (AmlDcaDemuxBs * pstrmBit,   /* Bitstream context */
                         uint32_t ui32BitsReq )        /* BitsRequired */
{
    uint32_t ui32Bits = 1;
    //ALOGI("[%s %d] -> start",__FUNCTION__,__LINE__);
     if ( NULL == pstrmBit )
        return (0);

     if ( ui32BitsReq <= pstrmBit->ui32BitsLeft)
     {
        /* Requested Bits are available in cache */
         pstrmBit->ui32BitsLeft -= ui32BitsReq;
         ui32Bits = (pstrmBit->ui32Cache >> pstrmBit->ui32BitsLeft) & aui32DemuxGetMask[ui32BitsReq];
     }
     else
     {
         if (NULL == pstrmBit->pFpGetWord)
         {
             return (0);
         }
         while (ui32BitsReq > 0 && !pstrmBit->ui32EOS)
         {
            /* Requested Bits are not available in cache so Load what is available */
             ui32Bits   = pstrmBit->ui32Cache & ( ( 1 << pstrmBit->ui32BitsLeft) -1);

            /* Update the request Bits */
             ui32BitsReq -= pstrmBit->ui32BitsLeft;
            ui32Bits <<= ui32BitsReq;

            /* Load 32 bits of data to cache */
             (*pstrmBit->pFpGetWord)(pstrmBit);

            /* Load the remaining bits requested */
             if ( ui32BitsReq <= pstrmBit->ui32BitsLeft)
             {
                 pstrmBit->ui32BitsLeft -= ui32BitsReq;
                 ui32Bits |= (pstrmBit->ui32Cache >> pstrmBit->ui32BitsLeft) & aui32DemuxGetMask[ui32BitsReq];
                ui32BitsReq = 0;
             }
        }
    }

    return (ui32Bits);
}


/*****************************************************************************
 * Function Name     : DTSDemuxReadWordCDFLittleEndian
 * Description       : Function to read 28 bits of data from stream in CD Format
 * Arguments         : pVoid - Bit Stream Context
 * Called functions  : DTSDemuxBSGetBits
 * Global Data       : None
 * Return Value      : None
 * Exceptions        : None
 *****************************************************************************/
 void AmlDcaDmuxReadWordCDFLittleEndian(void* pVoid)
 {
    uint32_t  ui32Word1;
    uint32_t  ui32Word2;

     AmlDcaDemuxBs*  pstrmBit= (AmlDcaDemuxBs *)pVoid;

     if (NULL == pstrmBit)
        return;

     if ((pstrmBit->ui32BuffOffset + 3) >= pstrmBit->ui32IpBuffSize)
     {
         pstrmBit->ui32EOS = 1;
        return;
    }

    /* Read Data as two Words */
    /* Read Data as two Words */
     ui32Word1   = (uint8_t)pstrmBit->pui8IpBuff[pstrmBit->ui32BuffOffset+1];
     ui32Word1 <<= 8;
     ui32Word1   |= (uint8_t)pstrmBit->pui8IpBuff[pstrmBit->ui32BuffOffset+0];

     ui32Word2   = (uint8_t)pstrmBit->pui8IpBuff[pstrmBit->ui32BuffOffset+3];
     ui32Word2 <<= 8;
     ui32Word2   |= (uint8_t)pstrmBit->pui8IpBuff[pstrmBit->ui32BuffOffset+2];

     pstrmBit->ui32Cache   = ui32Word1 & 0x3fff;
     pstrmBit->ui32Cache <<= 14;
     pstrmBit->ui32Cache  |= (ui32Word2 & 0x3fff);

     pstrmBit->ui32BuffOffset += 4;
     pstrmBit->ui32BitsLeft = 28;

    return;
}

/*****************************************************************************
 * Function Name     : DTSDemuxReadWordCDFBigEndian
 * Description       : Function to read 28 bits of data from stream in CD Format
                       Big Endian
 * Arguments         : pVoid - Bit Stream Context
 * Called functions  : DTSDemuxBSGetBits
 * Global Data       : None
 * Return Value      : None
 * Exceptions        : None
 *****************************************************************************/
 void AmlDcaDmuxReadWordCDFBigEndian(void * pVoid)
 {
    uint32_t  ui32Word1=0;
    uint32_t  ui32Word2=0;

     AmlDcaDemuxBs*  pstrmBit= (AmlDcaDemuxBs *)pVoid;

     if (NULL == pstrmBit)
        return;

     if ((pstrmBit->ui32BuffOffset + 3) >= pstrmBit->ui32IpBuffSize)
     {
         pstrmBit->ui32EOS = 1;
        return;
     }

     ui32Word1   = (uint8_t)pstrmBit->pui8IpBuff[pstrmBit->ui32BuffOffset+0];
     ui32Word1 <<= 8;
     ui32Word1   |= (uint8_t)pstrmBit->pui8IpBuff[pstrmBit->ui32BuffOffset+1];

     ui32Word2   = (uint8_t)pstrmBit->pui8IpBuff[pstrmBit->ui32BuffOffset+2];
     ui32Word2 <<= 8;
     ui32Word2   |= (uint8_t)pstrmBit->pui8IpBuff[pstrmBit->ui32BuffOffset+3];

     pstrmBit->ui32Cache   = ui32Word1 & 0x3fff;
     pstrmBit->ui32Cache <<= 14;
     pstrmBit->ui32Cache  |= (ui32Word2 & 0x3fff);

     pstrmBit->ui32BuffOffset += 4;
     pstrmBit->ui32BitsLeft = 28;

    return;
}

/*****************************************************************************
 * Function Name     : DTSDemuxReadWordLittleEndian
 * Description       : Function to read 32 bits of data from stream in
                       Little Endian Format
 * Arguments         : pVoid - Bit Stream Context
 * Called functions  : DTSDemuxBSGetBits
 * Global Data       : None
 * Return Value      : None
 * Exceptions        : None
 *****************************************************************************/
 void AmlDcaDmuxReadWordLittleEndian(void * pVoid)
 {
     AmlDcaDemuxBs*  pstrmBit= (AmlDcaDemuxBs *)pVoid;

     if (NULL == pstrmBit)
        return;

     if ((pstrmBit->ui32BuffOffset + 3) >= pstrmBit->ui32IpBuffSize)
     {
        pstrmBit->ui32EOS = 1;
        return;
    }

    /* Data available on buffer to load */

     pstrmBit->ui32Cache   = (uint8_t)pstrmBit->pui8IpBuff[pstrmBit->ui32BuffOffset+1];
     pstrmBit->ui32Cache <<= 8;
     pstrmBit->ui32Cache  |= (uint8_t)pstrmBit->pui8IpBuff[pstrmBit->ui32BuffOffset+0];
     pstrmBit->ui32Cache <<= 8;
     pstrmBit->ui32Cache  |= (uint8_t)pstrmBit->pui8IpBuff[pstrmBit->ui32BuffOffset+3];
     pstrmBit->ui32Cache <<= 8;
     pstrmBit->ui32Cache  |= (uint8_t)pstrmBit->pui8IpBuff[pstrmBit->ui32BuffOffset+2];

     pstrmBit->ui32BuffOffset += 4;
     pstrmBit->ui32BitsLeft = 32;

    return;
}

/*****************************************************************************
 * Function Name     : DTSDemuxReadWordBigEndian
 * Description       : Function to read 32 bits of data from stream in
                       Big Endian Format
 * Arguments         : pVoid - Bit Stream Context
 * Called functions  : DTSDemuxBSGetBits
 * Global Data       : None
 * Return Value      : None
 * Exceptions        : None
 *****************************************************************************/
 void AmlDcaDmuxReadWordBigEndian(void * pVoid)
 {
     AmlDcaDemuxBs*  pstrmBit= (AmlDcaDemuxBs *)pVoid;

     if (NULL == pstrmBit)
         return;

     if ( (pstrmBit->ui32BuffOffset + 3) > pstrmBit->ui32IpBuffSize)
     {
         pstrmBit->ui32EOS = 1;
        return;
    }

    /* Data available on buffer to load */
     pstrmBit->ui32Cache  = (uint8_t)pstrmBit->pui8IpBuff[pstrmBit->ui32BuffOffset+0];
     pstrmBit->ui32Cache <<=8;
     pstrmBit->ui32Cache |= (uint8_t)pstrmBit->pui8IpBuff[pstrmBit->ui32BuffOffset+1];
     pstrmBit->ui32Cache <<=8;
     pstrmBit->ui32Cache |= (uint8_t)pstrmBit->pui8IpBuff[pstrmBit->ui32BuffOffset+2];
     pstrmBit->ui32Cache <<=8;
     pstrmBit->ui32Cache |= (uint8_t)pstrmBit->pui8IpBuff[pstrmBit->ui32BuffOffset+3];

     pstrmBit->ui32BuffOffset += 4;
     pstrmBit->ui32BitsLeft = 32;
}

/***********************************************************************/

enum{
    SyncType_DTSDEMUX_SYNCWORD_CORE_16=0,
    SyncType_DTSDEMUX_SYNCWORD_CORE_14,
    SyncType_DTSDEMUX_SYNCWORD_CORE_16M,
    SyncType_DTSDEMUX_SYNCWORD_CORE_14M,
    SyncType_DTSDEMUX_SYNCWORD_CORE_24,
    SyncType_DTSDEMUX_SYNCWORD_CORE_24M,
    SyncType_Unknown,
};

void AmlDcaDmuxMatchDTSSyncType(uint32_t ui32TempSync, uint32_t ui32TempSync2, uint8_t* pDcaStrmFmt,
                                            uint8_t* bCDFormat, uint8_t* bMotorola, uint8_t*g_24_wav_msb_flag)
{

    *bCDFormat = 0;
    *bMotorola = 0;
    //*peDTSStreamFormat=SyncType_Unknown;
    *g_24_wav_msb_flag=0;

    if ( ui32TempSync == DTSDEMUX_SYNCWORD_CORE_16 )
    {
        *pDcaStrmFmt = SyncType_DTSDEMUX_SYNCWORD_CORE_16; /* 16-bit bit core stream*/
    } else if ( ui32TempSync == DTSDEMUX_SYNCWORD_CORE_14 ) {
        *bCDFormat = 1;
        *pDcaStrmFmt = SyncType_DTSDEMUX_SYNCWORD_CORE_14; /* 14-bit bit core stream*/
    } else if ( ui32TempSync == DTSDEMUX_SYNCWORD_CORE_16M ) {
        *bMotorola = 1;
        *pDcaStrmFmt = SyncType_DTSDEMUX_SYNCWORD_CORE_16M; /* 16-bit bit core stream - Motorola format*/
    } else if ( ui32TempSync == DTSDEMUX_SYNCWORD_CORE_14M ) {
        *bCDFormat = 1;
        *bMotorola = 1;
        *pDcaStrmFmt = SyncType_DTSDEMUX_SYNCWORD_CORE_14M;/* 32-bit sub stream Motorola*/
    } else if ( ui32TempSync == DTSDEMUX_SYNCWORD_SUBSTREAM ) {
        *pDcaStrmFmt = SyncType_Unknown;                   /* 32-bit sub stream*/
    } else if ( ui32TempSync == DTSDEMUX_SYNCWORD_SUBSTREAM_M ) {
        *bMotorola = 1;
        *pDcaStrmFmt = SyncType_Unknown;
    } else if ( (ui32TempSync & 0xffffff00) == (DTSDEMUX_SYNCWORD_CORE_24 & 0xffffff00)&&
        ((ui32TempSync2 >> 16) && 0xFF) == (DTSDEMUX_SYNCWORD_CORE_24 && 0xFF)) {
        /* 16-bit bit core stream - Motorola format*/
        *g_24_wav_msb_flag = 1;
        *bMotorola = 1;
        *pDcaStrmFmt = SyncType_DTSDEMUX_SYNCWORD_CORE_24;/* 16-bit bit core stream - Motorola format*/
    }
}

void  DtshdExtractor::DtshdGetStreamParas()
{
    #define BUF_LEN 40*1024
    #define META_DATA_NEED_LEN 12
    int64_t offset=0,file_pos=0,bytes_readed;
    mSource->getSize(&mFileSize);
    unsigned char *buf;//[BUF_LEN]={0};
    unsigned int ui32Sync_word=0,ui32Sync_word2=0,ui32SyncWordSave=0,ui32SyncWordSave2=0;
    int    SyncTpyeDetectFlag=0 ;
    uint8_t pDcaStrmFmt=SyncType_Unknown, bCDFormat=0,
          bMotorola=0,g_24_wav_msb_flag=0;
    int i;

    ALOGI("[%s %d] -> start",__FUNCTION__,__LINE__);

    buf=(unsigned char *)malloc(BUF_LEN);
    if (buf == NULL)
    {
        ALOGI("[%s %d] Err: malloc memory failed!",__FUNCTION__,__LINE__);
        return;
    }
    mOffsetVector.push(offset);
    mTimeStampVector.push(mDurationUs);

    file_pos = 0;
    offset = 0;
    bytes_readed = mSource->readAt(file_pos, buf, BUF_LEN);
    file_pos += bytes_readed;
    do
    {   //ALOGI("[%s %d] bytes_readed=%lld",__FUNCTION__,__LINE__,bytes_readed);
        //ALOGI("[%s %d] bytes_readed=%lld",__FUNCTION__,__LINE__,bytes_readed);
        for (i=0;i< bytes_readed-META_DATA_NEED_LEN;/*i++*/)
        {
            ui32Sync_word  = buf[i];  ui32Sync_word <<= 8;
            ui32Sync_word |= buf[i+1];  ui32Sync_word <<= 8;
            ui32Sync_word |= buf[i+2];  ui32Sync_word <<= 8;
            ui32Sync_word |= buf[i+3];

              if (pDcaStrmFmt == SyncType_Unknown)
              {
                ui32Sync_word2   = buf[i+4];  ui32Sync_word2 <<= 8;
                ui32Sync_word2  |= buf[i+5];  ui32Sync_word2 <<= 8;
                ui32Sync_word2  |= buf[i+6];  ui32Sync_word2 <<= 8;
                ui32Sync_word2  |= buf[i+7];
              }

              if (SyncTpyeDetectFlag == 0)
              {
                 AmlDcaDmuxMatchDTSSyncType(ui32Sync_word, ui32Sync_word2, &pDcaStrmFmt,
                  &bCDFormat, &bMotorola, &g_24_wav_msb_flag);
                 if (pDcaStrmFmt != SyncType_Unknown)
                 {
                        ALOGI("SyncType Detect: SyncType/%d\n",pDcaStrmFmt);
                    SyncTpyeDetectFlag = 1;
                    ui32SyncWordSave  = ui32Sync_word;
                    ui32SyncWordSave2 = ui32Sync_word2;
                    i += 4;
                } else {
                    //i32Index++;
                    i += 4;
                }
                continue;
            }

              if (SyncTpyeDetectFlag == 1)
              {
                uint8_t ui8NBLKS,ui8AMODE;
                uint16_t ui16FSIZE;
                uint16_t ui32SFREQ;

                AmlDcaDemuxBs  pstrmBit={0};
                int32_t bits_tmp,i32N;
                int64_t FrameDurationUs;
                uint8_t ui8IndexVecEnable=0;
                   if (pDcaStrmFmt == SyncType_DTSDEMUX_SYNCWORD_CORE_16 || pDcaStrmFmt == SyncType_DTSDEMUX_SYNCWORD_CORE_14 ||
                       pDcaStrmFmt == SyncType_DTSDEMUX_SYNCWORD_CORE_16M|| pDcaStrmFmt == SyncType_DTSDEMUX_SYNCWORD_CORE_14M)
                   {
                    if (ui32Sync_word == ui32SyncWordSave)
                        ui8IndexVecEnable=1;
                   }else if(pDcaStrmFmt == SyncType_DTSDEMUX_SYNCWORD_CORE_24){
                    if (ui32Sync_word == ui32SyncWordSave && ui32SyncWordSave2 == ui32Sync_word2 ) {
                        ui8IndexVecEnable=1;
                        //todo :bytes reverse!
                    }
                }

                if (ui8IndexVecEnable)//need 11 bytes to get frame header info:
                {
                    AmlDcaDmuxBSInit(&pstrmBit,&buf[i], BUF_LEN, bCDFormat,bMotorola);
                    AmlDcaDmuxBSGetBits(&pstrmBit,32);//syncword

                    bits_tmp = AmlDcaDmuxBSGetBits(&pstrmBit,28);
                    ui8NBLKS = (uint8_t)(((bits_tmp>>14)&127)+1);
                    if (ui8NBLKS < 6 || ui8NBLKS > 128 ) {
                        i+=4;
                        ALOGI("NOTE: unvalid NBLKS/%d, continue parse...",ui8NBLKS);
                        continue;
                    }
                    ui16FSIZE = (uint16_t)(((bits_tmp)&16383) + 1);

                    if (ui16FSIZE < 96 || ui16FSIZE>16384 ) {
                        i+=4;
                        ALOGI("NOTE: unvalid FSIZE/%d, continue parse...",ui16FSIZE);
                        continue;
                    }

                    bits_tmp = AmlDcaDmuxBSGetBits(&pstrmBit,28);
                    ui8AMODE = (uint8_t)((bits_tmp>>22)&63);
                    if (ui8AMODE>9) {
                        i+=4;
                        ALOGI("NOTE: unvalid AMODE/%d, continue parse...",ui8AMODE);
                        continue;
                    }

                    i32N = ((bits_tmp>>18)&15);
                    if (i32N <= FSTABLE_SIZE)
                    {
                        ui32SFREQ = FSTBL[i32N];
                    } else {
                        i+=4;
                        ALOGI("NOTE: unvalid SFREQ_INDEX/%d, continue parse...",i32N);
                        continue;
                    }

                        /*FSTBL[0] is 0, if sampleRate set to zero, will crash when compute FrameDurationUs*/

                    mSampleRate=ui32SFREQ;
                    mNumChannels=aui8NumCh[ui8AMODE];
                    FrameDurationUs= ui8NBLKS*32*1000000ll/mSampleRate;
                    mDurationUs+=FrameDurationUs;
                    ALOGI("[%s %d] offset/%lld  i/%d ",__FUNCTION__,__LINE__,offset,i);
                    mOffsetVector.push(offset+i);
                    mTimeStampVector.push(mDurationUs);
                    mFrameCount++;
                    i+=ui16FSIZE;
                } else {
                    //i32Index++;
                    i+=4;
                }
            }
        }
        memcpy(buf,buf+bytes_readed-META_DATA_NEED_LEN,META_DATA_NEED_LEN);
        offset += bytes_readed-META_DATA_NEED_LEN;

        bytes_readed = mSource->readAt(file_pos, buf+META_DATA_NEED_LEN, BUF_LEN-META_DATA_NEED_LEN);
        file_pos += bytes_readed;
    } while (bytes_readed ==(BUF_LEN-META_DATA_NEED_LEN));

    if (buf != NULL)
        free(buf);

    ALOGI("mFrameCount/%d  mTimeStampVector.size()/%d",mFrameCount,mTimeStampVector.size());
    //mOffsetVector.push(mFileSize);
    //mTimeStampVector.push(mDurationUs);
    ALOGI("[%s %d] -> end",__FUNCTION__,__LINE__);
}


//-------------------------------------------
DtshdExtractor::DtshdExtractor(const sp<DataSource> &source)
{
    ALOGI("[%s %d] -> start",__FUNCTION__,__LINE__);
    mSampleRate=0;
    mNumChannels=0;
    mBitsPerSample=0;

    mOffset=0;
    mDurationUs=0;
    mFrameDecodedCnt=0;
    mFrameCount=0;
    mFileSize=0;

    mSource=source;
    mMeta=NULL;
    mInitCheck=-1;

    DtshdGetStreamParas();

    mMeta = new MetaData;
    mMeta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_DTSHD);
    mMeta->setInt32(kKeySampleRate,mSampleRate);
    mMeta->setInt32(kKeyChannelCount,(mNumChannels>2?2:mNumChannels));
    mMeta->setInt64(kKeyDuration, mDurationUs);

    ALOGI("[%s]STREAM_PARS: mSampleRate/%d mNumChannels/%d mDurationUs/%lld(us) ",__FUNCTION__,
        mSampleRate,mNumChannels,mDurationUs);

    mInitCheck=OK;
}

status_t DtshdExtractor::seekToTime(int64_t timeUs)
{
   int size=mTimeStampVector.size();
   int index;
   ALOGI("[%s %d] -> start",__FUNCTION__,__LINE__);
   for (index=0;index+1<size;index++)
   {
        if (mTimeStampVector.itemAt(index) <= timeUs && mTimeStampVector.itemAt(index+1) >= timeUs)
        {
            mOffset=mOffsetVector.itemAt(index);
            ALOGI("[%s]seek success: target_time/%lld(us)-->given_time/%lld",__FUNCTION__,
            timeUs,mTimeStampVector.itemAt(index));
            mFrameDecodedCnt=index;
            return OK;
        }
    }
    ALOGE("[%s]seek failed: target_time/%lld(us)--> start/%lld(us) end/%lld(us)",__FUNCTION__,
    timeUs,mTimeStampVector.itemAt(0),mTimeStampVector.itemAt(size-1));
    return BAD_INDEX;
}

void DtshdExtractor::read_frame(unsigned char *buf, int*size,int64_t *pts)
{
    //ALOGI("[%s %d] -> start",__FUNCTION__,__LINE__);
    *size=mSource->readAt(mOffset,buf,DATA_LEN_READ_PER_TIME);
    while (mOffset > mOffsetVector.itemAt(mFrameDecodedCnt))
    {
        mFrameDecodedCnt++;
    }
    *pts=mTimeStampVector.itemAt(mFrameDecodedCnt);
    ALOGI("mFrameDecodedCnt/%lld pts/%lld ",mFrameDecodedCnt,*pts);
    mOffset +=*size;
}

DtshdExtractor::~DtshdExtractor()
{
}

size_t DtshdExtractor::countTracks()
{
    ALOGI("[%s %d] -> start",__FUNCTION__,__LINE__);
    return mInitCheck != OK ? 0 : 1;
}

sp<MediaSource> DtshdExtractor::getTrack(size_t index)
{
    if (index >= 1) {
        return NULL;
    }

    return new DtshdSource(this);
}

sp<MetaData> DtshdExtractor::getTrackMetaData(size_t index, uint32_t flags)
{
    if (index >= 1)
        return NULL;
    return mMeta;
}

sp<MetaData> DtshdExtractor::getMetaData()
{
    return mMeta;
}

//------------------------------------------
int AmlDcaDmuxMatchDTSSync2(uint32_t ui32TempSync, uint32_t ui32TempSync2)
{
    /* 16-bit bit core stream*/
    if ( ui32TempSync == DTSDEMUX_SYNCWORD_CORE_16  || ui32TempSync == DTSDEMUX_SYNCWORD_CORE_14 ||
        ui32TempSync == DTSDEMUX_SYNCWORD_CORE_16M || ui32TempSync == DTSDEMUX_SYNCWORD_CORE_14M||
        ui32TempSync == DTSDEMUX_SYNCWORD_SUBSTREAM|| ui32TempSync == DTSDEMUX_SYNCWORD_SUBSTREAM_M)
    {
        return 1;
    }

    if( (ui32TempSync & 0xffffff00) == (DTSDEMUX_SYNCWORD_CORE_24 & 0xffffff00)&&
        ((ui32TempSync2 >> 16) & 0xFF) == (DTSDEMUX_SYNCWORD_CORE_24 & 0xFF))
    {
       return 1;
    }
    return 0;
}

int Dcahd_probe( unsigned char *buf,int size)
{
   int i32Index=0;
   int result=0;
   unsigned int ui32Sync_word=0,ui32Sync_word2=0;
   for (i32Index=0; i32Index+7<size;i32Index++)
   {
        ui32Sync_word    = buf[i32Index];      ui32Sync_word <<= 8;
        ui32Sync_word   |= buf[i32Index + 1];  ui32Sync_word <<= 8;
        ui32Sync_word   |= buf[i32Index + 2];  ui32Sync_word <<= 8;
        ui32Sync_word   |= buf[i32Index + 3];

        ui32Sync_word2   = buf[i32Index + 4];  ui32Sync_word2 <<= 8;
        ui32Sync_word2  |= buf[i32Index + 5];  ui32Sync_word2 <<= 8;
        ui32Sync_word2  |= buf[i32Index + 6];  ui32Sync_word2 <<= 8;
        ui32Sync_word2  |= buf[i32Index + 7];
        result=AmlDcaDmuxMatchDTSSync2(ui32Sync_word,ui32Sync_word2);
        if (result) {
            ALOGI("SyncWord detect: ui32Sync_word/0x%x ui32Sync_word2/0x%x ",ui32Sync_word,ui32Sync_word2);
            break;
        }
    }

    return result;
}

bool SniffDcahd(const sp<DataSource> &source, String8 *mimeType, float *confidence,
        sp<AMessage> *)
{
    ALOGI("%s %d\n",__FUNCTION__,__LINE__);
    unsigned char tmp[MAX_INPUT_BUF_SIZE];
    int i32BytesToRead=0;
    i32BytesToRead=source->readAt(0, tmp, MAX_INPUT_BUF_SIZE);

    if (Dcahd_probe(tmp,i32BytesToRead)>0)
    {
        mimeType->setTo(MEDIA_MIMETYPE_AUDIO_DTSHD);
        *confidence = 0.19f;
        return true;
    } else {
        return false;
    }
}
//------------------------------------------

}

