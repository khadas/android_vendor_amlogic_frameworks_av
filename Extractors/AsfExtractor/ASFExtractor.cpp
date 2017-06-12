
#define LOG_TAG "AsfExtractor"
#include <utils/Log.h>
#include "../include/AsfExtractor.h"

#include <cutils/properties.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/MediaBuffer.h>
#include <media/stagefright/MediaBufferGroup.h>
#ifdef WITH_AMLOGIC_MEDIA_EX_SUPPORT
#include <media/stagefright/AmMediaDefsExt.h>
#endif

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

struct AsfSource : public MediaSource {
    AsfSource(const sp<AsfExtractor> &extractor);

    virtual sp<MetaData> getFormat();

    virtual status_t start(MetaData *params = NULL);
    virtual status_t stop();

    virtual status_t read(
            MediaBuffer **buffer, const ReadOptions *options = NULL);

protected:
    virtual ~AsfSource();

private:
    sp<AsfExtractor> mExtractor;
    bool mStarted;

    AsfSource(const AsfSource &);
    AsfSource &operator=(const AsfSource &);
};




/***********************************************************************/
////////////////////////////////////////////////////////////////////////////////
AsfSource::AsfSource(const sp<AsfExtractor> &extractor)
    : mExtractor(extractor),
      mStarted(false) {
}

AsfSource::~AsfSource() {
    if (mStarted) {
        stop();
    }
}
sp<MetaData> AsfSource::getFormat() {
    return mExtractor->getMetaData();
}

status_t AsfSource::start(MetaData *params) {
    if (mStarted) {
        return INVALID_OPERATION;
    }

    mStarted = true;

    return OK;
}

status_t AsfSource::stop() {
    mStarted = false;

    return OK;
}



status_t AsfSource::read(
        MediaBuffer **out, const ReadOptions *options) {
    *out = NULL;

    int64_t seekTimeUs;
    ReadOptions::SeekMode mode;
    if (options && options->getSeekTo(&seekTimeUs, &mode)) {
        if (mExtractor->seekToTime(seekTimeUs) != OK) {
            return ERROR_END_OF_STREAM;
        }
    }    	

	MediaBuffer *packet; 
	AVPacket pkt;//={0};
	int i;
	//---------------------------
	//note: may cause bug!!!
	while(1){
		int ret=mExtractor->av_read_frame(mExtractor->ic, &pkt);
		if(ret<0)
		{
			 return ERROR_END_OF_STREAM;
		}
		if(pkt.stream_index == mExtractor->audio_index){
			 break;
		}
	}
	packet=new MediaBuffer(pkt.size);
	memcpy(packet->data(), pkt.data, pkt.size);
    packet->set_range(0, pkt.size);
	//mExtractor->mOffset+=pkt.size;
	//---------------------------
#if 0
    int64_t timeUs;
    if (packet->meta_data()->findInt64(kKeyTime, &timeUs)) {
        ALOGI("found time = %lld us", timeUs);
    } else {
        ALOGI("NO time");
    }
#endif
    
    packet->meta_data()->setInt64(kKeyTime, pkt.pts);
    //int64_t pts_tmp;
	//packet->meta_data()->findInt64(kKeyTime, &pts_tmp);
    packet->meta_data()->setInt32(kKeyIsSyncFrame, 1);
    *out = packet;
    return OK;
}

////////////////////////////////////////////////////////////////////////////////

//extern "C"
//{
static void *av_malloc(unsigned int size)
{
    void *ptr;
#if defined (HAVE_MEMALIGN)
    ptr = memalign(16,size);
    /* Why 64? 
       Indeed, we should align it:
         on 4 for 386
         on 16 for 486
	 on 32 for 586, PPro - k6-III
	 on 64 for K7 (maybe for P3 too).
       Because L1 and L2 caches are aligned on those values.
       But I don't want to code such logic here!
     */
     /* Why 16?
        because some cpus need alignment, for example SSE2 on P4, & most RISC cpus
        it will just trigger an exception and the unaligned load will be done in the
        exception handler or it will just segfault (SSE2 on P4)
        Why not larger? because i didnt see a difference in benchmarks ...
     */
     /* benchmarks with p3
        memalign(64)+1		3071,3051,3032
        memalign(64)+2		3051,3032,3041
        memalign(64)+4		2911,2896,2915
        memalign(64)+8		2545,2554,2550
        memalign(64)+16		2543,2572,2563
        memalign(64)+32		2546,2545,2571
        memalign(64)+64		2570,2533,2558
        
        btw, malloc seems to do 8 byte alignment by default here
     */
#else
    ptr = malloc(size);
#endif
    return ptr;
}

static void *av_mallocz(unsigned int size)
{
    void *ptr;
    
    ptr = av_malloc(size);
    if (!ptr)
        return NULL;
    memset(ptr, 0, size);
    return ptr;
}


/* NOTE: ptr = NULL is explicetly allowed */
static void av_free(void *ptr)
{
    /* XXX: this test should not be needed on most libcs */
    if (ptr)
        free(ptr);
}

/* cannot call it directly because of 'void **' casting is not automatic */
static void __av_freep(void **ptr)
{
    av_free(*ptr);
    *ptr = NULL;
}

#define av_freep(p) __av_freep((void **)(p))

static void *av_realloc(void *ptr, unsigned int size)
{
    return (void *)realloc(ptr, size);
}
//}



/***********************************************************************/


static const GUID asf_header = 
{
    0x75B22630, 0x668E, 0x11CF, { 0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C },
};

static const GUID file_header = 
{
    0x8CABDCA1, 0xA947, 0x11CF, { 0x8E, 0xE4, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65 },
};

static const GUID stream_header = 
{
    0xB7DC0791, 0xA9B7, 0x11CF, { 0x8E, 0xE6, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65 },
};

static const GUID audio_stream = 
{
    0xF8699E40, 0x5B4D, 0x11CF, { 0xA8, 0xFD, 0x00, 0x80, 0x5F, 0x5C, 0x44, 0x2B },
};

static const GUID audio_conceal_none = 
{
    // 0x49f1a440, 0x4ece, 0x11d0, { 0xa3, 0xac, 0x00, 0xa0, 0xc9, 0x03, 0x48, 0xf6 },
    // New value lifted from avifile
    0x20fb5700, 0x5b55, 0x11cf, { 0xa8, 0xfd, 0x00, 0x80, 0x5f, 0x5c, 0x44, 0x2b },
};

static const GUID video_stream = 
{
    0xBC19EFC0, 0x5B4D, 0x11CF, { 0xA8, 0xFD, 0x00, 0x80, 0x5F, 0x5C, 0x44, 0x2B },
};

static const GUID video_conceal_none = 
{
    0x20FB5700, 0x5B55, 0x11CF, { 0xA8, 0xFD, 0x00, 0x80, 0x5F, 0x5C, 0x44, 0x2B },
};


static const GUID comment_header = 
{
    0x75b22633, 0x668e, 0x11cf, { 0xa6, 0xd9, 0x00, 0xaa, 0x00, 0x62, 0xce, 0x6c },
};

static const GUID codec_comment_header = 
{
    0x86D15240, 0x311D, 0x11D0, { 0xA3, 0xA4, 0x00, 0xA0, 0xC9, 0x03, 0x48, 0xF6 },
};
static const GUID codec_comment1_header =
{
    0x86d15241, 0x311d, 0x11d0, { 0xa3, 0xa4, 0x00, 0xa0, 0xc9, 0x03, 0x48, 0xf6 },
};

static const GUID data_header =
{
    0x75b22636, 0x668e, 0x11cf, { 0xa6, 0xd9, 0x00, 0xaa, 0x00, 0x62, 0xce, 0x6c },
};

static const GUID index_guid = 
{
    0x33000890, 0xe5b1, 0x11cf, { 0x89, 0xf4, 0x00, 0xa0, 0xc9, 0x03, 0x49, 0xcb },
};

static const GUID head1_guid = 
{
    0x5fbf03b5, 0xa92e, 0x11cf, { 0x8e, 0xe3, 0x00, 0xc0, 0x0c, 0x20, 0x53, 0x65 },
};

static const GUID head2_guid = 
{
    0xabd3d211, 0xa9ba, 0x11cf, { 0x8e, 0xe6, 0x00, 0xc0, 0x0c, 0x20, 0x53, 0x65 },
};

static const GUID extended_content_header =
{
        0xD2D0A440, 0xE307, 0x11D2, { 0x97, 0xF0, 0x00, 0xA0, 0xC9, 0x5E, 0xA8, 0x50 },
};

/* I am not a number !!! This GUID is the one found on the PC used to
   generate the stream */
static const GUID my_guid =
{
    0, 0, 0, { 0, 0, 0, 0, 0, 0, 0, 0 },
};


static const CodecTag codec_wav_tags[] =
{
    { CODEC_ID_MP2, 0x50, 0 },
    { CODEC_ID_MP3, 0x55, 0 },
    { CODEC_ID_AC3, 0x2000 },
    { CODEC_ID_PCM_S16LE, 0x01, 0 },
    { CODEC_ID_PCM_U8, 0x01, 0 }, /* must come after s16le in this list */
    { CODEC_ID_PCM_ALAW, 0x06, 0 },
    { CODEC_ID_PCM_MULAW, 0x07, 0 },
    { CODEC_ID_ADPCM_MS, 0x02, 0 },
    { CODEC_ID_ADPCM_IMA_WAV, 0x11, 0 },
    { CODEC_ID_ADPCM_IMA_DK4, 0x61, 0 },  /* rogue format number */
    { CODEC_ID_ADPCM_IMA_DK3, 0x62, 0 },  /* rogue format number */
    { CODEC_ID_WMAV1, 0x160, 0 },
    { CODEC_ID_WMAV2, 0x161, 0 },
    { CODEC_ID_WMAPRO, 0x0162, 0 },
    /* HACK/FIXME: Does Vorbis in WAV/AVI have an (in)official ID? */
    { CODEC_ID_VORBIS, ('V' << 8) + 'o', 0 },
    { 0, 0, 0 },
};

static int/*enum CodecID */codec_get_id(const CodecTag *tags, unsigned int tag)
{
    while (tags->id != 0)
	{
        if(   toupper((tag >> 0)&0xFF) == toupper((tags->tag >> 0)&0xFF)
           && toupper((tag >> 8)&0xFF) == toupper((tags->tag >> 8)&0xFF)
           && toupper((tag >>16)&0xFF) == toupper((tags->tag >>16)&0xFF)
           && toupper((tag >>24)&0xFF) == toupper((tags->tag >>24)&0xFF))
            return tags->id;
        tags++;
    }
    return CODEC_ID_NONE;
}

static AVInputFormat *first_iformat=NULL;
static AVOutputFormat *first_oformat=NULL;
static AVImageFormat *first_image_format=NULL;


static int wav_codec_get_id(unsigned int tag, int bps)
{
    int id;
    id = codec_get_id(codec_wav_tags, tag);
    if (id <= 0)
        return id;
    /* handle specific u8 codec */
    if (id == CODEC_ID_PCM_S16LE && bps == 8)
        id = CODEC_ID_PCM_U8;
    return id;
}


/* XXX: must be called before any I/O */
static int url_setbufsize(ByteIOContext *s, int buf_size)
{
    uint8_t *buffer;
    buffer = (uint8_t *)av_malloc(buf_size);
    if (!buffer)
        //return -ENOMEM;  
        return -1; //jacky 2006/10/18	 

    av_free(s->buffer);
    s->buffer = buffer;
    s->buffer_size = buf_size;
    s->buf_ptr = buffer;
    if (!s->write_flag) 
        s->buf_end = buffer;
    else
        s->buf_end = buffer + buf_size;
    return 0;
}


static int get_frame_filename(char *buf, int buf_size,
                       const char *path, int number)
{
    const char *p;
    char *q, buf1[20], c;
    int nd, len, percentd_found;
	
    q = buf;
    p = path;
    percentd_found = 0;
    for(;;)
	{
        c = *p++;
        if (c == '\0')
            break;
        if (c == '%') 
		{
            do {
                nd = 0;
                while (isdigit(*p))
				{
                    nd = nd * 10 + *p++ - '0';
                }
                c = *p++;
            } while (isdigit(c));
			
            switch(c) 
			{
            case '%':
                goto addchar;
            case 'd':
                if (percentd_found)
                    goto fail;
                percentd_found = 1;
                printf(buf1, sizeof(buf1), "%0*d", nd, number);
                len = strlen(buf1);
                if ((q - buf + len) > buf_size - 1)
                    goto fail;
                memcpy(q, buf1, len);
                q += len;
                break;
            default:
                goto fail;
            }
        } 
		else 
		{
addchar:
		if ((q - buf) < buf_size - 1)
			*q++ = c;
        }
    }
    if (!percentd_found)
        goto fail;
    *q = '\0';
    return 0;
fail:
    *q = '\0';
    return -1;
}

static int filename_number_test(const char *filename)
{
    char buf[1024];
    return get_frame_filename(buf, sizeof(buf), filename, 1);
}

static int match_ext(const char *filename, const char *extensions)
{
    const char *ext, *p;
    char ext1[32], *q;
	
    ext = strrchr(filename, '.');
    if (ext) 
	{
        ext++;
        p = extensions;
        for(;;) 
		{
            q = ext1;
            while (*p != '\0' && *p != ',') 
                *q++ = *p++;
            *q = '\0';
			//    if (!strcasecmp(ext1, ext))  //jacky 2006/10/18
			//         return 1;
            if (*p == '\0') 
                break;
            p++;
        }
    }
    return 0;
}

static int asf_probe(AVProbeData *pd);
/* guess file format */
static AVInputFormat *av_probe_input_format(AVProbeData *pd, int is_opened)
{
    AVInputFormat *fmt1, *fmt;
    int score, score_max;
	
    fmt = NULL;
    score_max = 0;
    for(fmt1 = first_iformat; fmt1 != NULL; fmt1 = fmt1->next)
	{
        if (!is_opened && !(fmt1->flags & AVFMT_NOFILE))
            continue;
        score = 0;
        if (1/*fmt1->read_probe*/) 
		{
            score = asf_probe(pd);
        } 
		else if (fmt1->extensions) 
		{
            if (match_ext(pd->filename, fmt1->extensions)) 
			{
                score = 50;
            }
        } 
        if (score > score_max) 
		{
            score_max = score;
            fmt = fmt1;
        }
    }
    return fmt;
}



#define PROBE_BUF_SIZE 2048
int AsfExtractor::av_open_input_file(AVFormatContext **ic_ptr, const char *filename, 
                       AVInputFormat *fmt,
                       int buf_size,
                       AVFormatParameters *ap)
{
    int err, must_open_file, file_opened;
    uint8_t buf[PROBE_BUF_SIZE];
    AVProbeData probe_data, *pd = &probe_data;
    ByteIOContext pb1, *pb = &pb1;
    
    file_opened = 0;
    pd->filename = "";
    if (filename)
        pd->filename = filename;
    pd->buf = buf;
    pd->buf_size = 0;
	
    if (0/*!fmt*/) 
	{
        /* guess format if no file can be opened  */
        fmt = av_probe_input_format(pd, 0);
    }
    /* do not open file if the format does not need it. XXX: specific
	hack needed to handle RTSP/TCP */
    must_open_file = 1;
    if (fmt && (fmt->flags & AVFMT_NOFILE)) 
	{
        must_open_file = 0;
    }
	
    if (!fmt || must_open_file) 
	{
        /* if no file needed do not try to open one */
        if (url_fopen(pb, filename, URL_RDONLY) < 0) 
		{
            err = AVERROR_IO;
            goto fail;
        }
        file_opened = 1;
        if (buf_size > 0)
		{
            url_setbufsize(pb, buf_size);
        }
        if (!fmt)
		{
            /* read probe data */
            pd->buf_size = get_buffer(pb, buf, PROBE_BUF_SIZE);
            url_fseek(pb, 0, SEEK_SET);
        }
    }
    
    /* guess file format */
    if (0/*!fmt*/) 
	{
        fmt = av_probe_input_format(pd, 1);
    }
	
    /* if still no format found, error */
    if (0/*!fmt*/) 
	{
        err = AVERROR_NOFMT;
        goto fail;
    }
	
    /* XXX: suppress this hack for redirectors */
	
	
    /* check filename in case of an image number is expected */
    if (0/*fmt->flags & AVFMT_NEEDNUMBER*/) 
	{
        if (filename_number_test(filename) < 0) 
		{ 
            err = AVERROR_NUMEXPECTED;
            goto fail;
        }
    }
    err = av_open_input_stream(ic_ptr, pb, filename, fmt, ap);
    if (err)
        goto fail;
    return 0;
fail:
    if (file_opened)
        url_fclose(pb);
    *ic_ptr = NULL;
    return err;
    
}

static int url_get_max_packet_size(URLContext *h)
{
    return h->max_packet_size;
}

static int init_put_byte(ByteIOContext *s,
                  unsigned char *buffer,
                  int buffer_size,
                  int write_flag,
                  void *opaque,
                  int (*read_packet)(void *opaque, uint8_t *buf, int buf_size),
                  void (*write_packet)(void *opaque, uint8_t *buf, int buf_size),
                  int (*seek)(void *opaque, offset_t offset, int whence))
{
    s->buffer = buffer;
    s->buffer_size = buffer_size;
    s->buf_ptr = buffer;
    s->write_flag = write_flag;
    if (!s->write_flag) 
        s->buf_end = buffer;
    else
        s->buf_end = buffer + buffer_size;
    s->opaque = opaque;
    s->write_packet = write_packet;
    s->read_packet = read_packet;
    s->seek = seek;
    s->pos = 0;
    s->must_flush = 0;
    s->eof_reached = 0;
    s->is_streamed = 0;
    s->max_packet_size = 0;
    return 0;
}
                  

static int url_fdopen(ByteIOContext *s, URLContext *h)
{
    uint8_t *buffer;
    int buffer_size, max_packet_size;

    
    max_packet_size = url_get_max_packet_size(h);
    if (max_packet_size)
	{
        buffer_size = max_packet_size; /* no need to bufferize more than one packet */
    } 
	else
	{
        buffer_size = IO_BUFFER_SIZE;
    }
    buffer =(uint8_t*) av_malloc(buffer_size);
    if (!buffer)
        //return -ENOMEM;
	    return -1; //jacky 2006/10/18

    if (init_put_byte(s, buffer, buffer_size, 
                      (h->flags & URL_WRONLY) != 0, h,
                      NULL/*url_read_packet*/, url_write_packet, NULL/*url_seek_packet*/) < 0)
	{
        av_free(buffer);
        //return -EIO;
	    return -1; //jacky 2006/10/18
    }
    s->is_streamed = h->is_streamed;
    s->max_packet_size = max_packet_size;
    return 0;
}

int AsfExtractor::url_fopen(ByteIOContext *s, const char *filename, int flags)
{
    URLContext *h;
    int err;
    err = url_open(&h, filename, flags);
    if (err < 0)
        return err;
    err = url_fdopen(s, h);
    if (err < 0) 
	{
        url_close(h);
        return err;
    }
    return 0;
}


int AsfExtractor::url_open(URLContext **puc, const char *filename, int flags)
{   
    URLContext *uc;
    URLProtocol *up=NULL;
    const char *p;
    char proto_str[128], *q;
    int err;

    p = filename;
    q = proto_str;
    while (0/**p != '\0' && *p != ':'*/) 
	{
        /* protocols can only contain alphabetic chars */
        if (!isalpha(*p))
            goto file_proto;
        if ((q - proto_str) < sizeof(proto_str) - 1)
            *q++ = *p;
        p++;
    }
    /* if the protocol has length 1, we consider it is a dos drive */
    if (0/**p == '\0' || (q - proto_str) <= 1*/) 
	{
		file_proto:
        strcpy(proto_str, "file");
    } 
	else 
	{
        *q = '\0';
    }
    
    //up = first_protocol;
    while (0/*up != NULL*/) 
	{
        if (!strcmp(proto_str, up->name))
            goto found;
        up = up->next;
    }
    //err = -ENOENT; 
	//err = -1 ; //jacky 2006/10/18
    //goto fail;
 found:
    uc = (URLContext*)av_malloc(sizeof(URLContext) /*+ strlen(filename)*/);//bug---->strlen(filename)
    if (!uc) 
	{
       // err = -ENOMEM;
		err = -1; //jacky 2006/10/18
        goto fail;
    }
	
	//bug----->strcpy(uc->filename, filename);
    //strcpy(uc->filename, filename);
    uc->prot = up;
    uc->flags = flags;
    uc->is_streamed = 0; /* default = not streamed */
    uc->max_packet_size = 0; /* default: stream file */
    err = /*up->url_open*/file_open(uc, filename, flags);
    if (err < 0) 
	{
        av_free(uc);
        *puc = NULL;
        return err;
    }
	
    *puc = uc;
    return 0;
 fail:
    *puc = NULL;
    return err;
}



int AsfExtractor::get_buffer(ByteIOContext *s, unsigned char *buf, int size)
{
    int len, size1;

    size1 = size;
    while (size > 0)
	{
        len = s->buf_end - s->buf_ptr;
        if (len > size)
            len = size;
        if (len == 0)
		{
            fill_buffer(s);
            len = s->buf_end - s->buf_ptr;
            if (len == 0)
                break;
        } 
		else 
		{
            memcpy(buf, s->buf_ptr, len);
            buf += len;
            s->buf_ptr += len;
            size -= len;
        }
    }
    return size1 - size;
}

void AsfExtractor::fill_buffer(ByteIOContext *s)
{
    int len;

    /* no need to do anything if EOF already reached */
    if (s->eof_reached)
        return;
    len = file_read/*s->read_packet*/((URLContext *)(s->opaque), s->buffer, s->buffer_size);

    if (len <= 0)
	{
        /* do not modify buffer if EOF reached so that a seek back can
           be done without rereading data */
        s->eof_reached = 1;
    } 
	else 
	{
        s->pos += len;
        s->buf_ptr = s->buffer;
        s->buf_end = s->buffer + len;
    }
}

static void pstrcpy(char *buf, int buf_size, const char *str)
{
    int c;
    char *q = buf;

    if (buf_size <= 0)
        return;

    for(;;) 
	{
        c = *str++;
        if (c == 0 || q >= buf + buf_size - 1)
            break;
        *q++ = c;
    }
    *q = '\0';
}

static void av_set_pts_info(AVFormatContext *s, int pts_wrap_bits,
                     int pts_num, int pts_den)
{
    s->pts_wrap_bits = pts_wrap_bits;
    s->pts_num = pts_num;
    s->pts_den = pts_den;
}

int AsfExtractor::av_open_input_stream(AVFormatContext **ic_ptr, 
                         ByteIOContext *pb, const char *filename, 
                         AVInputFormat *fmt, AVFormatParameters *ap)
{
    int err;
    AVFormatContext *ic;
    ic = (AVFormatContext *)av_mallocz(sizeof(AVFormatContext));
    if (!ic) 
	{
        err = AVERROR_NOMEM;
        goto fail;
    }
    ic->iformat = fmt;
    if (pb)
        ic->pb = *pb;
    ic->duration = AV_NOPTS_VALUE;
    ic->start_time = AV_NOPTS_VALUE;
	
	//bug--> pstrcpy(ic->filename, sizeof(ic->filename), filename); when filename=NULL
    //pstrcpy(ic->filename, sizeof(ic->filename), filename);
	
    /* allocate private data */
    if (/*fmt->priv_data_size > */1) 
	{
        ic->priv_data = av_mallocz(sizeof(ASFContext)/*fmt->priv_data_size*/);
        if (!ic->priv_data) 
		{
            err = AVERROR_NOMEM;
            goto fail;
        }
    } 
	else 
	{
        ic->priv_data = NULL;
    }
	
    /* default pts settings is MPEG like */
    av_set_pts_info(ic, 33, 1, 90000);
    ic->last_pkt_pts = AV_NOPTS_VALUE;
    ic->last_pkt_dts = AV_NOPTS_VALUE;
    ic->last_pkt_stream_pts = AV_NOPTS_VALUE;
    ic->last_pkt_stream_dts = AV_NOPTS_VALUE;
    
    err = asf_read_header/*ic->iformat->read_header*/(ic, ap);

    if (err < 0)
        goto fail;
	
    if (pb)
        ic->data_offset = url_ftell(&ic->pb);
    *ic_ptr = ic;
    return 0;
fail:
    if (ic) 
	{
        av_freep(&ic->priv_data);
    }
    av_free(ic);
    *ic_ptr = NULL;
    return err;
}

static void avcodec_default_release_buffer(AVCodecContext *s, AVFrame *pic)
{
    int i;
    InternalBuffer *buf, *last, temp;

    assert(pic->type==FF_BUFFER_TYPE_INTERNAL);
    assert(s->internal_buffer_count);

    buf = NULL; /* avoids warning */
    for(i=0; i<s->internal_buffer_count; i++)
	{ //just 3-5 checks so is not worth to optimize
        buf= &((InternalBuffer*)s->internal_buffer)[i];
        if(buf->data[0] == pic->data[0])
            break;
    }
    assert(i < s->internal_buffer_count);
    s->internal_buffer_count--;
    last = &((InternalBuffer*)s->internal_buffer)[s->internal_buffer_count];

    temp= *buf;
    *buf= *last;
    *last= temp;

    for(i=0; i<3; i++)
	{
        pic->data[i]=NULL;
//        pic->base[i]=NULL;
    }
//printf("R%X\n", pic->opaque);
}

static enum PixelFormatP avcodec_default_get_format(struct AVCodecContext *s, enum PixelFormatP * fmt)
{
    return fmt[0];
}

static void avcodec_get_context_defaults(AVCodecContext *s)
{
    s->bit_rate= 800*1000;
    s->bit_rate_tolerance= s->bit_rate*10;
    s->qmin= 2;
    s->qmax= 31;
    s->mb_qmin= 2;
    s->mb_qmax= 31;
    s->rc_eq= "tex^qComp";
    s->qcompress= 0.5;
    s->max_qdiff= 3;
    s->b_quant_factor=1.25;
    s->b_quant_offset=1.25;
    s->i_quant_factor=-0.8;
    s->i_quant_offset=0.0;
    s->error_concealment= 3;
    s->error_resilience= 1;
    s->workaround_bugs= FF_BUG_AUTODETECT;
    s->frame_rate_base= 1;
    s->frame_rate = 25;
    s->gop_size= 50;
    s->me_method= ME_EPZS;
    //s->get_buffer= avcodec_default_get_buffer;
    s->release_buffer= avcodec_default_release_buffer;
    s->get_format= avcodec_default_get_format;
    s->me_subpel_quality=8;
    s->lmin= FF_QP2LAMBDA * s->qmin;
    s->lmax= FF_QP2LAMBDA * s->qmax;
    //s->sample_aspect_ratio= (AVRational){0,1};
    s->ildct_cmp= FF_CMP_VSAD;
    
    s->intra_quant_bias= FF_DEFAULT_QUANT_BIAS;
    s->inter_quant_bias= FF_DEFAULT_QUANT_BIAS;
    s->palctrl = NULL;
    //s->reget_buffer= avcodec_default_reget_buffer;
}

AVStream * AsfExtractor::av_new_stream(AVFormatContext *s, int id)
{
    AVStream *st;
	
    if (s->nb_streams >= MAX_STREAMS)
        return NULL;
	
    st = (AVStream *)av_mallocz(sizeof(AVStream));
    if (!st)
        return NULL;
    avcodec_get_context_defaults(&st->codec);
    if (s->iformat)
	{
        /* no default bitrate if decoding */
        st->codec.bit_rate = 0;
    }
    st->index = s->nb_streams;
    st->id = id;
    st->start_time = AV_NOPTS_VALUE;
    st->duration = AV_NOPTS_VALUE;
    s->streams[s->nb_streams++] = st;
    return st;
}

int AsfExtractor::asf_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    ASFContext *asf =(ASFContext *) (s->priv_data);
    GUID g;
    ByteIOContext *pb = &s->pb;
    AVStream *st;
    ASFStream *asf_st;
    //int size, i;
    int i;
    int64_t gsize;

    av_set_pts_info(s, 32, 1, 1000); /* 32 bit pts in ms */

    get_guid(pb, &g);
    if (memcmp(&g, &asf_header, sizeof(GUID)))
        goto fail;
    get_le64(pb);
    get_le32(pb);
    get_byte(pb);
    get_byte(pb);
    memset(&asf->asfid2avid, -1, sizeof(asf->asfid2avid));
    for(;;) 
	{
        get_guid(pb, &g);
        gsize = get_le64(pb);
#if 0
        printf("%08Lx: ", url_ftell(pb) - 24);
        print_guid(&g);
        printf("  size=0x%Lx\n", gsize);
#endif
        if (gsize < 24)
            goto fail;
        if (!memcmp(&g, &file_header, sizeof(GUID))) 
		{
            get_guid(pb, &asf->hdr.guid);
			asf->hdr.file_size		= get_le64(pb);
			asf->hdr.create_time	= get_le64(pb);
			asf->hdr.packets_count	= get_le64(pb);
			asf->hdr.play_time		= get_le64(pb);
			asf->hdr.send_time		= get_le64(pb);
			asf->hdr.preroll		= get_le32(pb);
			asf->hdr.ignore		= get_le32(pb);
			asf->hdr.flags		= get_le32(pb);
			asf->hdr.min_pktsize	= get_le32(pb);
			asf->hdr.max_pktsize	= get_le32(pb);
			asf->hdr.max_bitrate	= get_le32(pb);
			asf->packet_size = asf->hdr.max_pktsize;
            asf->nb_packets = asf->hdr.packets_count;
        } 
		else if (!memcmp(&g, &stream_header, sizeof(GUID))) 
		{
            int type, total_size, type_specific_size;
            //unsigned int tag1;
            int64_t pos1, pos2;

            pos1 = url_ftell(pb);

            st = av_new_stream(s, 0);
            if (!st)
                goto fail;
            asf_st =(ASFStream *) av_mallocz(sizeof(ASFStream));
            if (!asf_st)
                goto fail;
            st->priv_data = asf_st;
            st->start_time = asf->hdr.preroll / (10000000 / AV_TIME_BASE);
			st->duration = (asf->hdr.send_time - asf->hdr.preroll) / 
                (10000000 / AV_TIME_BASE);
            get_guid(pb, &g);
            if (!memcmp(&g, &audio_stream, sizeof(GUID)))
			{
                type = CODEC_TYPE_AUDIO;
            } 
			else if (!memcmp(&g, &video_stream, sizeof(GUID)))
			{
                type = CODEC_TYPE_VIDEO;
            } 
			else 
			{
                goto fail;
            }
            get_guid(pb, &g);
            total_size =(int) get_le64(pb);
            type_specific_size = get_le32(pb);
            get_le32(pb);
			st->id = get_le16(pb) & 0x7f; /* stream id */
            // mapping of asf ID to AV stream ID;
            asf->asfid2avid[st->id] = s->nb_streams - 1;

            get_le32(pb);
			st->codec.codec_type = (CodecType)type;
            /* 1 fps default (XXX: put 0 fps instead) */
            st->codec.frame_rate = 1; 
            st->codec.frame_rate_base = 1;
            if (type == CODEC_TYPE_AUDIO)
			{
                get_wav_header(pb, &st->codec, type_specific_size);
                st->need_parsing = 1;
				/* We have to init the frame size at some point .... */
				pos2 = url_ftell(pb);
				if (gsize > (pos2 + 8 - pos1 + 24))
				{
					asf_st->ds_span = get_byte(pb);
					asf_st->ds_packet_size = get_le16(pb);
					asf_st->ds_chunk_size = get_le16(pb);
					asf_st->ds_data_size = get_le16(pb);
					asf_st->ds_silence_data = get_byte(pb);
				}
				//printf("Descrambling: ps:%d cs:%d ds:%d s:%d  sd:%d\n",
				//       asf_st->ds_packet_size, asf_st->ds_chunk_size,
				//       asf_st->ds_data_size, asf_st->ds_span, asf_st->ds_silence_data);
				if (asf_st->ds_span > 1)
				{
					if (!asf_st->ds_chunk_size
					|| (asf_st->ds_packet_size/asf_st->ds_chunk_size <= 1))
					asf_st->ds_span = 0; // disable descrambling
				}
				switch (st->codec.codec_id) 
				{
					case CODEC_ID_MP3:
						st->codec.frame_size = MPA_FRAME_SIZE;
						break;
					case CODEC_ID_PCM_S16LE:
					case CODEC_ID_PCM_S16BE:
					case CODEC_ID_PCM_U16LE:
					case CODEC_ID_PCM_U16BE:
					case CODEC_ID_PCM_S8:
					case CODEC_ID_PCM_U8:
					case CODEC_ID_PCM_ALAW:
					case CODEC_ID_PCM_MULAW:
						st->codec.frame_size = 1;
						break;
					default:
						/* This is probably wrong, but it prevents a crash later */
						st->codec.frame_size = 1;
					break;
				}
			}
            pos2 = url_ftell(pb);
            url_fskip(pb, gsize - (pos2 - pos1 + 24));
        } 
		else if (!memcmp(&g, &data_header, sizeof(GUID))) 
		{
            break;
        } 
		else if (!memcmp(&g, &comment_header, sizeof(GUID))) 
		{
            int len1, len2, len3, len4, len5;

            len1 = get_le16(pb);
            len2 = get_le16(pb);
            len3 = get_le16(pb);
            len4 = get_le16(pb);
            len5 = get_le16(pb);
            get_str16_nolen(pb, len1, s->title, sizeof(s->title));
            get_str16_nolen(pb, len2, s->author, sizeof(s->author));
            get_str16_nolen(pb, len3, s->copyright, sizeof(s->copyright));
            get_str16_nolen(pb, len4, s->comment, sizeof(s->comment));
			url_fskip(pb, len5);
        } 
		else if (!memcmp(&g, &extended_content_header, sizeof(GUID)))
		{
            int desc_count, i;

            desc_count = get_le16(pb);
            for(i=0;i<desc_count;i++)
            {
                int name_len,value_type,value_len,value_num = 0;
                char *name, *value;

                name_len = get_le16(pb);
                name = (char *)av_mallocz(name_len);
                get_str16_nolen(pb, name_len, name, name_len);
                value_type = get_le16(pb);
                value_len = get_le16(pb);
                if ((value_type == 0) || (value_type == 1)) // unicode or byte
                {
                    value = (char *)av_mallocz(value_len);
                    get_str16_nolen(pb, value_len, value, value_len);
                    if (strcmp(name,"WM/AlbumTitle")==0) 
					{ 
						strcpy(s->album, value); 
					}
                    if (strcmp(name,"WM/Genre")==0) 
					{ 
						strcpy(s->genre, value); 
					}
                    if (strcmp(name,"WM/Year")==0)
						s->year = atoi(value);
                    av_free(value);
                }
                if ((value_type >= 2) || (value_type <= 5)) // boolean or DWORD or QWORD or WORD
                {
                    if (value_type==2) value_num = get_le32(pb);
                    if (value_type==3) value_num = get_le32(pb);
                    if (value_type==4) value_num = (int)get_le64(pb);
                    if (value_type==5) value_num = get_le16(pb);
                    if (strcmp(name,"WM/Track")==0) s->track = value_num + 1;
                    if (strcmp(name,"WM/TrackNumber")==0) s->track = value_num;
                }
                av_free(name);
            }

        } 
		else if (url_feof(pb)) 
		{
            goto fail;
        }
		else 
		{
            url_fseek(pb, gsize - 24, SEEK_CUR);
        }
    }
    get_guid(pb, &g);
    get_le64(pb);
    get_byte(pb);
    get_byte(pb);
    if (url_feof(pb))
        goto fail;
    asf->data_offset = url_ftell(pb);
    asf->packet_size_left = 0;

    return 0;

 fail:
     for(i=0;i<s->nb_streams;i++) 
	 {
        AVStream *st = s->streams[i];
		if (st) 
		{
			av_free(st->priv_data);
			av_free(st->codec.extradata);
		}
        av_free(st);
    }
    return -1;
}

int AsfExtractor::get_byte(ByteIOContext *s)
{
    if (s->buf_ptr < s->buf_end) 
	{
        return *s->buf_ptr++;
    } 
	else 
	{
        fill_buffer(s);
        if (s->buf_ptr < s->buf_end)
            return *s->buf_ptr++;
        else
            return 0;
    }
}

unsigned int AsfExtractor::get_le16(ByteIOContext *s)
{
    unsigned int val;
    val = get_byte(s);
    val |= get_byte(s) << 8;
    return val;
}

unsigned int AsfExtractor::get_le32(ByteIOContext *s)
{
    unsigned int val;
    val = get_byte(s);
    val |= get_byte(s) << 8;
    val |= get_byte(s) << 16;
    val |= get_byte(s) << 24;
    return val;
}

uint64_t AsfExtractor::get_le64(ByteIOContext *s)
{
    uint64_t val;
    val = (uint64_t)get_le32(s);
    val |= (uint64_t)get_le32(s) << 32;
    return val;
}

void AsfExtractor::get_str16_nolen(ByteIOContext *pb, int len, char *buf, int buf_size)
{
    int c, lenz;
    char *q;

    q = buf;
    lenz = len;
    while (len > 0)
	{
        c = get_byte(pb);
        if ((q - buf) < buf_size-1)
            *q++ = c;
        len--;
    }
//    tag_recode(buf, lenz);  //jacky 2006/10/18
}

void AsfExtractor::get_guid(ByteIOContext *s, GUID *g)
{
    int i;

    g->v1 = get_le32(s);
    g->v2 = get_le16(s);
    g->v3 = get_le16(s);
    for(i=0;i<8;i++)
        g->v4[i] = get_byte(s);
}

void AsfExtractor::get_wav_header(ByteIOContext *pb, AVCodecContext *codec, int size)
{
    int id;

    id = get_le16(pb);
    codec->codec_type = CODEC_TYPE_AUDIO;
    codec->codec_tag = id;
    codec->channels = get_le16(pb);
    codec->sample_rate = get_le32(pb);
    codec->bit_rate = get_le32(pb) * 8;
    codec->block_align = get_le16(pb);
    if (size == 14)
	{  /* We're dealing with plain vanilla WAVEFORMAT */
        codec->bits_per_sample = 8;
    }
	else
        codec->bits_per_sample = get_le16(pb);
    codec->codec_id =(CodecID) wav_codec_get_id(id, codec->bits_per_sample);

    if (size > 16) 
	{  /* We're obviously dealing with WAVEFORMATEX */
        codec->extradata_size = get_le16(pb);
        if (codec->extradata_size > 0)
		{
            if (codec->extradata_size > size - 18)
                codec->extradata_size = size - 18;
            codec->extradata = av_mallocz(codec->extradata_size);
            get_buffer(pb, (unsigned char *)(codec->extradata), codec->extradata_size);
        } 
		else
            codec->extradata_size = 0;

        /* It is possible for the chunk to contain garbage at the end */
        if (size - codec->extradata_size - 18 > 0)
            url_fskip(pb, size - codec->extradata_size - 18);
    }
}

int AsfExtractor::has_codec_parameters(AVCodecContext *enc)
{
    int val;
    switch(enc->codec_type)
	{
    case CODEC_TYPE_AUDIO:
        val = enc->sample_rate;
        break;
    case CODEC_TYPE_VIDEO:
        val = enc->width;
        break;
    default:
        val = 1;
        break;
    }
    return (val != 0);
}

static void av_destruct_packet(AVPacket *pkt)
{
    av_free(pkt->data);
    pkt->data = NULL; pkt->size = 0;
}

int AsfExtractor::av_dup_packet(AVPacket *pkt)
{
    if (pkt->destruct != av_destruct_packet)
	{
        uint8_t *data;
        /* we duplicate the packet and don't forget to put the padding
		again */
        data = (uint8_t *)av_malloc(pkt->size + FF_INPUT_BUFFER_PADDING_SIZE);
        if (!data) 
		{
            return AVERROR_NOMEM;
        }
        memcpy(data, pkt->data, pkt->size);
        memset(data + pkt->size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
        pkt->data = data;
        pkt->destruct = av_destruct_packet;
    }
    return 0;
}

int AsfExtractor::av_find_stream_info(AVFormatContext *ic)
{
    int i, count, ret, read_size;
    AVStream *st;
    AVPacket pkt1, *pkt;
    AVPacketList *pktl=NULL, **ppktl;

    count = 0;
    read_size = 0;
    ppktl = &ic->packet_buffer;
    for(;;) 
	{
        /* check if one codec still needs to be handled */
        for(i=0;i<ic->nb_streams;i++)
		{
            st = ic->streams[i];
            if (!has_codec_parameters(&st->codec))
                break;
        }
		
        if (i == ic->nb_streams) 
		{
		/* NOTE: if the format has no header, then we need to read
		some packets to get most of the streams, so we cannot
			stop here */
            if (!(ic->ctx_flags & AVFMTCTX_NOHEADER)) 
			{
                /* if we found the info for all the codecs, we can stop */
                ret = count;
                break;
            }
        } 
		else 
		{
            /* we did not get all the codec info, but we read too much data */
            if (read_size >= MAX_READ_SIZE)
			{
                ret = count;
                break;
            }
        }
        /* NOTE: a new stream can be added there if no header in file
		(AVFMTCTX_NOHEADER) */
        ret = av_read_frame_internal(ic, &pkt1);
        if (ret < 0)
		{
            /* EOF or error */
            ret = -1; /* we could not have all the codec parameters before EOF */
            if ((ic->ctx_flags & AVFMTCTX_NOHEADER) &&
                i == ic->nb_streams)
                ret = 0;
            break;
        }
		
        pktl = (AVPacketList *)av_mallocz(sizeof(AVPacketList));
        if (!pktl) 
		{
            ret = AVERROR_NOMEM;
            break;
        }
		
        /* add the packet in the buffered packet list */
        *ppktl = pktl;
        ppktl = &pktl->next;
		
        pkt = &pktl->pkt;
        *pkt = pkt1;
        
        /* duplicate the packet */
        if (av_dup_packet(pkt) < 0)
		{
			ret = AVERROR_NOMEM;
			break;
        }
		
        read_size += pkt->size;
		
        st = ic->streams[pkt->stream_index];
        st->codec_info_duration += pkt->duration;
        if (pkt->duration != 0)
            st->codec_info_nb_frames++;
		
			/* if still no information, we try to open the codec and to
			decompress the frame. We try to avoid that in most cases as
			it takes longer and uses more memory. For MPEG4, we need to
		decompress for Quicktime. */
        if (!has_codec_parameters(&st->codec) &&
            (st->codec.codec_id == CODEC_ID_FLV1 ||
			st->codec.codec_id == CODEC_ID_H264 ||
			st->codec.codec_id == CODEC_ID_H263 ||
			(st->codec.codec_id == CODEC_ID_MPEG4 && !st->need_parsing)))
            try_decode_frame(st, pkt->data, pkt->size);
        
        if (st->codec_info_duration >= MAX_STREAM_DURATION)
		{
            break;
        }
        count++;
    }
	
    /* set real frame rate info */
    for(i=0;i<ic->nb_streams;i++)
	{
        st = ic->streams[i];
        if (st->codec.codec_type == CODEC_TYPE_VIDEO)
		{
            /* compute the real frame rate for telecine */
            if ((st->codec.codec_id == CODEC_ID_MPEG1VIDEO ||
				st->codec.codec_id == CODEC_ID_MPEG2VIDEO) &&
                st->codec.sub_id == 2) 
			{
                if (st->codec_info_nb_frames >= 20)
				{
                    float coded_frame_rate, est_frame_rate;
                    est_frame_rate =((double)st->codec_info_nb_frames * AV_TIME_BASE) / 
                        (double)st->codec_info_duration ;
                    coded_frame_rate = (double)st->codec.frame_rate /
                        (double)st->codec.frame_rate_base;
#if 0
                    printf("telecine: coded_frame_rate=%0.3f est_frame_rate=%0.3f\n", 
						coded_frame_rate, est_frame_rate);
#endif
						/* if we detect that it could be a telecine, we
						signal it. It would be better to do it at a
					higher level as it can change in a film */
                    if (coded_frame_rate >= 24.97 && 
                        (est_frame_rate >= 23.5 && est_frame_rate < 24.5)) 
					{
                        st->r_frame_rate = 24024;
                        st->r_frame_rate_base = 1001;
                    }
                }
            }
            /* if no real frame rate, use the codec one */
            if (!st->r_frame_rate)
			{
                st->r_frame_rate      = st->codec.frame_rate;
                st->r_frame_rate_base = st->codec.frame_rate_base;
            }
        }
    }
    av_estimate_timings(ic);
    return ret;
}

static URLContext *url_fileno(ByteIOContext *s)
{
    return (URLContext *)s->opaque;
}

offset_t AsfExtractor::url_filesize(URLContext *h)
{
#if 0
    offset_t pos, size;
    pos = file_seek(h, 0, SEEK_CUR);
    size = file_seek(h, 0, SEEK_END);
    file_seek(h, pos, SEEK_SET);
    return size;
#else
    return mFileSize;
#endif 
}

static int av_has_timings(AVFormatContext *ic)
{
    int i;
    AVStream *st;
	
    for(i = 0;i < ic->nb_streams; i++) 
	{
        st = ic->streams[i];
        if (st->start_time != AV_NOPTS_VALUE &&
            st->duration != AV_NOPTS_VALUE)
            return 1;
    }
    return 0;
}

static void av_update_stream_timings(AVFormatContext *ic)
{
    int64_t start_time, end_time, end_time1;
    int i;
    AVStream *st;
    start_time = MAXINT64;
    end_time = MININT64;
    for(i = 0;i < ic->nb_streams; i++) 
	{
        st = ic->streams[i];
        if (st->start_time != AV_NOPTS_VALUE) 
		{
            if (st->start_time < start_time)
                start_time = st->start_time;
            if (st->duration != AV_NOPTS_VALUE) 
			{
                end_time1 = st->start_time + st->duration;
                if (end_time1 > end_time)
                    end_time = end_time1;
            }
        }
    }
    if (start_time != MAXINT64)
	{
        ic->start_time = start_time;
        if (end_time != MAXINT64) 
		{
            ic->duration = end_time - start_time;
            if (ic->file_size > 0) 
			{
                /* compute the bit rate */
                ic->bit_rate = (double)ic->file_size * 8.0 * AV_TIME_BASE / 
                    (double)ic->duration;
            }
        }
    }
}

static void fill_all_stream_timings(AVFormatContext *ic)
{
    int i;
    AVStream *st;
    av_update_stream_timings(ic);
    for(i = 0;i < ic->nb_streams; i++) 
	{
        st = ic->streams[i];
        if (st->start_time == AV_NOPTS_VALUE)
		{
            st->start_time = ic->start_time;
            st->duration = ic->duration;
        }
    }
}

static void av_estimate_timings_from_bit_rate(AVFormatContext *ic)
{
    int64_t filesize, duration;
    int bit_rate, i;
    AVStream *st;
	
    /* if bit_rate is already set, we believe it */
    if (ic->bit_rate == 0)
	{
        bit_rate = 0;
        for(i=0;i<ic->nb_streams;i++)
		{
            st = ic->streams[i];
            bit_rate += st->codec.bit_rate;
        }
        ic->bit_rate = bit_rate;
    }
	
    /* if duration is already set, we believe it */
    if (ic->duration == AV_NOPTS_VALUE && 
        ic->bit_rate != 0 && 
        ic->file_size != 0) 
	{
        filesize = ic->file_size;
        if (filesize > 0)
		{
            duration = (int64_t)((8 * AV_TIME_BASE * (double)filesize) / (double)ic->bit_rate);
            for(i = 0; i < ic->nb_streams; i++)
			{
                st = ic->streams[i];
                if (st->start_time == AV_NOPTS_VALUE ||
                    st->duration == AV_NOPTS_VALUE) 
				{
                    st->start_time = 0;
                    st->duration = duration;
                }
            }
        }
    }
}

void AsfExtractor::av_estimate_timings(AVFormatContext *ic)
{
    URLContext *h;
    int64_t file_size;
    /* get the file size, if possible */
    if (0/*ic->iformat->flags & AVFMT_NOFILE*/)
	{
        file_size = 0;
    }
	else 
	{
        h = url_fileno(&ic->pb);
        file_size = url_filesize(h);
        if (file_size < 0)
            file_size = 0;
    }
    ic->file_size = file_size;
    if (av_has_timings(ic))
	{
	/* at least one components has timings - we use them for all
		the components */
        fill_all_stream_timings(ic);
    } 
	else 
	{
        /* less precise: use bit rate info */
        av_estimate_timings_from_bit_rate(ic);
    }
    av_update_stream_timings(ic);
}


int AsfExtractor::get_audio_frame_size(AVCodecContext *enc, int size)
 {
	 int frame_size;
	 
	 if (enc->frame_size <= 1) 
	 {
	 /* specific hack for pcm codecs because no frame size is
		 provided */
		 switch(enc->codec_id)
		 {
		 case CODEC_ID_PCM_S16LE:
		 case CODEC_ID_PCM_S16BE:
		 case CODEC_ID_PCM_U16LE:
		 case CODEC_ID_PCM_U16BE:
			 if (enc->channels == 0)
				 return -1;
			 frame_size = size / (2 * enc->channels);
			 break;
		 case CODEC_ID_PCM_S8:
		 case CODEC_ID_PCM_U8:
		 case CODEC_ID_PCM_MULAW:
		 case CODEC_ID_PCM_ALAW:
			 if (enc->channels == 0)
				 return -1;
			 frame_size = size / (enc->channels);
			 break;
		 default:
			 /* used for example by ADPCM codecs */
			 if (enc->bit_rate == 0)
				 return -1;
			 frame_size = (size * 8 * enc->sample_rate) / enc->bit_rate;
			 break;
		 }
	 }
	 else 
	 {
		 frame_size = enc->frame_size;
	 }
	 return frame_size;
 }

 void AsfExtractor::compute_frame_duration(int *pnum, int *pden,
                                   AVFormatContext *s, AVStream *st, 
                                   AVCodecParserContext *pc, AVPacket *pkt)
{
    int frame_size;
	
    *pnum = 0;
    *pden = 0;
    switch(st->codec.codec_type)
	{
    case CODEC_TYPE_VIDEO:
        *pnum = st->codec.frame_rate_base;
        *pden = st->codec.frame_rate;
        if (pc && pc->repeat_pict)
		{
            *pden *= 2;
            *pnum = (*pnum) * (2 + pc->repeat_pict);
        }
        break;
    case CODEC_TYPE_AUDIO:
        frame_size = get_audio_frame_size(&st->codec, pkt->size);
        if (frame_size < 0)
            break;
        *pnum = frame_size;
        *pden = st->codec.sample_rate;
        break;
    default:
        break;
    }
}

void AsfExtractor::compute_pkt_fields(AVFormatContext *s, AVStream *st, 
                               AVCodecParserContext *pc, AVPacket *pkt)
{
    int num, den, presentation_delayed;
	
    if (pkt->duration == 0)
	{
        compute_frame_duration(&num, &den, s, st, pc, pkt);
        if (den && num) 
		{
            //pkt->duration = (num * (int64_t)AV_TIME_BASE) / den;
            pkt->duration=0;
        }
    }
	
    /* do we have a video B frame ? */
    presentation_delayed = 0;
    if (st->codec.codec_type == CODEC_TYPE_VIDEO) 
	{
	/* XXX: need has_b_frame, but cannot get it if the codec is
		not initialized */
        if ((st->codec.codec_id == CODEC_ID_MPEG1VIDEO ||
			st->codec.codec_id == CODEC_ID_MPEG2VIDEO ||
			st->codec.codec_id == CODEC_ID_MPEG4 ||
			st->codec.codec_id == CODEC_ID_H264) && 
            pc && pc->pict_type != FF_B_TYPE)
            presentation_delayed = 1;
    }
	
    /* interpolate PTS and DTS if they are not present */
    if (presentation_delayed)
	{
        /* DTS = decompression time stamp */
        /* PTS = presentation time stamp */
        if (pkt->dts == AV_NOPTS_VALUE) 
		{
            pkt->dts = st->cur_dts;
        } 
		else
		{
            st->cur_dts = pkt->dts;
        }
        /* this is tricky: the dts must be incremented by the duration
		of the frame we are displaying, i.e. the last I or P frame */
        if (st->last_IP_duration == 0)
            st->cur_dts += pkt->duration;
        else
            st->cur_dts += st->last_IP_duration;
        st->last_IP_duration  = pkt->duration;
        /* cannot compute PTS if not present (we can compute it only
		by knowing the futur */
    } 
	else 
	{
        /* presentation is not delayed : PTS and DTS are the same */
        if (pkt->pts == AV_NOPTS_VALUE)
		{
            pkt->pts = st->cur_dts;
            pkt->dts = st->cur_dts;
        } 
		else 
		{
            st->cur_dts = pkt->pts;
            pkt->dts = pkt->pts;
        }
        st->cur_dts += pkt->duration;
    }
    
    /* update flags */
    if (pc) 
	{
        pkt->flags = 0;
        /* key frame computation */
        switch(st->codec.codec_type)
		{
        case CODEC_TYPE_VIDEO:
            if (pc->pict_type == FF_I_TYPE)
                pkt->flags |= PKT_FLAG_KEY;
            break;
        case CODEC_TYPE_AUDIO:
            pkt->flags |= PKT_FLAG_KEY;
            break;
        default:
            break;
        }
    }
	
}

static void av_destruct_packet_nofree(AVPacket *pkt)
{
    pkt->data = NULL; pkt->size = 0;
}

static __inline void av_free_packet(AVPacket *pkt) //jacky 2006/10/18
{
    if (pkt && pkt->destruct) 
	{
		pkt->destruct(pkt);
    }
}

static __inline void av_init_packet(AVPacket *pkt)
{
    pkt->pts   = AV_NOPTS_VALUE;
    pkt->dts   = AV_NOPTS_VALUE;
    pkt->duration = 0;
    pkt->flags = 0;
    pkt->stream_index = 0;
}



//copy from latest ffmpeg1.0:
static int av_new_packet(AVPacket *pkt, int size)
{
    uint8_t *data = NULL;
    if ((unsigned)size < (unsigned)size + FF_INPUT_BUFFER_PADDING_SIZE)
        data = (uint8_t *)av_malloc(size + FF_INPUT_BUFFER_PADDING_SIZE);
    if (data) {
        memset(data + size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
    } else
        size = 0;

    av_init_packet(pkt);
    pkt->data     = (uint8_t *)data;
    pkt->size     = size;
    pkt->destruct = av_destruct_packet;
    if (!data)
        return AVERROR_NOMEM;
    return 0;
}

int AsfExtractor::av_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    return asf_read_packet/*s->iformat->read_packet*/(s, pkt);
}

#define DO_2BITS(bits, var, defval) \
	 switch (bits & 3) \
	 { \
	 case 3: var = get_le32(pb); rsize += 4; break; \
	 case 2: var = get_le16(pb); rsize += 2; break; \
	 case 1: var = get_byte(pb); rsize++; break; \
	 default: var = defval; break; \
	 }

 int AsfExtractor::asf_get_packet(AVFormatContext *s)
 {
	 ASFContext *asf =(ASFContext *) s->priv_data;
	 ByteIOContext *pb = &s->pb;
	 uint32_t packet_length, padsize;
	 int rsize = 9;
	 int c;
	 
	 //assert((url_ftell(&s->pb) - s->data_offset) % asf->packet_size == 0);
	 c=(url_ftell(&s->pb) - s->data_offset) % asf->packet_size == 0;
	 if (!c)
		 return -1;
 
	 c = get_byte(pb);
	 if (c != 0x82) 
	 {
		 if (!url_feof(pb))
		 printf("ff asf bad header %x  at:%lld\n", c, url_ftell(pb));
	 }
	 if ((c & 0x0f) == 2)
	 { // always true for now
		 if (get_le16(pb) != 0) 
		 {
			 if (!url_feof(pb))
			 printf("ff asf bad non zero\n");
			 //return -EIO;
			 return -1;  //jacky 2006/10/18
		 }
		 rsize+=2;
 /*    }else{
		 if (!url_feof(pb))
		 printf("ff asf bad header %x  at:%lld\n", c, url_ftell(pb));
	 return -EIO;*/
	 }
 
	 asf->packet_flags = get_byte(pb);
	 asf->packet_property = get_byte(pb);
 
	 DO_2BITS(asf->packet_flags >> 5, packet_length, asf->packet_size);
	 DO_2BITS(asf->packet_flags >> 1, padsize, 0); // sequence ignored
	 DO_2BITS(asf->packet_flags >> 3, padsize, 0); // padding length
 
	 asf->packet_timestamp = get_le32(pb);
	 get_le16(pb); /* duration */
	 // rsize has at least 11 bytes which have to be present
 
	 if (asf->packet_flags & 0x01) 
	 {
		 asf->packet_segsizetype = get_byte(pb); rsize++;
		 asf->packet_segments = asf->packet_segsizetype & 0x3f;
	 } 
	 else 
	 {
		 asf->packet_segments = 1;
		 asf->packet_segsizetype = 0x80;
	 }
	 asf->packet_size_left = packet_length - padsize - rsize;
	 if (packet_length < asf->hdr.min_pktsize)
		 padsize += asf->hdr.min_pktsize - packet_length;
	 asf->packet_padsize = padsize;
#if 0
	 printf("packet: size=%d padsize=%d  left=%d\n", asf->packet_size, asf->packet_padsize, asf->packet_size_left);
#endif
	 return 0;
 }


 int AsfExtractor::asf_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    ASFContext *asf =(ASFContext *) s->priv_data;
    ASFStream *asf_st = 0;
    ByteIOContext *pb = &s->pb;
    //static int pc = 0;
    for (;;) 
	{
		int rsize = 0;
		if (asf->packet_size_left < FRAME_HEADER_SIZE|| asf->packet_segments < 1)
		{
			//asf->packet_size_left <= asf->packet_padsize) {
			int ret = asf->packet_size_left + asf->packet_padsize;
			//printf("PacketLeftSize:%d  Pad:%d Pos:%Ld\n", asf->packet_size_left, asf->packet_padsize, url_ftell(pb));
			/* fail safe */
			url_fskip(pb, ret);
			asf->packet_pos=(int) url_ftell(&s->pb);
			ret = asf_get_packet(s);
			//printf("READ ASF PACKET  %d   r:%d   c:%d\n", ret, asf->packet_size_left, pc++);
			if (ret < 0 || url_feof(pb)){
			   //return -EIO;
				return -1; //jacky 2006/10/18
			}
			asf->packet_time_start = 0;
			continue;
		}
		
		if (asf->packet_time_start == 0)
		{
			/* read frame header */
			int num = get_byte(pb);
			asf->packet_segments--;
			rsize++;
			asf->packet_key_frame = (num & 0x80) >> 7;
			asf->stream_index = asf->asfid2avid[num & 0x7f];
			// sequence should be ignored!
			DO_2BITS(asf->packet_property >> 4, asf->packet_seq, 0);
			DO_2BITS(asf->packet_property >> 2, asf->packet_frag_offset, 0);
			DO_2BITS(asf->packet_property, asf->packet_replic_size, 0);
		    //printf("key:%d stream:%d seq:%d offset:%d replic_size:%d\n", asf->packet_key_frame, asf->stream_index, asf->packet_seq, //asf->packet_frag_offset, asf->packet_replic_size);
			if (asf->packet_replic_size > 1)
			{
				assert(asf->packet_replic_size >= 8);
				// it should be always at least 8 bytes - FIXME validate
				asf->packet_obj_size = get_le32(pb);
				asf->packet_frag_timestamp = get_le32(pb); // timestamp
				if (asf->packet_replic_size > 8)
					url_fskip(pb, asf->packet_replic_size - 8);
				rsize += asf->packet_replic_size; // FIXME - check validity
			} 
			else if (asf->packet_replic_size==1)
			{
				// multipacket - frag_offset is begining timestamp
				asf->packet_time_start = asf->packet_frag_offset;
				asf->packet_frag_offset = 0;
				asf->packet_frag_timestamp = asf->packet_timestamp;
				asf->packet_time_delta = get_byte(pb);
				rsize++;
			}
			else
			{
                assert(asf->packet_replic_size==0);
            }
			if (asf->packet_flags & 0x01) 
			{
				DO_2BITS(asf->packet_segsizetype >> 6, asf->packet_frag_size, 0); // 0 is illegal
				#undef DO_2BITS
				//printf("Fragsize %d\n", asf->packet_frag_size);
			}
			else 
			{
				asf->packet_frag_size = asf->packet_size_left - rsize;
				//printf("Using rest  %d %d %d\n", asf->packet_frag_size, asf->packet_size_left, rsize);
			}
			if (asf->packet_replic_size == 1) 
			{
				asf->packet_multi_size = asf->packet_frag_size;
				if (asf->packet_multi_size > asf->packet_size_left) 
				{
					asf->packet_segments = 0;
                    continue;
				}
			}
	        asf->packet_size_left -= rsize;
	        //printf("___objsize____  %d   %d    rs:%d\n", asf->packet_obj_size, asf->packet_frag_offset, rsize);
	        if (asf->stream_index < 0) 
		    {
			   asf->packet_time_start = 0;
			   /* unhandled packet (should not happen) */
			   url_fskip(pb, asf->packet_frag_size);
			   asf->packet_size_left -= asf->packet_frag_size;
			   printf("ff asf skip %d  %d\n", asf->packet_frag_size, num & 0x7f);
               continue;
	       }
	       asf->asf_st =(ASFStream*) s->streams[asf->stream_index]->priv_data;
	    }
	   asf_st = asf->asf_st;
	   if ((asf->packet_frag_offset != asf_st->frag_offset || (asf->packet_frag_offset
		    && asf->packet_seq != asf_st->seq)) // seq should be ignored
	      ) 
	   {
	       /* cannot continue current packet: free it */
	       // FIXME better check if packet was already allocated
	       printf("ff asf parser skips: %d - %d     o:%d - %d    %d %d   fl:%d\n",
		     asf_st->pkt.size,
		     asf->packet_obj_size,
		     asf->packet_frag_offset, asf_st->frag_offset,
		     asf->packet_seq, asf_st->seq, asf->packet_frag_size);
	       if (asf_st->pkt.size)
			  av_free_packet(&asf_st->pkt);
	       asf_st->frag_offset = 0;
	       if (asf->packet_frag_offset != 0) 
		    {
			  url_fskip(pb, asf->packet_frag_size);
			  printf("ff asf parser skiping %db\n", asf->packet_frag_size);
			  asf->packet_size_left -= asf->packet_frag_size;
			  continue;
	        }
	     }
	   
	     if (asf->packet_replic_size == 1)
	     {
	        // frag_offset is here used as the begining timestamp
	        asf->packet_frag_timestamp = asf->packet_time_start;
	        asf->packet_time_start += asf->packet_time_delta;
	        asf->packet_obj_size = asf->packet_frag_size = get_byte(pb);
	        asf->packet_size_left--;
            asf->packet_multi_size--;
	        if (asf->packet_multi_size < asf->packet_obj_size)
	        {
			   asf->packet_time_start = 0;
			   url_fskip(pb, asf->packet_multi_size);
			   asf->packet_size_left -= asf->packet_multi_size;
               continue;
	        }
	        asf->packet_multi_size -= asf->packet_obj_size;
	        //printf("COMPRESS size  %d  %d  %d   ms:%d\n", asf->packet_obj_size, asf->packet_frag_timestamp, asf->packet_size_left, asf->packet_multi_size);
	     }
		 
	     if (asf_st->frag_offset == 0) 
	     {
	        /* new packet */
	        av_new_packet(&asf_st->pkt, asf->packet_obj_size);
	        asf_st->seq = asf->packet_seq;
	        asf_st->pkt.pts = asf->packet_frag_timestamp - asf->hdr.preroll;
	        asf_st->pkt.stream_index = asf->stream_index;
            asf_st->packet_pos= asf->packet_pos;            
            //printf("new packet: stream:%d key:%d packet_key:%d audio:%d size:%d\n", 
            //asf->stream_index, asf->packet_key_frame, asf_st->pkt.flags & PKT_FLAG_KEY,
            //s->streams[asf->stream_index]->codec.codec_type == CODEC_TYPE_AUDIO, asf->packet_obj_size);
	        if (s->streams[asf->stream_index]->codec.codec_type == CODEC_TYPE_AUDIO) 
			    asf->packet_key_frame = 1;
	        if (asf->packet_key_frame)
			    asf_st->pkt.flags |= PKT_FLAG_KEY;
	      }
	      /* read data */
	      //printf("READ PACKET s:%d  os:%d  o:%d,%d  l:%d   DATA:%p\n",
	      //       asf->packet_size, asf_st->pkt.size, asf->packet_frag_offset,
	      //       asf_st->frag_offset, asf->packet_frag_size, asf_st->pkt.data);
	      asf->packet_size_left -= asf->packet_frag_size;
	      if (asf->packet_size_left < 0)
                continue;
	      get_buffer(pb, asf_st->pkt.data + asf->packet_frag_offset,
		             asf->packet_frag_size);
	      asf_st->frag_offset += asf->packet_frag_size;
	      /* test if whole packet is read */
	     if (asf_st->frag_offset == asf_st->pkt.size) 
	     {
	    	 /* return packet */
	   	 	 if (asf_st->ds_span > 1) 
			 {
			    /* packet descrambling */
			    char* newdata =(char*) av_malloc(asf_st->pkt.size);
			    if (newdata){
				    int offset = 0;
				    while (offset < asf_st->pkt.size)
				    {
					    int off = offset / asf_st->ds_chunk_size;
					    int row = off / asf_st->ds_span;
					    int col = off % asf_st->ds_span;
					    int idx = row + col * asf_st->ds_packet_size / asf_st->ds_chunk_size;
					    //printf("off:%d  row:%d  col:%d  idx:%d\n", off, row, col, idx);
					    memcpy(newdata + offset,asf_st->pkt.data + idx * asf_st->ds_chunk_size,asf_st->ds_chunk_size);
					    offset += asf_st->ds_chunk_size;
				    }
				    av_free(asf_st->pkt.data);
				    asf_st->pkt.data =(uint8_t *) newdata;
			     }
	         }
	         asf_st->frag_offset = 0;
	         memcpy(pkt, &asf_st->pkt, sizeof(AVPacket));
	         //printf("packet %d %d\n", asf_st->pkt.size, asf->packet_frag_size);
	         asf_st->pkt.size = 0;
	         asf_st->pkt.data = 0;
	         break; // packet completed
	     }
    }
    return 0;
}





int AsfExtractor::av_read_frame_internal(AVFormatContext *s, AVPacket *pkt)
{
    AVStream *st;
    int len, ret, i;
    for(;;) 
	{
        /* select current input stream component */
        st = s->cur_st;
        if (st) 
		{
            if (!st->parser) 
			{
                /* no parsing needed: we just output the packet as is */
                /* raw data support */
                *pkt = s->cur_pkt;
                compute_pkt_fields(s, st, NULL, pkt);
                s->cur_st = NULL;
                return 0;
            }else if (s->cur_len > 0){
                len = av_parser_parse(st->parser, &st->codec, &pkt->data, &pkt->size, 
					s->cur_ptr, s->cur_len,
					s->cur_pkt.pts, s->cur_pkt.dts);
                s->cur_pkt.pts = AV_NOPTS_VALUE;
                s->cur_pkt.dts = AV_NOPTS_VALUE;
                /* increment read pointer */
                s->cur_ptr += len;
                s->cur_len -= len;
                /* return packet if any */
                if (pkt->size)
				{
got_packet:
				    pkt->duration = 0;
				    pkt->stream_index = st->index;
				    pkt->pts = st->parser->pts;
				    pkt->dts = st->parser->dts;
				    pkt->destruct = av_destruct_packet_nofree;
				    compute_pkt_fields(s, st, st->parser, pkt);
				    return 0;
                }
            }else	{
                /* free packet */
                av_free_packet(&s->cur_pkt); 
                s->cur_st = NULL;
            }
        }else{  /* read next packet */
            ret = av_read_packet(s, &s->cur_pkt);
            if (ret < 0)
			{   
				if (ret == -1) //jacky 2006/10/18 
					//if (ret == -EAGAIN)
                    return ret;
                /* return the last frames, if any */
                for(i = 0; i < s->nb_streams; i++) 
				{
                    st = s->streams[i];
                    if (st->parser) 
					{
                        av_parser_parse(st->parser, &st->codec, 
							&pkt->data, &pkt->size, 
							NULL, 0, 
							AV_NOPTS_VALUE, AV_NOPTS_VALUE);
                        if (pkt->size)
                            goto got_packet;
                    }
                }
                /* no more packets: really terminates parsing */
                return ret;
            }
			
            /* convert the packet time stamp units and handle wrapping */
            s->cur_pkt.pts = convert_timestamp_units(s, 
				&s->last_pkt_pts, &s->last_pkt_pts_frac,
				&s->last_pkt_stream_pts,
				s->cur_pkt.pts);
            s->cur_pkt.dts = convert_timestamp_units(s, 
				&s->last_pkt_dts,  &s->last_pkt_dts_frac,
				&s->last_pkt_stream_dts,
				s->cur_pkt.dts);
			
            
            /* duration field */
            if (s->cur_pkt.duration != 0) 
			{
                s->cur_pkt.duration = ((int64_t)s->cur_pkt.duration * AV_TIME_BASE * s->pts_num) / 
                    s->pts_den;
            }
			
            st = s->streams[s->cur_pkt.stream_index];
            s->cur_st = st;
            s->cur_ptr = s->cur_pkt.data;
            s->cur_len = s->cur_pkt.size;
            if (st->need_parsing && !st->parser) 
			{
                st->parser = av_parser_init(st->codec.codec_id);
                if (!st->parser) 
				{
                    /* no parser available : just output the raw packets */
                    st->need_parsing = 0;
                }
            }
        }
    }
}

int AsfExtractor::av_parser_parse(AVCodecParserContext *s, 
                    AVCodecContext *avctx,
                    uint8_t **poutbuf, int *poutbuf_size, 
                    const uint8_t *buf, int buf_size,
                    int64_t pts, int64_t dts)
{
    int index, i, k;
    uint8_t dummy_buf[FF_INPUT_BUFFER_PADDING_SIZE];
    
    if (buf_size == 0) 
	{
        /* padding is always necessary even if EOF, so we add it here */
        memset(dummy_buf, 0, sizeof(dummy_buf));
        buf = dummy_buf;
    } 
	else
	{
        /* add a new packet descriptor */
        k = (s->cur_frame_start_index + 1) & (AV_PARSER_PTS_NB - 1);
        s->cur_frame_start_index = k;
        s->cur_frame_offset[k] = s->cur_offset;
        s->cur_frame_pts[k] = pts;
        s->cur_frame_dts[k] = dts;

        /* fill first PTS/DTS */
        if (s->cur_offset == 0) 
		{
            s->last_pts = pts;
            s->last_dts = dts;
        }
    }

    /* WARNING: the returned index can be negative */
    index = s->parser->parser_parse(s, avctx, poutbuf, poutbuf_size, buf, buf_size);
    /* update the file pointer */
    if (*poutbuf_size) 
	{
        /* fill the data for the current frame */
        s->frame_offset = s->last_frame_offset;
        s->pts = s->last_pts;
        s->dts = s->last_dts;
        
        /* offset of the next frame */
        s->last_frame_offset = s->cur_offset + index;
        /* find the packet in which the new frame starts. It
           is tricky because of MPEG video start codes
           which can begin in one packet and finish in
           another packet. In the worst case, an MPEG
           video start code could be in 4 different
           packets. */
        k = s->cur_frame_start_index;
        for(i = 0; i < AV_PARSER_PTS_NB; i++)
		{
            if (s->last_frame_offset >= s->cur_frame_offset[k])
                break;
            k = (k - 1) & (AV_PARSER_PTS_NB - 1);
        }
        s->last_pts = s->cur_frame_pts[k];
        s->last_dts = s->cur_frame_dts[k];
    }
    if (index < 0)
        index = 0;
    s->cur_offset += index;
    return index;
}


int64_t AsfExtractor::convert_timestamp_units(AVFormatContext *s,
												int64_t *plast_pkt_pts,
												int *plast_pkt_pts_frac,
												int64_t *plast_pkt_stream_pts,
												int64_t pts)
{
    int64_t stream_pts;
    int64_t delta_pts;
    int shift, pts_frac;
	
    if (pts != AV_NOPTS_VALUE)
	{
        stream_pts = pts;
        if (*plast_pkt_stream_pts != AV_NOPTS_VALUE) 
		{
            shift = 64 - s->pts_wrap_bits;
            delta_pts = ((stream_pts - *plast_pkt_stream_pts) << shift) >> shift;
            /* XXX: overflow possible but very unlikely as it is a delta */
            delta_pts = delta_pts * AV_TIME_BASE * s->pts_num;
            pts = *plast_pkt_pts + (delta_pts / s->pts_den);
            pts_frac = *plast_pkt_pts_frac + (delta_pts % s->pts_den);
            if (pts_frac >= s->pts_den) 
			{
                pts_frac -= s->pts_den;
                pts++;
            }
        } 
		else 
		{
            /* no previous pts, so no wrapping possible */
			
			
			/*         pts = (int64_t)(((double)stream_pts * AV_TIME_BASE * s->pts_num) / 
			(double)s->pts_den);
			*/
			//pts = (int64_t)((stream_pts * AV_TIME_BASE * s->pts_num) / s->pts_den);
			/////////by ren 20091020
			pts=0;
			
            pts_frac = 0;
        }
        *plast_pkt_stream_pts = stream_pts;
        *plast_pkt_pts = pts;
        *plast_pkt_pts_frac = pts_frac;
    }
    return pts;
}

int AsfExtractor::try_decode_frame(AVStream *st, const uint8_t *data, int size)
{
    int16_t *samples;
    AVCodec *codec;
    int got_picture, ret;
    //AVFrame picture;
    
    codec = avcodec_find_decoder(st->codec.codec_id);
    if (!codec)
        return -1;
    ret = avcodec_open(&st->codec, codec);
    if (ret < 0)
        return ret;
    switch(st->codec.codec_type) 
	{
    case CODEC_TYPE_VIDEO:
        //ret = avcodec_decode_video(&st->codec, &picture, 
        //                           &got_picture, (uint8_t *)data, size);
        break;
    case CODEC_TYPE_AUDIO:
        samples =(int16_t *) av_malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);
        if (!samples)
            goto fail;
        ret = avcodec_decode_audio(&st->codec, samples, 
			&got_picture, (uint8_t *)data, size);
        av_free(samples);
        break;
    default:
        break;
    }
fail:
    avcodec_close(&st->codec);
    return ret;
}

int AsfExtractor::avcodec_close(AVCodecContext *avctx)
{
    if (avctx->codec->close)
        avctx->codec->close(avctx);
    av_freep(&avctx->priv_data);
    avctx->codec = NULL;
    return 0;
}

int AsfExtractor::avcodec_decode_audio(AVCodecContext *avctx, int16_t *samples, 
                         int *frame_size_ptr,
                         uint8_t *buf, int buf_size)
{
    int ret;
    ret = avctx->codec->decode(avctx, samples, frame_size_ptr, 
                               buf, buf_size);
    avctx->frame_number++;
	//printf("framenum=%d,,",avctx->frame_number);
    return ret;
}

int AsfExtractor::avcodec_open(AVCodecContext *avctx, AVCodec *codec)
{
    int ret;

    if(avctx->codec)
        return -1;

    avctx->codec = codec;
    avctx->codec_id =(CodecID) codec->id;
    avctx->frame_number = 0;
    if (codec->priv_data_size > 0) 
	{
        avctx->priv_data = av_mallocz(codec->priv_data_size);
        if (!avctx->priv_data) 
            return -1;  //jacky 2006/10/18
		    //return -ENOMEM;
    } 
	else 
	{
        avctx->priv_data = NULL;
    }
    ret = avctx->codec->init(avctx);
    if (ret < 0) 
	{
        av_freep(&avctx->priv_data);
        return ret;
    }
    return 0;
}

AVCodec *first_avcodec=NULL;
AVCodecParser *av_first_parser = NULL;

AVCodec *AsfExtractor::avcodec_find_decoder(enum CodecID id)
{
    AVCodec *p;
    p = first_avcodec;
    while (p)
	{
        if (p->decode != NULL && p->id == id)
            return p;
        p = p->next;
    }
    return NULL;
}

AVCodecParserContext *AsfExtractor::av_parser_init(int codec_id)
{
    AVCodecParserContext *s;
    AVCodecParser *parser;
    int ret;

    for(parser = av_first_parser; parser != NULL; parser = parser->next)
	{
        if (parser->codec_ids[0] == codec_id ||
            parser->codec_ids[1] == codec_id ||
            parser->codec_ids[2] == codec_id)
            goto found;
    }
    return NULL;
 found:
    s =(AVCodecParserContext *) av_mallocz(sizeof(AVCodecParserContext));
    if (!s)
        return NULL;
    s->parser = parser;
    s->priv_data = av_mallocz(parser->priv_data_size);
    if (!s->priv_data)
	{
        av_free(s);
        return NULL;
    }
    if (parser->parser_init) 
	{
        ret = parser->parser_init(s);
        if (ret != 0) 
		{
            av_free(s->priv_data);
            av_free(s);
            return NULL;
        }
    }
    return s;
}

int AsfExtractor::av_read_frame(AVFormatContext *s, AVPacket *pkt)
{
    AVPacketList *pktl;
	int ret=0;
    pktl = s->packet_buffer;
    if (pktl) 
	{
        /* read packet from packet buffer, if there is data */
        *pkt = pktl->pkt;
        s->packet_buffer = pktl->next;
        av_free(pktl);
        return 0;
    } 
	else 
	{
        ret=av_read_frame_internal(s, pkt);
		if(ret<0)
			ALOGW("%s %d av_read_frame_internal->ret=%d %s\n",__FUNCTION__,__LINE__,ret,__FILE__);
		return ret;
    }
}

void AsfExtractor::asf_reset_header(AVFormatContext *s)
{
    ASFContext *asf =(ASFContext *) s->priv_data;
    ASFStream *asf_st;
    int i;

    asf->packet_nb_frames = 0;
    asf->packet_timestamp_start = -1;
    asf->packet_timestamp_end = -1;
    asf->packet_size_left = 0;
    asf->packet_segments = 0;
    asf->packet_flags = 0;
    asf->packet_property = 0;
    asf->packet_timestamp = 0;
    asf->packet_segsizetype = 0;
    asf->packet_segments = 0;
    asf->packet_seq = 0;
    asf->packet_replic_size = 0;
    asf->packet_key_frame = 0;
    asf->packet_padsize = 0;
    asf->packet_frag_offset = 0;
    asf->packet_frag_size = 0;
    asf->packet_frag_timestamp = 0;
    asf->packet_multi_size = 0;
    asf->packet_obj_size = 0;
    asf->packet_time_delta = 0;
    asf->packet_time_start = 0;
    
    for(i=0; i<s->nb_streams; i++)
	{
        asf_st= (ASFStream *)s->streams[i]->priv_data;
        av_free_packet(&asf_st->pkt);
        asf_st->frag_offset=0;
        asf_st->seq=0;
    }
    asf->asf_st= NULL;
}


int64_t AsfExtractor::asf_read_pts(AVFormatContext *s, int64_t *ppos, int stream_index)
{
    ASFContext *asf = (ASFContext *)s->priv_data;
    AVPacket pkt1, *pkt = &pkt1;
    ASFStream *asf_st;
    int64_t pts;
    int64_t pos= *ppos;
    int i;
    //int64_t start_pos[s->nb_streams];
    int64_t start_pos[1]; //jacky 2006/10/18

    for(i=0; i<s->nb_streams; i++)
	{
        start_pos[i]= pos;
    }

//printf("asf_read_pts\n");
    url_fseek(&s->pb, pos*asf->packet_size + s->data_offset, SEEK_SET);
    asf_reset_header(s);
    for(;;)
	{
        if (av_read_frame(s, pkt) < 0)
		{
            printf("seek failed\n");
    	    return AV_NOPTS_VALUE;
        }
        pts= pkt->pts;

        av_free_packet(pkt);
        if(pkt->flags&PKT_FLAG_KEY)
		{
            i= pkt->stream_index;

            asf_st= (ASFStream *)s->streams[i]->priv_data;

            assert((asf_st->packet_pos - s->data_offset) % asf->packet_size == 0);
            pos= (asf_st->packet_pos - s->data_offset) / asf->packet_size;

            av_add_index_entry(s->streams[i], pos, pts, pos - start_pos[i] + 1, AVINDEX_KEYFRAME);
            start_pos[i]= pos + 1;
            
            if(pkt->stream_index == stream_index)
               break;
        }
    }

    *ppos= pos;
//printf("found keyframe at %Ld stream %d stamp:%Ld\n", *ppos, stream_index, pts);
    return pts;
}


int AsfExtractor::av_index_search_timestamp(AVStream *st, int wanted_timestamp)
{
    AVIndexEntry *entries= st->index_entries;
    int nb_entries= st->nb_index_entries;
    int a, b, m;
    int64_t timestamp;
	
    if (nb_entries <= 0)
        return -1;
    
    a = 0;
    b = nb_entries - 1;
	
    while (a < b)
	{
        m = (a + b + 1) >> 1;
        timestamp = entries[m].timestamp;
        if (timestamp > wanted_timestamp) 
		{
            b = m - 1;
        } 
		else 
		{
            a = m;
        }
    }
    return a;
}


static void *av_fast_realloc(void *ptr, unsigned int *size, unsigned int min_size)
{
    if(min_size < *size) 
        return ptr;
    
    *size= min_size + 10*1024;

    return av_realloc(ptr, *size);
}

/* add a index entry into a sorted list updateing if it is already there */
int AsfExtractor::av_add_index_entry(AVStream *st,
					   int64_t pos, int64_t timestamp, int distance, int flags)
{
    AVIndexEntry *entries, *ie;
    int index;
    
    entries =(AVIndexEntry *) av_fast_realloc(st->index_entries,
		(uint32_t *)&st->index_entries_allocated_size,
		(st->nb_index_entries + 1)*sizeof(AVIndexEntry));
    st->index_entries= entries;
	
    if(st->nb_index_entries)
	{
        index= av_index_search_timestamp(st, timestamp);
        ie= &entries[index];
		
        if(ie->timestamp != timestamp)
		{
            if(ie->timestamp < timestamp)
			{
                index++; //index points to next instead of previous entry, maybe nonexistant
                ie= &st->index_entries[index];
            }
			else
                assert(index==0);
			
            if(index != st->nb_index_entries)
			{
                assert(index < st->nb_index_entries);
                memmove(entries + index + 1, entries + index, sizeof(AVIndexEntry)*(st->nb_index_entries - index));
            }
            st->nb_index_entries++;
        }
    }
	else
	{
        index= st->nb_index_entries++;
        ie= &entries[index];
    }
    
    ie->pos = pos;
    ie->timestamp = timestamp;
    ie->min_distance= distance;
    ie->flags = flags;
    
    return index;
}

static int av_find_default_stream_index(AVFormatContext *s)
{
    int i;
    AVStream *st;
	
    if (s->nb_streams <= 0)
        return -1;
    for(i = 0; i < s->nb_streams; i++) 
	{
        st = s->streams[i];
        if (st->codec.codec_type == CODEC_TYPE_VIDEO) 
		{
            return i;
        }
    }
    return 0;
}


int AsfExtractor::asf_read_close(AVFormatContext *s)
{
    int i;

    for(i=0;i<s->nb_streams;i++) 
	{
		AVStream *st = s->streams[i];
		av_free(st->priv_data);
		av_free(st->codec.extradata);
		av_free(st->codec.palctrl);
    }
    return 0;
}

int AsfExtractor::asf_read_seek(AVFormatContext *s, int stream_index, int64_t pts)
{
    ASFContext *asf = (ASFContext *)s->priv_data;
    AVStream *st;
    int64_t pos;
    int64_t pos_min, pos_max, pts_min, pts_max, cur_pts, pos_limit;
    int no_change;
    
    if (stream_index == -1)
        stream_index= av_find_default_stream_index(s);
    
    if (asf->packet_size <= 0)
        return -1;

    pts_max=
    pts_min= AV_NOPTS_VALUE;
    pos_max= pos_limit= -1; // gcc thinks its uninitalized

    st= s->streams[stream_index];
    if(st->index_entries)
	{
        AVIndexEntry *e;
        int index;

        index= av_index_search_timestamp(st, pts);
        e= &st->index_entries[index];
        if(e->timestamp <= pts)
		{
            pos_min= e->pos;
            pts_min= e->timestamp;
#ifdef DEBUG_SEEK
        printf("unsing cached pos_min=0x%llx dts_min=%0.3f\n", 
               pos_min,pts_min / 90000.0);
#endif
        }
		else
		{
            assert(index==0);
        }
        index++;
        if(index < st->nb_index_entries)
		{
            e= &st->index_entries[index];
            assert(e->timestamp >= pts);
            pos_max= e->pos;
            pts_max= e->timestamp;
            pos_limit= pos_max - e->min_distance;
#ifdef DEBUG_SEEK
        printf("unsing cached pos_max=0x%llx dts_max=%0.3f\n", 
               pos_max,pts_max / 90000.0);
#endif
        }
    }

    if(pts_min == AV_NOPTS_VALUE)
	{
        pos_min = 0;
        pts_min = asf_read_pts(s, &pos_min, stream_index);
        if (pts_min == AV_NOPTS_VALUE) return -1;
    }
    if(pts_max == AV_NOPTS_VALUE)
	{
        pos_max = (url_filesize(url_fileno(&s->pb)) - 1 - s->data_offset) / asf->packet_size; //FIXME wrong
        pts_max = s->duration; //FIXME wrong
        pos_limit= pos_max;
    } 

    no_change=0;
    while (pos_min < pos_limit)
	{
        int64_t start_pos;
        assert(pos_limit <= pos_max);

        if(no_change==0)
		{
            int64_t approximate_keyframe_distance= pos_max - pos_limit;
            // interpolate position (better than dichotomy)
            pos = (int64_t)((double)(pos_max - pos_min) *
                            (double)(pts - pts_min) /
                            (double)(pts_max - pts_min)) + pos_min - approximate_keyframe_distance;
        }
		else if(no_change==1)
		{
            // bisection, if interpolation failed to change min or max pos last time
            pos = (pos_min + pos_limit)>>1;
        }
		else
		{
            // linear search if bisection failed, can only happen if there are very few or no keyframes between min/max
            pos=pos_min;
        }
        if(pos <= pos_min)
            pos= pos_min + 1;
        else if(pos > pos_limit)
            pos= pos_limit;
        start_pos= pos;

        // read the next timestamp 
    	cur_pts = asf_read_pts(s, &pos, stream_index);    
        if(pos == pos_max)
            no_change++;
        else
            no_change=0;

#ifdef DEBUG_SEEK
printf("%Ld %Ld %Ld / %Ld %Ld %Ld target:%Ld limit:%Ld start:%Ld\n", pos_min, pos, pos_max, pts_min, cur_pts, pts_max, pts, pos_limit, start_pos);
#endif
        assert (cur_pts != AV_NOPTS_VALUE);
        if (pts < cur_pts) 
		{
            pos_limit = start_pos - 1;
            pos_max = pos;
            pts_max = cur_pts;
        } 
		else
		{
            pos_min = pos;
            pts_min = cur_pts;
            /* check if we are lucky */
            if (pts == cur_pts)
                break;
        }
    }
    pos = pos_min;
    url_fseek(&s->pb, pos*asf->packet_size + s->data_offset, SEEK_SET);
    asf_reset_header(s);
    return 0;
}

static void av_parser_close(AVCodecParserContext *s)
{
    if (s->parser->parser_close)
        s->parser->parser_close(s);
    av_free(s->priv_data);
    av_free(s);
}

static void flush_packet_queue(AVFormatContext *s)
{
    AVPacketList *pktl;
	
    for(;;) 
	{
        pktl = s->packet_buffer;
        if (!pktl) 
            break;
        s->packet_buffer = pktl->next;
        av_free_packet(&pktl->pkt);
        av_free(pktl);
    }
}

void AsfExtractor::av_close_input_file(AVFormatContext *s)
{
    int i, must_open_file;
    AVStream *st;
	
    /* free previous packet */
    if (s->cur_st && s->cur_st->parser)
        av_free_packet(&s->cur_pkt); 
	
    if (1/*s->iformat->read_close*/)
        asf_read_close/*s->iformat->read_close*/(s);
    for(i=0;i<s->nb_streams;i++) 
	{
        /* free all data in a stream component */
        st = s->streams[i];
        if (st->parser) 
		{
            av_parser_close(st->parser);
        }
        av_free(st->index_entries);
        av_free(st);
    }
    flush_packet_queue(s);
    must_open_file = 1;
    if (0/*s->iformat->flags & AVFMT_NOFILE*/)
	{
        must_open_file = 0;
    }
    if (must_open_file) 
	{
        url_fclose(&s->pb);
    }
    av_freep(&s->priv_data);
    av_free(s);
}


/* flush the frame reader */
static void av_read_frame_flush(AVFormatContext *s)
{
    AVStream *st;
    int i;
	
    flush_packet_queue(s);
	
    /* free previous packet */
    if (s->cur_st) 
	{
        if (s->cur_st->parser)
            av_free_packet(&s->cur_pkt);
        s->cur_st = NULL;
    }
    /* fail safe */
    s->cur_ptr = NULL;
    s->cur_len = 0;
    
    /* for each stream, reset read state */
    for(i = 0; i < s->nb_streams; i++) 
	{
        st = s->streams[i];
        
        if (st->parser) 
		{
            av_parser_close(st->parser);
            st->parser = NULL;
        }
        st->cur_dts = 0; /* we set the current DTS to an unspecified origin */
    }
}

int AsfExtractor::av_seek_frame(AVFormatContext *s, int stream_index, int64_t timestamp)
{
    int ret;
    
    av_read_frame_flush(s);
	
    /* first, we try the format specific seek */
    if (1/*s->iformat->read_seek*/)
        ret = asf_read_seek/*s->iformat->read_seek*/(s, stream_index, timestamp);
    else
        ret = -1;
    if (ret >= 0) 
	{
        return 0;
    }
    
    return av_seek_frame_generic(s, stream_index, timestamp);

}




static int is_raw_stream(AVFormatContext *s)
{
    AVStream *st;
	
    if (s->nb_streams != 1)
        return 0;
    st = s->streams[0];
    if (!st->need_parsing)
        return 0;
    return 1;
}

 void AsfExtractor::av_build_index_raw(AVFormatContext *s)
{
    AVPacket pkt1, *pkt = &pkt1;
    int ret;
    AVStream *st;
	
    st = s->streams[0];
    av_read_frame_flush(s);
    url_fseek(&s->pb, s->data_offset, SEEK_SET);
	
    for(;;) 
	{
        ret = av_read_frame(s, pkt);
        if (ret < 0)
            break;
        if (pkt->stream_index == 0 && st->parser &&
            (pkt->flags & PKT_FLAG_KEY))
		{
            av_add_index_entry(st, st->parser->frame_offset, pkt->dts, 
				0, AVINDEX_KEYFRAME);
        }
        av_free_packet(pkt);
    }
}

int AsfExtractor::av_seek_frame_generic(AVFormatContext *s, 
                                 int stream_index, int64_t timestamp)
{
    int index;
    AVStream *st;
    AVIndexEntry *ie;
	
    if (!s->index_built) 
	{
        if (is_raw_stream(s)) 
		{
            av_build_index_raw(s);
        } 
		else 
		{
            return -1;
        }
        s->index_built = 1;
    }
	
    if (stream_index < 0)
        stream_index = 0;
    st = s->streams[stream_index];
    index = av_index_search_timestamp(st, timestamp);
    if (index < 0)
        return -1;
	
    /* now we have found the index, we can seek */
    ie = &st->index_entries[index];
    av_read_frame_flush(s);
    url_fseek(&s->pb, ie->pos, SEEK_SET);
    st->cur_dts = ie->timestamp;
    return 0;
}

int AsfExtractor::url_fgetc(ByteIOContext *s)
{
    if (s->buf_ptr < s->buf_end)
	{
        return *s->buf_ptr++;
    } 
	else 
	{
        fill_buffer(s);
        if (s->buf_ptr < s->buf_end)
            return *s->buf_ptr++;
        else
            return URL_EOF;
    }
}

unsigned int AsfExtractor::get_be16(ByteIOContext *s)
{
    unsigned int val;
    val = get_byte(s) << 8;
    val |= get_byte(s);
    return val;
}

unsigned int AsfExtractor::get_be32(ByteIOContext *s)
{
    unsigned int val;
    val = get_byte(s) << 24;
    val |= get_byte(s) << 16;
    val |= get_byte(s) << 8;
    val |= get_byte(s);
    return val;
}
/*
double get_be64_double(ByteIOContext *s)
{
    union
	{
        double d;
        uint64_t ull;
    } u;

    u.ull = get_be64(s);
    return u.d;
}
*/ //by ren
char *AsfExtractor::get_strz(ByteIOContext *s, char *buf, int maxlen)
{
    int i = 0;
    char c;

    while ((c = get_byte(s))) 
	{
        if (i < maxlen-1)
            buf[i++] = c;
    }
    
    buf[i] = 0; /* Ensure null terminated, but may be truncated */

    return buf;
}

uint64_t AsfExtractor::get_be64(ByteIOContext *s)
{
    uint64_t val;
    val = (uint64_t)get_be32(s) << 32;
    val |= (uint64_t)get_be32(s);
    return val;
}



char *AsfExtractor::url_fgets(ByteIOContext *s, char *buf, int buf_size)
{
    int c;
    char *q;

    c = url_fgetc(s);
    if (c == EOF)
        return NULL;
    q = buf;
    for(;;) 
	{
        if (c == EOF || c == '\n')
            break;
        if ((q - buf) < buf_size - 1)
            *q++ = c;
        c = url_fgetc(s);
    }
    if (buf_size > 0)
        *q = '\0';
    return buf;
}

static int url_fget_max_packet_size(ByteIOContext *s)
{
    return s->max_packet_size;
}

int AsfExtractor::strstart(const char *str, const char *val, const char **ptr)
{
    const char *p, *q;
    p = str;
    q = val;
    while (*q != '\0') 
	{
        if (*p != *q)
            return 0;
        p++;
        q++;
    }
    if (ptr)
        *ptr = p;
    return 1;
}

char *AsfExtractor::pstrcat(char *buf, int buf_size, const char *s)
{
    int len;
    len = strlen(buf);
    if (len < buf_size) 
        pstrcpy(buf + len, buf_size - len, s);
    return buf;
}


offset_t AsfExtractor::url_fseek(ByteIOContext *s, offset_t offset, int whence)
{
#if 1
    offset_t offset1;
    if (whence != SEEK_CUR && whence != SEEK_SET)
       // return -EINVAL;
	   return -1; //jacky 2006/10/18
    {
        if (whence == SEEK_CUR)
		{
            offset1 = s->pos - (s->buf_end - s->buffer) + (s->buf_ptr - s->buffer);
            if (offset == 0)
                return offset1;
            offset += offset1;
        }
        offset1 = offset - (s->pos - (s->buf_end - s->buffer));
        if (offset1 >= 0 && offset1 <= (s->buf_end - s->buffer)) 
		{
            /* can do the seek inside the buffer */
            s->buf_ptr = s->buffer + offset1;
        } 
		else 
		{
            if (0/*!s->seek*/)
                //return -EPIPE;
			    return -1; //jacky 2006/10/18
            s->buf_ptr = s->buffer;
            s->buf_end = s->buffer;
            file_seek/*s->seek*/((URLContext *)s->opaque, offset, SEEK_SET);
            s->pos = offset;
        }
        s->eof_reached = 0;
    }
    return offset;

#endif
}

int AsfExtractor::url_fclose(ByteIOContext *s)
{
    URLContext *h =(URLContext*) s->opaque;
    av_free(s->buffer);
    memset(s, 0, sizeof(ByteIOContext));
    return url_close(h);
}

int AsfExtractor::url_close(URLContext *h)
{
    int ret;
    ret = /*h->prot->url_close*/file_close(h);
    av_free(h);
    return ret;
}

offset_t AsfExtractor::url_ftell(ByteIOContext *s)
{
    return url_fseek(s, 0, SEEK_CUR);
}

void AsfExtractor::url_fskip(ByteIOContext *s, offset_t offset)
{
    url_fseek(s, offset, SEEK_CUR);
}

int AsfExtractor::url_feof(ByteIOContext *s)
{
	//if(mOffset==mFileSize)
	//	s->eof_reached=1;
    return s->eof_reached;
}

status_t AsfExtractor::seekToTime(int64_t timeUs)
{
	return asf_read_seek(ic, audio_index, timeUs);
    
}


int AsfExtractor::file_open(URLContext *h, const char *filename, int flags)
{
#if 0	
    int access=0x8000;
    int fd;
    strstart(filename, "file:", &filename);

    if (flags & URL_WRONLY)
	{
        access = O_CREAT | O_TRUNC | O_WRONLY;
    } else 
	{
        access = O_RDONLY;
    }
#if defined(CONFIG_WIN32) || defined(CONFIG_OS2) || defined(__CYGWIN__)
    access |= O_BINARY;
#endif

    fd = open(filename, access, 0666);
    if(fd<0)
       // return -ENOENT;
		return -1;  //jacky 2006/10/18
    h->priv_data = (void *)fd;
#else
    return 0;
#endif
}

/* XXX: use llseek */
offset_t AsfExtractor::file_seek(URLContext *h, offset_t pos, int whence)
{
#if 0
    int fd = (int)h->priv_data;
#ifdef CONFIG_WIN32
    return _lseeki64(fd, pos, whence);
#else
    return lseek(fd, pos, whence);
#endif
#else
   if(whence==SEEK_SET){
   	     ALOGI("%s %d SEEK_SET:pos=%lld \n",__FUNCTION__,__LINE__,pos);
         mOffset=pos;
   }else if(whence==SEEK_CUR){
         ALOGI("%s %d SEEK_CUR:pos=%lld \n",__FUNCTION__,__LINE__,pos);
         mOffset+=pos;
   }else if(whence==SEEK_END){
         ALOGI("%s %d SEEK_END:pos=%lld mFileSize=%lld\n",__FUNCTION__,__LINE__,pos,mFileSize);
         mOffset=mFileSize+pos;
   }
   return mOffset;
#endif
   
}

int AsfExtractor::file_read(URLContext *h, unsigned char *buf, int size)
{
    int readsize=0;
	readsize=mSource->readAt(mOffset,buf,size);
	if(readsize!=size)
		ALOGI("NOTE:%s %d readsize=%d not equals size=%d\n",__FUNCTION__,__LINE__,readsize,size);
	mOffset+=readsize;
	return readsize;
}

int AsfExtractor::file_close(URLContext *h)
{
#if 0
    int fd = (int)h->priv_data;
    return close(fd);
#else
	return 0;
#endif
    
}

//-------------------------------------------
AsfExtractor::AsfExtractor(const sp<DataSource> &source)
{
	cc=NULL;
	ic=NULL;
	mOffset=0;
	mFileSize=0;
	mSource=source;
	audio_index=-1;
	mMeta=NULL;
	mInitCheck=-1;
	
	mSource->getSize(&mFileSize);
	if(av_open_input_file(&ic, NULL, NULL, 0, NULL) < 0){
		ALOGI("open input file failed!\n");
		return;
	}	
	int i;
    for(i = 0; i < ic->nb_streams; i++){
		cc = &ic->streams[i]->codec;
		if(cc->codec_type == CODEC_TYPE_AUDIO){
			audio_index = i;
            break;
		}
	}
	
	av_find_stream_info(ic);
	mMeta = new MetaData;    
	//------------------------------
	//set samplingrate: 
	//set channel        :
	//set blockalign     :
	if(cc->codec_tag==0x162){
	   mMeta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_WMAPRO);
	}else if(cc->codec_tag==0x160 ||cc->codec_tag==0x161){
	   mMeta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_WMA);
	}else if(cc->codec_tag==0x566F){
	   mMeta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_VORBIS);
	}else{
	   ALOGI("not add codec tag 0x%x \n",cc->codec_tag);
	}
	mMeta->setInt32(kKeySampleRate,cc->sample_rate);
	mMeta->setInt32(kKeyChannelCount,cc->channels);
	mMeta->setInt32(kKeyBitRate,cc->bit_rate);
	mMeta->setInt32(kKeyCodecID,cc->codec_id);
	mMeta->setData(kKeyExtraData,0,(char*)cc->extradata,cc->extradata_size);
	mMeta->setInt32(kKeyExtraDataSize,cc->extradata_size);
	mMeta->setInt32(kKeyBlockAlign,cc->block_align);
	mMeta->setInt64(kKeyDuration, ic->duration);
	mInitCheck=OK;
	ALOGI("%s %d :samplerate =%d \n",__FUNCTION__,__LINE__,cc->sample_rate);
	ALOGI("%s %d :channels   =%d \n",__FUNCTION__,__LINE__,cc->channels);
	ALOGI("%s %d :bit_rate   =%d \n",__FUNCTION__,__LINE__,cc->bit_rate);
	ALOGI("%s %d :codec_tag  =%d \n",__FUNCTION__,__LINE__,cc->codec_tag);
	ALOGI("%s %d :codec_id  =%d \n",__FUNCTION__,__LINE__,cc->codec_id);
	ALOGI("%s %d :exdatsize  =%d \n",__FUNCTION__,__LINE__,cc->extradata_size);
	ALOGI("%s %d :block_align=%d \n",__FUNCTION__,__LINE__,cc->block_align);
	ALOGI("%s %d :duration   =%lld(us) \n",__FUNCTION__,__LINE__,ic->duration);
	//------------------------------
	
	
}

AsfExtractor::~AsfExtractor()
{
	if(ic)
		av_close_input_file(ic);

}

size_t AsfExtractor::countTracks()
{
    return mInitCheck != OK ? 0 : 1;


}

sp<IMediaSource> AsfExtractor::getTrack(size_t index)
{
    if (index >= 1) {
        return NULL;
    }

    return new AsfSource(this);

}

 sp<MetaData> AsfExtractor::getTrackMetaData(size_t index, uint32_t flags)
{
    if (index >= 1) 
        return NULL;
    return mMeta;
   
}

 sp<MetaData> AsfExtractor::getMetaData()
{
	return mMeta;
}


static int asf_probe(AVProbeData *pd)
{
    GUID g;
    const unsigned char *p;
    int i;
    /* check file header */
    if (pd->buf_size <= 32)
        return 0;
    p = pd->buf;
    g.v1 = p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
    p += 4;
    g.v2 = p[0] | (p[1] << 8);
    p += 2;
    g.v3 = p[0] | (p[1] << 8);
    p += 2;
    for(i=0;i<8;i++)
        g.v4[i] = *p++;
    if (!memcmp(&g, &asf_header, sizeof(GUID)))
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}


bool SniffAsf(const sp<DataSource> &source, String8 *mimeType, float *confidence,
        sp<AMessage> *)
{
	ALOGI("%s %d\n",__FUNCTION__,__LINE__);
	AVProbeData pd;
	unsigned char tmp[PROBE_BUF_SIZE];
	pd.buf=tmp;
	pd.buf_size=PROBE_BUF_SIZE;

    if (source->readAt(0, tmp, PROBE_BUF_SIZE) < PROBE_BUF_SIZE) 
        return false;
    if(asf_probe(&pd)>0){
        mimeType->setTo(MEDIA_MIMETYPE_AUDIO_WMA);
        *confidence = 0.01f;
		//*confidence = 1.0f;
        return true;
    }else{
        return false;
    }
}

}

