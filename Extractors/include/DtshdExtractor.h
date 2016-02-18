
#ifndef __DTSHDEXTRACTOR_H__
#define __DTSHDEXTRACTOR_H__


#include <utils/Errors.h>
#include <media/stagefright/MediaExtractor.h>
#include <utils/Vector.h>

namespace android {

typedef struct URLContext URLContext;

struct AMessage;
class  DataSource;
class  String8;
struct DtshdSource;


class DtshdExtractor :public MediaExtractor
{
    
public:	
    DtshdExtractor(const sp<DataSource> &source);

    virtual size_t countTracks();
    virtual sp<MediaSource> getTrack(size_t index);
    virtual sp<MetaData> getTrackMetaData(size_t index, uint32_t flags);
    virtual sp<MetaData> getMetaData();
	status_t init();
	void  DtshdGetStreamParas();
	status_t seekToTime(int64_t timeUs);
	void read_frame(unsigned char *buf, int*size,int64_t *pts);
protected:
    virtual ~DtshdExtractor();
private:
	
	sp<DataSource> mSource;
	
	int32_t mSampleRate;   
	int32_t mNumChannels;    
	int32_t mBitsPerSample;
	
	off64_t mOffset;	
	int64_t mCurrentTimeUs;
	off64_t mFileSize;
	
    friend struct DtshdSource;
	sp<MetaData> mMeta;
    status_t mInitCheck;
	
	Vector<int64_t> mOffsetVector;	
	Vector<int64_t> mTimeStampVector;
	Vector<int32_t> mFrameLenVerctor;
	int64_t mDurationUs;
	int64_t mFrameCount;
	int64_t mFrameDecodedCnt;
	
    DtshdExtractor(const DtshdExtractor &);
    DtshdExtractor &operator=(const DtshdExtractor &);
     
};

#define DTSDEMUX_SYNCWORD_CORE_16M            0x7ffe8001
#define DTSDEMUX_SYNCWORD_CORE_14M            0x1fffe800
#define DTSDEMUX_SYNCWORD_CORE_24M            0xfe80007f    
#define DTSDEMUX_SYNCWORD_CORE_16             0xfe7f0180
#define DTSDEMUX_SYNCWORD_CORE_14             0xff1f00e8
#define DTSDEMUX_SYNCWORD_CORE_24             0x80fe7f01
#define DTSDEMUX_SYNCWORD_SUBSTREAM_M         0x64582025
#define DTSDEMUX_SYNCWORD_SUBSTREAM           0x58642520


#define DTS_SYNCWORD_AUX			0x9A1105A0

#define DTS_SYNCWORD_XCH		    0x5a5a5a5a
#define DTS_SYNCWORD_XXCH		    0x47004a03
#define DTS_SYNCWORD_X96K		    0x1d95f262
#define DTS_SYNCWORD_XBR		    0x655e315e
#define DTS_SYNCWORD_LBR		    0x0a801921
#define DTS_SYNCWORD_XLL		    0x41a29547
#define DTS_SYNCWORD_SUBSTREAM	    0x64582025
#define DTS_SYNCWORD_SUBSTREAMM		0x58642520
#define DTS_SYNCWORD_SUBSTREAM_CORE 0x02b09261
#define DTS_SYNCWORD_REV2AUX		0x7004C070

#define MAX_INPUT_BUF_SIZE  (40960)
#define DATA_LEN_READ_PER_TIME (40*1024)
/*************************************/
/* Bitstream Parser Data Structure   */
/*************************************/
typedef void (* PFNGETWORD)(void*);

typedef struct _tagDTSDemuxBitStrm{
    uint8_t*        pui8IpBuff;                    /* I/P Buffer Pointer */
    uint32_t        ui32IpBuffSize;                /* Input Buffer Size */
    uint32_t         ui32BitsLeft;                /* Bits left inside Cache */
    uint32_t         ui32Cache;                    /* 4 Bytes input Cache */
    uint32_t         ui32BuffOffset;                /* Offset for i/p Buffer */
    PFNGETWORD    pFpGetWord;                    /* Function pointer to read next word
                                               of data to cache, there are different
                                               implementations to read data in case 
                                               of big endian streams & cd format streams */
    uint32_t        ui32EOS;                    /* End of Stream */
    uint32_t        ui32ReadPos;                /* Circular Buffer Params Read Pointer*/
    uint32_t        ui32WritePos;                /* Cicrular Buffer Write Pointer */
    uint32_t        ui32StartRead;                /* Circular Buffer Start Pointer */
    uint32_t      ui32PBRFlag;                /* Flag Indicating PBR*/
}AmlDcaDemuxBs;

#define FSTABLE_SIZE    16
const uint32_t FSTBL[FSTABLE_SIZE]= {0,8000,
                 16000,32000,64000,128000,
                 11025,22050,44100,88200,
                 17640,12000,24000,48000,
                 96000,192000};
const uint8_t aui8NumCh[] = {1,2,2,2,2,3,3,4,4,5};

/* Function to intialize the Bitstream */
uint32_t AmlDcaDmuxRevBSInit(AmlDcaDemuxBs * pstBit,		   /* Bitstream context */
					uint8_t*	pui8InputBuffer,		/* InputBuffer from where to extract bits */
					uint32_t	ui32InputBufferLength,	  /* InputBuffer Length */
					uint32_t	ui32MaxCacheBits,		 /* Max bits that will be cached */
					uint32_t	ui32BigEndian );		/* Flag to indicate whether the bitstream is bigendian or not */


/* Function to intialize the Bitstream */
uint32_t AmlDcaDmuxBSInit(AmlDcaDemuxBs * pstBit, 		/* Bitstream context */
					uint8_t*	pui8InputBuffer,		/* InputBuffer from where to extract bits */
					uint32_t	ui32InputBufferLength,	  /* InputBuffer Length */
					uint32_t	ui32MaxCacheBits,		 /* Max bits that will be cached */
					uint32_t	ui32BigEndian );		/* Flag to indicate whether the bitstream is bigendian or not */

/*Function to Initialize Bitstream for each channel Set*/
uint32_t AmlDcaDmuxBSChannelsSetInit(AmlDcaDemuxBs * pstBit,	   /* Bitstream context */
							   uint8_t* pui8XLLStartPos,
							   uint32_t ui32XLLBuffOffset,
							   uint32_t ui32ChannelOffset); 							  
						
/* Function to extract bits from the stream */
uint32_t AmlDcaDmuxBSGetBits (AmlDcaDemuxBs * pstBit,    /* Bitstream context */
							  uint32_t i32Bits );			 /* BitsRequired */

/* Function to extract bits from the stream */
uint32_t AmlDcaDmuxRevBSGetBits (AmlDcaDemuxBs * pstBit,	  /* Bitstream context */
							  uint32_t i32Bits );			 /* BitsRequired */

/* Function to preview bits from the stream */
uint32_t AmlDcaDmuxBSShowBits (AmlDcaDemuxBs * pstBit,	/* Bitstream context */
							  uint32_t i32Bits );			 /* BitsRequired */

uint32_t AmlDcaDmuxBSEOS ( AmlDcaDemuxBs * pstBit);


/* Function to skip bits from the stream */
uint32_t AmlDcaDmuxRevBSSkipBits ( AmlDcaDemuxBs * pstBit,	/* Bitstream context */
							  uint32_t ui32SkipBits );			  /* Bits to skip */

/* Function to skip bits from the stream */
uint32_t AmlDcaDmuxBSSkipBits ( AmlDcaDemuxBs * pstBit,	 /* Bitstream context */
							  uint32_t ui32SkipBits );		  /* Bits to skip */

/* Function to flush remaining bits from cache used to word align the parsing*/
void AmlDcaDmuxBSFlush (AmlDcaDemuxBs * pstBit);		 /* Bitstream context */

/* Function to get the offset on stream */
uint32_t AmlDcaDmuxBSGetOffset ( AmlDcaDemuxBs * pstBit );		/* Bitstream context */

uint32_t AmlDcaDmuxBSGetOffset1 ( AmlDcaDemuxBs * pstBit );		 /* Bitstream context */

/* Function to read 28 bits in CD Format in Big Endian*/
void AmlDcaDmuxReadWordCDFBigEndian (void *);

/* Function to read 28 bits in CD Format in Little Endian*/
void AmlDcaDmuxReadWordCDFLittleEndian (void *);

/* Function to read 32 bits in Big Endian Format */
void AmlDcaDmuxReadWordBigEndian (void *);

/* Function to read 32 bits in Little Endian Format */
void AmlDcaDmuxReadWordLittleEndian (void *);

/* Function to ByteAlign the cache */
void AmlDcaDmuxBSByteAlign( AmlDcaDemuxBs *pstBit );

/* Function to skip specified number of bytes */
void AmlDcaDmuxBSSkipBytes( AmlDcaDemuxBs *pstBit, uint32_t ui32SkipBytes);

/* Function for bit stream intialisation pbr streams */
uint32_t AmlDcaDmuxXLLPBRInit(AmlDcaDemuxBs * pstBit,
						uint8_t*			pui8PBRBuff,
						uint32_t			ui32PBRBuffSize);

void AmlDcaDmuxReadWordPBRBigEndian(void * pVoid);
void AmlDcaDmuxReadWordPBRCDFBigEndian(void * pVoid);
void AmlDcaDmuxReadWordPBRLittleEndian(void * pVoid);
void AmlDcaDmuxReadWordPBRCDFLittleEndian(void * pVoid);
void AmlDcaDmuxReadBytes(uint8_t* pui8Buff, uint64_t* pui64Data,uint32_t ui32Bytes);

#if defined (DTSDEC_XXCH) || defined (DTSDEC_X96)
void AmlDcaDmuxBSReset(AmlDcaDemuxBs *pstBitStrm,uint32_t ui32ByteOffset);
#endif



bool SniffDcahd(
        const sp<DataSource> &source, String8 *mimeType, float *confidence,
        sp<AMessage> *);
}  // namespace android
#endif
