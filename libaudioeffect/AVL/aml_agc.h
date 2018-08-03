/*
 * Copyright (C) 2017 Amlogic Corporation.
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

#ifndef _AMLAGC_H_
#define _AMLAGC_H_

#ifdef __cplusplus
extern "C"  {
#endif

typedef struct {
    int         agc_enable;
    int         response_time;
    int         release_time;
    int         sample_max[2];
    int         counter[2];
    float       gain[2];
    float       CompressionRatio;
    int         silence_counter[2];
    long        peak;
    long        silence_threshold;
    long        active_threshold;
    float       sample_sum[2];
    long        last_sample[2];
    float       average_level[2];
    float       cross_zero_num[2];
}AmlAGC;

AmlAGC* NewAmlAGC(float peak_level, float dynamic_theshold, float noise_threshold, int response_time, int release_time);
void DoAmlAGC(AmlAGC *agc, void *buffer, int len);
void DeleteAmlAGC(AmlAGC *agc);
int SetAmlAGC(AmlAGC *agc, float peak_level, float dynamic_threshold, float noise_threshold, int response_time, int release_time);
#ifdef __cplusplus
}
#endif

#endif
