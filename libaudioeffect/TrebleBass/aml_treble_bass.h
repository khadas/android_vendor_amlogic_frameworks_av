#ifndef _AML_Treble_Bass_H_
#define _AML_Treble_Bass_H_
#include <stdint.h>

#ifdef __cplusplus
extern "C"  {
#endif

int audio_Treble_Bass_process(int16_t *input, int16_t *output, int frame_length);
void audio_Treble_Bass_init(float bass_gain, float treble_gain);

#ifdef __cplusplus
}
#endif

#endif
