#ifndef FFMPEG_AVI_H
#define FFMPEG_AVI_H
#if 0
#include "avcodec.h"

offset_t start_tag(ByteIOContext *pb, const char *tag);
void end_tag(ByteIOContext *pb, offset_t start);




int put_wav_header(ByteIOContext *pb, AVCodecContext *enc);
int wav_codec_get_id(unsigned int tag, int bps);
void get_wav_header(ByteIOContext *pb, AVCodecContext *codec, int size); 

extern const CodecTag codec_bmp_tags[];
extern const CodecTag codec_wav_tags[];

unsigned int codec_get_tag(const CodecTag *tags, int id);
enum CodecID codec_get_id(const CodecTag *tags, unsigned int tag);

unsigned int codec_get_wav_tag(int id);

enum CodecID codec_get_wav_id(unsigned int tag);
#endif
#endif /* FFMPEG_AVI_H */
