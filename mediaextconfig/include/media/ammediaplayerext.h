/*
 * Copyright (C) 2007 The Android Open Source Project
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

#ifndef ANDROID_AMMEDIAPLAYER_H
#define ANDROID_AMMEDIAPLAYER_H

namespace android {

	enum  {
		AMLOGIC_PLAYER = 110,
		AMSUPER_PLAYER = 111,
		AMNUPLAYER = 112,
	};


	enum {
		/*enum media_event_type*/
		MEDIA_BLURAY_INFO       = 203,
	};
	enum {

		//amlogic extend warning message type,just for notify,never force to exit player.
		//amlogic extend warning message type,just for notify,never force to exit player.
		MEDIA_INFO_AMLOGIC_BASE = 8000,
		MEDIA_INFO_AMLOGIC_VIDEO_NOT_SUPPORT=MEDIA_INFO_AMLOGIC_BASE+1,
		MEDIA_INFO_AMLOGIC_AUDIO_NOT_SUPPORT = MEDIA_INFO_AMLOGIC_BASE+2,
		MEDIA_INFO_AMLOGIC_NO_VIDEO = MEDIA_INFO_AMLOGIC_BASE+3,
		MEDIA_INFO_AMLOGIC_NO_AUDIO = MEDIA_INFO_AMLOGIC_BASE+4,
		MEDIA_INFO_AMLOGIC_SHOW_DTS_ASSET = MEDIA_INFO_AMLOGIC_BASE+5,
		MEDIA_INFO_AMLOGIC_SHOW_DTS_EXPRESS = MEDIA_INFO_AMLOGIC_BASE+6,
		MEDIA_INFO_AMLOGIC_SHOW_DTS_HD_MASTER_AUDIO = MEDIA_INFO_AMLOGIC_BASE+7,
		MEDIA_INFO_AMLOGIC_SHOW_AUDIO_LIMITED = MEDIA_INFO_AMLOGIC_BASE+8,
		MEDIA_INFO_AMLOGIC_SHOW_DTS_MULASSETHINT=MEDIA_INFO_AMLOGIC_BASE+9,
		MEDIA_INFO_AMLOGIC_SHOW_DTS_HPS_NOTSUPPORT=MEDIA_INFO_AMLOGIC_BASE+10,
		MEDIA_INFO_AMLOGIC_BLURAY_STREAM_PATH = MEDIA_INFO_AMLOGIC_BASE+11,
                MEDIA_INFO_AMLOGIC_SHOW_DOLBY_VISION = MEDIA_INFO_AMLOGIC_BASE+12,
		//notify java app the download bitrate
		MEDIA_INFO_DOWNLOAD_BITRATE = 0x9001,
		//notify java app the buffering circle percent
		MEDIA_INFO_BUFFERING_PERCENT = 0x9002,

	};
	enum {

		//AML Video INFO string,set only
		KEY_PARAMETER_AML_VIDEO_POSITION_INFO = 2000,

		//PLAYER TYPE STRING
		KEY_PARAMETER_AML_PLAYER_TYPE_STR = 2001,
		//PLAYER VIDEO	OUT/TYPE
		//public static final int VIDEO_OUT_SOFT_RENDER = 0;
		//public static final int VIDEO_OUT_HARDWARE	= 1;
		KEY_PARAMETER_AML_PLAYER_VIDEO_OUT_TYPE = 2002,

		//amlogic private API,set only.
		KEY_PARAMETER_AML_PLAYER_SWITCH_SOUND_TRACK 	= 2003, 	// string, refer to lmono,rmono,stereo, set only
		KEY_PARAMETER_AML_PLAYER_SWITCH_AUDIO_TRACK 	= 2004, 	// string, refer to audio track index, set only
		KEY_PARAMETER_AML_PLAYER_TRICKPLAY_FORWARD	= 2005, 	// string, refer to forward:speed
		KEY_PARAMETER_AML_PLAYER_TRICKPLAY_BACKWARD 	= 2006, 	// string, refer to  backward:speed
		KEY_PARAMETER_AML_PLAYER_FORCE_HARD_DECODE	= 2007, 	// string, refer to mp3,etc.
		KEY_PARAMETER_AML_PLAYER_FORCE_SOFT_DECODE	= 2008, 	// string, refer to mp3,etc.
		KEY_PARAMETER_AML_PLAYER_GET_MEDIA_INFO 	= 2009, 	// string, get media info
		KEY_PARAMETER_AML_PLAYER_FORCE_SCREEN_MODE	= 2010, 	// string, set screen mode
		KEY_PARAMETER_AML_PLAYER_SET_DISPLAY_MODE	= 2011, 	// string, set display mode
		KEY_PARAMETER_AML_PLAYER_GET_DTS_ASSET_TOTAL	= 2012, 	// string, get dts asset total number
		KEY_PARAMETER_AML_PLAYER_SET_DTS_ASSET		= 2013, 	// string, set dts asset
		KEY_PARAMETER_AML_PLAYER_SWITCH_VIDEO_TRACK 	= 2015, 	//string,refer to video track index,set only
		KEY_PARAMETER_AML_PLAYER_SET_TRICKPLAY_MODE     = 2016,     //string,refer to video ff/fb flag,set only

		KEY_PARAMETER_AML_PLAYER_HWBUFFER_STATE 	= 3001, 	// string,refer to stream buffer info
		KEY_PARAMETER_AML_PLAYER_RESET_BUFFER		= 8000, 	// top level seek..player need to reset & clearbuffers
		KEY_PARAMETER_AML_PLAYER_FREERUN_MODE		= 8002, 	// play ASAP...
		KEY_PARAMETER_AML_PLAYER_ENABLE_OSDVIDEO	= 8003, 	// play enable osd video for this player....
		KEY_PARAMETER_AML_PLAYER_DIS_AUTO_BUFFER	= 8004, 	// play ASAP...
		KEY_PARAMETER_AML_PLAYER_ENA_AUTO_BUFFER	= 8005, 	// play ASAP...
		KEY_PARAMETER_AML_PLAYER_USE_SOFT_DEMUX 	= 8006, 	// play use soft demux
		KEY_PARAMETER_AML_PLAYER_PR_CUSTOM_DATA 	= 9001, 	// string, playready, set only
	};
	enum video_out_type {
		VIDEO_OUT_SOFT_RENDER = 0,
		VIDEO_OUT_HARDWARE    = 1,
	};
	enum {
		INVOKE_ID_NETWORK_GET_LPBUF_BUFFERED_SIZE = 8,
		INVOKE_ID_NETWORK_GET_STREAMBUF_BUFFERED_SIZE = 9,
		INVOKE_ID_SET_TRACK_VOLUME = 10,
	};
}; // namespace android

#endif

