#ifndef __ASFEXTRACTOR_H__
#define __ASFEXTRACTOR_H__

#include "../AsfExtractor/avformat.h"
//#include "../AsfExtractor/file.h"
//#include "../AsfExtractor/asf.h"
#include <utils/Errors.h>
#include <media/stagefright/MediaExtractor.h>

namespace android {

typedef struct URLContext URLContext;

struct AMessage;
class  DataSource;
class  String8;
struct AsfSource;

class AsfExtractor :public MediaExtractor
{
    
public:	
	//--------------this part getted by porting-----------------
    int av_open_input_file(AVFormatContext **ic_ptr, const char *filename, 
                       AVInputFormat *fmt,
                       int buf_size,
                       AVFormatParameters *ap);
	int url_fopen(ByteIOContext *s, const char *filename, int flags);
	
	int url_open(URLContext **puc, const char *filename, int flags);
	int get_buffer(ByteIOContext *s, unsigned char *buf, int size);
	void fill_buffer(ByteIOContext *s);
	int av_open_input_stream(AVFormatContext **ic_ptr, 
                         ByteIOContext *pb, const char *filename, 
                         AVInputFormat *fmt, AVFormatParameters *ap);
	AVStream * av_new_stream(AVFormatContext *s, int id);
	int asf_read_header(AVFormatContext *s, AVFormatParameters *ap);
	int get_byte(ByteIOContext *s);
	unsigned int get_le16(ByteIOContext *s);
	unsigned int get_le32(ByteIOContext *s);
	uint64_t get_le64(ByteIOContext *s);
	
	void get_str16_nolen(ByteIOContext *pb, int len, char *buf, int buf_size);
	void get_guid(ByteIOContext *s, GUID *g);
	
	void get_wav_header(ByteIOContext *pb, AVCodecContext *codec, int size);
	int has_codec_parameters(AVCodecContext *enc);
	int av_dup_packet(AVPacket *pkt);
	int av_find_stream_info(AVFormatContext *ic);
	offset_t url_filesize(URLContext *h);
	void av_estimate_timings(AVFormatContext *ic);
	int get_audio_frame_size(AVCodecContext *enc, int size);
	 void compute_frame_duration(int *pnum, int *pden,
                                   AVFormatContext *s, AVStream *st, 
                                   AVCodecParserContext *pc, AVPacket *pkt);
	 void compute_pkt_fields(AVFormatContext *s, AVStream *st, 
                               AVCodecParserContext *pc, AVPacket *pkt);
	 int av_read_packet(AVFormatContext *s, AVPacket *pkt);
	 int asf_read_packet(AVFormatContext *s, AVPacket *pkt);
	 int asf_get_packet(AVFormatContext *s);
	 int av_read_frame_internal(AVFormatContext *s, AVPacket *pkt);
	 int av_parser_parse(AVCodecParserContext *s, 
                    AVCodecContext *avctx,
                    uint8_t **poutbuf, int *poutbuf_size, 
                    const uint8_t *buf, int buf_size,
                    int64_t pts, int64_t dts);
	 int64_t convert_timestamp_units(AVFormatContext *s,
												int64_t *plast_pkt_pts,
												int *plast_pkt_pts_frac,
												int64_t *plast_pkt_stream_pts,
												int64_t pts);
	 int try_decode_frame(AVStream *st, const uint8_t *data, int size);
	 int avcodec_close(AVCodecContext *avctx);
	 int avcodec_decode_audio(AVCodecContext *avctx, int16_t *samples, 
                         int *frame_size_ptr,
                         uint8_t *buf, int buf_size);
	 int avcodec_open(AVCodecContext *avctx, AVCodec *codec);
	 AVCodec *avcodec_find_decoder(enum CodecID id);
	 AVCodecParserContext *av_parser_init(int codec_id);
	 int av_read_frame(AVFormatContext *s, AVPacket *pkt);
	 void asf_reset_header(AVFormatContext *s);
	 int64_t asf_read_pts(AVFormatContext *s, int64_t *ppos, int stream_index);
	 int av_index_search_timestamp(AVStream *st, int wanted_timestamp);
	 int av_add_index_entry(AVStream *st,
					   int64_t pos, int64_t timestamp, int distance, int flags);
	 int asf_read_close(AVFormatContext *s);
	 int asf_read_seek(AVFormatContext *s, int stream_index, int64_t pts);
	 void av_close_input_file(AVFormatContext *s);
	 int av_seek_frame(AVFormatContext *s, int stream_index, int64_t timestamp);
	  void av_build_index_raw(AVFormatContext *s);
	  int av_seek_frame_generic(AVFormatContext *s, 
                                 int stream_index, int64_t timestamp);
	  int url_fgetc(ByteIOContext *s);
	  unsigned int get_be16(ByteIOContext *s);
	  unsigned int get_be32(ByteIOContext *s);
	  char *get_strz(ByteIOContext *s, char *buf, int maxlen);
	  uint64_t get_be64(ByteIOContext *s);
	  char *url_fgets(ByteIOContext *s, char *buf, int buf_size);
	  int strstart(const char *str, const char *val, const char **ptr);
	  char *pstrcat(char *buf, int buf_size, const char *s);
	  
	  offset_t url_fseek(ByteIOContext *s, offset_t offset, int whence);
	  int url_fclose(ByteIOContext *s);
	  int url_close(URLContext *h);
	  offset_t url_ftell(ByteIOContext *s);
	  void url_fskip(ByteIOContext *s, offset_t offset);
	  int url_feof(ByteIOContext *s);
	  int file_open(URLContext *h, const char *filename, int flags);
	  offset_t file_seek(URLContext *h, offset_t pos, int whence);
	  int file_read(URLContext *h, unsigned char *buf, int size);
	  int file_close(URLContext *h);
	//--------------------------------------------------
    AsfExtractor(const sp<DataSource> &source);

    virtual size_t countTracks();
    virtual sp<MediaSource> getTrack(size_t index);
    virtual sp<MetaData> getTrackMetaData(size_t index, uint32_t flags);
    virtual sp<MetaData> getMetaData();
	status_t seekToTime(int64_t timeUs);
protected:
    virtual ~AsfExtractor();
private:
	AVCodecContext  *cc;
	AVFormatContext *ic;
	int             audio_index;
	
	off64_t mOffset;
	off64_t mFileSize;
	sp<DataSource> mSource;
    friend struct AsfSource;
	sp<MetaData> mMeta;
    status_t mInitCheck;
    AsfExtractor(const AsfExtractor &);
    AsfExtractor &operator=(const AsfExtractor &);
     
};

bool SniffAsf(
        const sp<DataSource> &source, String8 *mimeType, float *confidence,
        sp<AMessage> *);
}  // namespace android
#endif
