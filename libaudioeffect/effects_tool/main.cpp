#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>
#include <media/AudioEffect.h>

#ifdef LOG
#undef LOG
#endif
#define LOG(x...) printf("[AudioEffect] " x)

using namespace android;

//-----------Balance parameters-------------------------------
typedef struct Balance_param_s {
    effect_param_t param;
    uint32_t command;
    union {
        int32_t v;
        float f;
    };
} Balance_param_t;

typedef enum {
    BALANCE_PARAM_LEVEL = 0,
    BALANCE_PARAM_ENABLE,
    BALANCE_PARAM_LEVEL_NUM,
} Balance_params;

Balance_param_t gBalanceParam[] = {
    {{0, 4, 4}, BALANCE_PARAM_LEVEL, {25}},
    {{0, 4, 4}, BALANCE_PARAM_ENABLE, {1}},
    {{0, 4, 4}, BALANCE_PARAM_LEVEL_NUM, {51}},
};

int balance_level_num = 0;

const char *BalanceStatusstr[] = {"Disable", "Enable"};
//-----------TruSurround parameters---------------------------
typedef struct SRS_param_s {
    effect_param_t param;
    uint32_t command;
    union {
        uint32_t v;
        float f;
    };
} SRS_param_t;

typedef enum {
    SRS_PARAM_MODE = 0,
    SRS_PARAM_DIALOGCLARTY_MODE,
    SRS_PARAM_SURROUND_MODE,
    SRS_PARAM_VOLUME_MODE,
    SRS_PARAM_ENABLE,
    SRS_PARAM_TRUEBASS_ENABLE,
    SRS_PARAM_TRUEBASS_SPKER_SIZE,
    SRS_PARAM_TRUEBASS_GAIN,
    SRS_PARAM_DIALOG_CLARITY_ENABLE,
    SRS_PARAM_DIALOGCLARTY_GAIN,
    SRS_PARAM_DEFINITION_ENABLE,
    SRS_PARAM_DEFINITION_GAIN,
    SRS_PARAM_SURROUND_ENABLE,
    SRS_PARAM_SURROUND_GAIN,
    SRS_PARAM_INPUT_GAIN,
    SRS_PARAM_OUTPUT_GAIN,
    SRS_PARAM_OUTPUT_GAIN_COMP,
} SRS_params;

SRS_param_t gSRSParam[] = {
    {{0, 4, 4}, SRS_PARAM_MODE, {0}},
    {{0, 4, 4}, SRS_PARAM_DIALOGCLARTY_MODE, {0}},
    {{0, 4, 4}, SRS_PARAM_SURROUND_MODE, {0}},
    {{0, 4, 4}, SRS_PARAM_VOLUME_MODE, {0}},
    {{0, 4, 4}, SRS_PARAM_ENABLE, {1}},
    {{0, 4, 4}, SRS_PARAM_TRUEBASS_ENABLE, {0}},
    {{0, 4, 4}, SRS_PARAM_TRUEBASS_SPKER_SIZE, {0}},
    {{0, 4, 4}, SRS_PARAM_TRUEBASS_GAIN, {0}},
    {{0, 4, 4}, SRS_PARAM_DIALOG_CLARITY_ENABLE, {0}},
    {{0, 4, 4}, SRS_PARAM_DIALOGCLARTY_GAIN, {0}},
    {{0, 4, 4}, SRS_PARAM_DEFINITION_ENABLE, {0}},
    {{0, 4, 4}, SRS_PARAM_DEFINITION_GAIN, {0}},
    {{0, 4, 4}, SRS_PARAM_SURROUND_ENABLE, {0}},
    {{0, 4, 4}, SRS_PARAM_SURROUND_GAIN, {0}},
    {{0, 4, 4}, SRS_PARAM_INPUT_GAIN, {0}},
    {{0, 4, 4}, SRS_PARAM_OUTPUT_GAIN, {0}},
    {{0, 4, 4}, SRS_PARAM_OUTPUT_GAIN_COMP, {0}},
};

const char *SRSModestr[] = {"Standard", "Music", "Movie", "Clear Voice", "Enhanced Bass", "Custom"};

const char *SRSStatusstr[] = {"Disable", "Enable"};
const char *SRSTrueBassstr[] = {"Disable", "Enable"};
const char *SRSDialogClaritystr[] = {"Disable", "Enable"};
const char *SRSDefinitionstr[] = {"Disable", "Enable"};
const char *SRSSurroundstr[] = {"Disable", "Enable"};

const char *SRSDialogClarityModestr[] = {"OFF", "LOW", "HIGH"};
const char *SRSSurroundModestr[] = {"ON", "OFF"};

//-------------TrebleBass parameters--------------------------
typedef struct HPEQ_param_s {
    effect_param_t param;
    uint32_t command;
    union {
        uint32_t v;
        float f;
    };
} HPEQ_param_t;

typedef enum {
    HPEQ_PARAM_BASS = 0,
    HPEQ_PARAM_TREBLE,
    HPEQ_PARAM_ENABLE,
} HPEQ_params;

HPEQ_param_t gHPEQParam[] = {
    {{0, 4, 4}, HPEQ_PARAM_BASS, {0}},
    {{0, 4, 4}, HPEQ_PARAM_TREBLE, {0}},
    {{0, 4, 4}, HPEQ_PARAM_ENABLE, {1}},
};

const char *HPEQStatusstr[] = {"Disable", "Enable"};
//-------UUID--------------------------------
typedef enum {
    EFFECT_BALANCE = 0,
    EFFECT_SRS,
    EFFECT_HPEQ,
    EFFECT_MAX,
} EFFECT_params;

effect_uuid_t gEffectStr[] = {
    {0x6f33b3a0, 0x578e, 0x11e5, 0x892f, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}, // 0:Balance
    {0x8a857720, 0x0209, 0x11e2, 0xa9d8, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}, // 1:TruSurround
    {0x76733af0, 0x2889, 0x11e2, 0x81c1, {0x08, 0x00, 0x20, 0x0c, 0x9a, 0x66}}, // 2:TrebleBass
};

static int Balance_effect_func(AudioEffect* gAudioEffect, int gParamIndex, int gParamValue)
{
    if (balance_level_num == 0) {
        gAudioEffect->getParameter(&gBalanceParam[BALANCE_PARAM_LEVEL_NUM].param);
        balance_level_num = gBalanceParam[BALANCE_PARAM_LEVEL_NUM].v;
        LOG("Balance: Level size = %d\n", balance_level_num);
    }
    switch (gParamIndex) {
    case BALANCE_PARAM_LEVEL:
        if (gParamValue  < 0 || gParamValue > ((balance_level_num - 1) << 1)) {
            LOG("Balance: Level gParamValue = %d invalid\n", gParamValue);
            return -1;
        }
        gBalanceParam[gParamIndex].v = gParamValue;
        gAudioEffect->setParameter(&gBalanceParam[gParamIndex].param);
        gAudioEffect->getParameter(&gBalanceParam[gParamIndex].param);
        LOG("Balance: Level is %d -> %d\n", gParamValue, gBalanceParam[gParamIndex].v);
        return 0;
    case BALANCE_PARAM_ENABLE:
        if (gParamValue < 0 || gParamValue > 1) {
            LOG("Balance: Status gParamValue = %d invalid\n", gParamValue);
            return -1;
        }
        gBalanceParam[gParamIndex].v = gParamValue;
        gAudioEffect->setParameter(&gBalanceParam[gParamIndex].param);
        gAudioEffect->getParameter(&gBalanceParam[gParamIndex].param);
        LOG("Balance: Status is %d -> %s\n", gParamValue, BalanceStatusstr[gBalanceParam[gParamIndex].v]);
        return 0;
    default:
        LOG("Balance: ParamIndex = %d invalid\n", gParamIndex);
        return -1;
    }
}

static int SRS_effect_func(AudioEffect* gAudioEffect, int gParamIndex, int gParamValue, float gParamScale)
{
    switch (gParamIndex) {
    case SRS_PARAM_MODE:
        if (gParamValue < 0 || gParamValue > 5) {
            LOG("TruSurround: Mode gParamValue = %d invalid\n", gParamValue);
            return -1;
        }
        gSRSParam[gParamIndex].v = gParamValue;
        gAudioEffect->setParameter(&gSRSParam[gParamIndex].param);
        gAudioEffect->getParameter(&gSRSParam[gParamIndex].param);
        LOG("TruSurround: Mode is %d -> %s\n", gParamValue, SRSModestr[gSRSParam[gParamIndex].v]);
        return 0;
    case SRS_PARAM_DIALOGCLARTY_MODE:
        gAudioEffect->getParameter(&gSRSParam[SRS_PARAM_DIALOG_CLARITY_ENABLE].param);
        if (gSRSParam[SRS_PARAM_DIALOG_CLARITY_ENABLE].v == 0) {
            LOG("TruSurround: Set Dialog Clarity Mode failed as Dialog Clarity is disabled\n");
            return -1;
        }
        if (gParamValue < 0 || gParamValue > 2) {
            LOG("TruSurround: Dialog Clarity Mode gParamValue = %d invalid\n", gParamValue);
            return -1;
        }
        gSRSParam[gParamIndex].v = gParamValue;
        gAudioEffect->setParameter(&gSRSParam[gParamIndex].param);
        gAudioEffect->getParameter(&gSRSParam[gParamIndex].param);
        LOG("TruSurround: Dialog Clarity Mode is %s -> %d\n", SRSDialogClarityModestr[gParamValue], gSRSParam[gParamIndex].v);
        return 0;
    case SRS_PARAM_SURROUND_MODE:
        gAudioEffect->getParameter(&gSRSParam[SRS_PARAM_SURROUND_ENABLE].param);
        if (gSRSParam[SRS_PARAM_SURROUND_ENABLE].v == 0) {
            LOG("TruSurround: Set Surround Mode failed as Surround is disabled\n");
            return -1;
        }
        if (gParamValue < 0 || gParamValue > 1) {
            LOG("TruSurround: Surround Mode gParamValue = %d invalid\n", gParamValue);
            return -1;
        }
        gSRSParam[gParamIndex].v = gParamValue;
        gAudioEffect->setParameter(&gSRSParam[gParamIndex].param);
        gAudioEffect->getParameter(&gSRSParam[gParamIndex].param);
        LOG("TruSurround: Surround Mode is %s -> %d\n", SRSSurroundModestr[gParamValue], gSRSParam[gParamIndex].v);
        return 0;
    case SRS_PARAM_VOLUME_MODE:
        return 0;
    case SRS_PARAM_ENABLE:
        if (gParamValue < 0 || gParamValue > 1) {
            LOG("TruSurround: Status gParamValue = %d invalid\n", gParamValue);
            return -1;
        }
        gSRSParam[gParamIndex].v = gParamValue;
        gAudioEffect->setParameter(&gSRSParam[gParamIndex].param);
        gAudioEffect->getParameter(&gSRSParam[gParamIndex].param);
        LOG("TruSurround: Status is %d -> %s\n", gParamValue, SRSStatusstr[gSRSParam[gParamIndex].v]);
        return 0;
    case SRS_PARAM_TRUEBASS_ENABLE:
        if (gParamValue < 0 || gParamValue > 1) {
            LOG("TruSurround: True Bass gParamValue = %d invalid\n", gParamValue);
            return -1;
        }
        gSRSParam[gParamIndex].v = gParamValue;
        gAudioEffect->setParameter(&gSRSParam[gParamIndex].param);
        gAudioEffect->getParameter(&gSRSParam[gParamIndex].param);
        LOG("TruSurround: True Bass is %d -> %s\n", gParamValue, SRSTrueBassstr[gSRSParam[gParamIndex].v]);
        return 0;
    case SRS_PARAM_TRUEBASS_SPKER_SIZE:
        gAudioEffect->getParameter(&gSRSParam[SRS_PARAM_TRUEBASS_ENABLE].param);
        if (gSRSParam[SRS_PARAM_TRUEBASS_ENABLE].v == 0) {
            LOG("TruSurround: Set True Bass Speaker Size failed as True Bass is disabled\n");
            return -1;
        }
        if (gParamValue < 0 || gParamValue > 7) {
            LOG("TruSurround: True Bass Speaker Size gParamValue = %d invalid\n", gParamValue);
            return -1;
        }
        gSRSParam[gParamIndex].v = gParamValue;
        gAudioEffect->setParameter(&gSRSParam[gParamIndex].param);
        gAudioEffect->getParameter(&gSRSParam[gParamIndex].param);
        LOG("TruSurround: True Bass Speaker Size is %d -> %d\n", gParamValue, gSRSParam[gParamIndex].v);
        return 0;
    case SRS_PARAM_TRUEBASS_GAIN:
        gAudioEffect->getParameter(&gSRSParam[SRS_PARAM_TRUEBASS_ENABLE].param);
        if (gSRSParam[SRS_PARAM_TRUEBASS_ENABLE].v == 0) {
            LOG("TruSurround: Set True Bass Gain failed as True Bass is disabled\n");
            return -1;
        }
        if (gParamScale < 0.0 || gParamScale > 1.0) {
            LOG("TruSurround: True Bass Gain gParamScale = %f invalid\n", gParamScale);
            return -1;
        }
        gSRSParam[gParamIndex].f = gParamScale;
        gAudioEffect->setParameter(&gSRSParam[gParamIndex].param);
        gAudioEffect->getParameter(&gSRSParam[gParamIndex].param);
        LOG("TruSurround: True Bass Gain %f -> %f\n", gParamScale, gSRSParam[gParamIndex].f);
        return 0;
    case SRS_PARAM_DIALOG_CLARITY_ENABLE:
        if (gParamValue < 0 || gParamValue > 1) {
            LOG("TruSurround: Dialog Clarity gParamValue = %d invalid\n", gParamValue);
            return -1;
        }
        gSRSParam[gParamIndex].v = gParamValue;
        gAudioEffect->setParameter(&gSRSParam[gParamIndex].param);
        gAudioEffect->getParameter(&gSRSParam[gParamIndex].param);
        LOG("TruSurround: Dialog Clarity is %d -> %s\n", gParamValue, SRSDialogClaritystr[gSRSParam[gParamIndex].v]);
        return 0;
    case SRS_PARAM_DIALOGCLARTY_GAIN:
        gAudioEffect->getParameter(&gSRSParam[SRS_PARAM_DIALOG_CLARITY_ENABLE].param);
        if (gSRSParam[SRS_PARAM_DIALOG_CLARITY_ENABLE].v == 0) {
            LOG("TruSurround: Set Dialog Clarity Gain failed as Dialog Clarity is disabled\n");
            return -1;
        }
        if (gParamScale < 0.0 || gParamScale > 1.0) {
            LOG("TruSurround: Dialog Clarity Gain gParamScale = %f invalid\n", gParamScale);
            return -1;
        }
        gSRSParam[gParamIndex].f = gParamScale;
        gAudioEffect->setParameter(&gSRSParam[gParamIndex].param);
        gAudioEffect->getParameter(&gSRSParam[gParamIndex].param);
        LOG("TruSurround: Dialog Clarity Gain %f -> %f\n", gParamScale, gSRSParam[gParamIndex].f);
        return 0;
    case SRS_PARAM_DEFINITION_ENABLE:
        if (gParamValue < 0 || gParamValue > 1) {
            LOG("TruSurround: Definition gParamValue = %d invalid\n", gParamValue);
            return -1;
        }
        gSRSParam[gParamIndex].v = gParamValue;
        gAudioEffect->setParameter(&gSRSParam[gParamIndex].param);
        gAudioEffect->getParameter(&gSRSParam[gParamIndex].param);
        LOG("TruSurround: Definition is %d -> %s\n", gParamValue, SRSDefinitionstr[gSRSParam[gParamIndex].v]);
        return 0;
    case SRS_PARAM_DEFINITION_GAIN:
        gAudioEffect->getParameter(&gSRSParam[SRS_PARAM_DEFINITION_ENABLE].param);
        if (gSRSParam[SRS_PARAM_DEFINITION_ENABLE].v == 0) {
            LOG("TruSurround: Set Definition Gain failed as Definition is disabled\n");
            return -1;
        }
        if (gParamScale < 0.0 || gParamScale > 1.0) {
            LOG("TruSurround: Definition Gain gParamScale = %f invalid\n", gParamScale);
            return -1;
        }
        gSRSParam[gParamIndex].f = gParamScale;
        gAudioEffect->setParameter(&gSRSParam[gParamIndex].param);
        gAudioEffect->getParameter(&gSRSParam[gParamIndex].param);
        LOG("TruSurround: Definition Gain %f -> %f\n", gParamScale, gSRSParam[gParamIndex].f);
        return 0;
    case SRS_PARAM_SURROUND_ENABLE:
        if (gParamValue < 0 || gParamValue > 1) {
            LOG("TruSurround: Surround gParamValue = %d invalid\n", gParamValue);
            return -1;
        }
        gSRSParam[gParamIndex].v = gParamValue;
        gAudioEffect->setParameter(&gSRSParam[gParamIndex].param);
        gAudioEffect->getParameter(&gSRSParam[gParamIndex].param);
        LOG("TruSurround: Surround is %d -> %s\n", gParamValue, SRSSurroundstr[gSRSParam[gParamIndex].v]);
        return 0;
    case SRS_PARAM_SURROUND_GAIN:
        gAudioEffect->getParameter(&gSRSParam[SRS_PARAM_SURROUND_ENABLE].param);
        if (gSRSParam[SRS_PARAM_SURROUND_ENABLE].v == 0) {
            LOG("TruSurround: Set Surround Gain failed as Surround is disabled\n");
            return -1;
        }
        if (gParamScale < 0.0 || gParamScale > 1.0) {
            LOG("TruSurround: Surround Gain gParamScale = %f invalid\n", gParamScale);
            return -1;
        }
        gSRSParam[gParamIndex].f = gParamScale;
        gAudioEffect->setParameter(&gSRSParam[gParamIndex].param);
        gAudioEffect->getParameter(&gSRSParam[gParamIndex].param);
        LOG("TruSurround: Surround Gain %f -> %f\n", gParamScale, gSRSParam[gParamIndex].f);
        return 0;
    case SRS_PARAM_INPUT_GAIN:
        if (gParamScale < 0.0 || gParamScale > 1.0) {
            LOG("TruSurround: Input Gain gParamScale = %f invalid\n", gParamScale);
            return -1;
        }
        gSRSParam[gParamIndex].f = gParamScale;
        gAudioEffect->setParameter(&gSRSParam[gParamIndex].param);
        gAudioEffect->getParameter(&gSRSParam[gParamIndex].param);
        LOG("TruSurround: Input Gain is %f -> %f\n", gParamScale, gSRSParam[gParamIndex].f);
        return 0;
    case SRS_PARAM_OUTPUT_GAIN:
        if (gParamScale < 0.0 || gParamScale > 1.0) {
            LOG("TruSurround: Output Gain gParamScale = %f invalid\n", gParamScale);
            return -1;
        }
        gSRSParam[gParamIndex].f = gParamScale;
        gAudioEffect->setParameter(&gSRSParam[gParamIndex].param);
        gAudioEffect->getParameter(&gSRSParam[gParamIndex].param);
        LOG("TruSurround: Output Gain is %f -> %f\n", gParamScale, gSRSParam[gParamIndex].f);
        return 0;
    case SRS_PARAM_OUTPUT_GAIN_COMP:
        if (gParamScale < -20.0 || gParamScale > 20.0) {
            LOG("TruSurround: Output Gain gParamScale = %f invalid\n", gParamScale);
            return -1;
        }
        gSRSParam[gParamIndex].f = gParamScale;
        gAudioEffect->setParameter(&gSRSParam[gParamIndex].param);
        gAudioEffect->getParameter(&gSRSParam[gParamIndex].param);
        LOG("TruSurround: Output Gain Comp is %f -> %f\n", gParamScale, gSRSParam[gParamIndex].f);
        return 0;
    default:
        LOG("TruSurround: ParamIndex = %d invalid\n", gParamIndex);
        return -1;
    }
}

static int HPEQ_effect_func(AudioEffect* gAudioEffect, int gParamIndex, int gParamValue)
{
    switch (gParamIndex) {
    case HPEQ_PARAM_BASS:
        if (gParamValue < 0 || gParamValue > 100) {
            LOG("TrebleBass: Bass gParamValue = %d invalid\n", gParamValue);
            return -1;
        }
        gHPEQParam[gParamIndex].v = gParamValue;
        gAudioEffect->setParameter(&gHPEQParam[gParamIndex].param);
        gAudioEffect->getParameter(&gHPEQParam[gParamIndex].param);
        LOG("TrebleBass: Bass is %d -> %d level\n", gParamValue, gHPEQParam[gParamIndex].v);
        return 0;
    case HPEQ_PARAM_TREBLE:
        if (gParamValue < 0 || gParamValue > 100) {
            LOG("TrebleBass: Treble gParamValue = %d invalid\n", gParamValue);
            return -1;
        }
        gHPEQParam[gParamIndex].v = gParamValue;
        gAudioEffect->setParameter(&gHPEQParam[gParamIndex].param);
        gAudioEffect->getParameter(&gHPEQParam[gParamIndex].param);
        LOG("TrebleBass: Treble is %d -> %d level\n", gParamValue, gHPEQParam[gParamIndex].v);
        return 0;
    case HPEQ_PARAM_ENABLE:
        if (gParamValue < 0 || gParamValue > 1) {
            LOG("TrebleBass: Status gParamValue = %d invalid\n", gParamValue);
            return -1;
        }
        gHPEQParam[gParamIndex].v = gParamValue;
        gAudioEffect->setParameter(&gHPEQParam[gParamIndex].param);
        gAudioEffect->getParameter(&gHPEQParam[gParamIndex].param);
        LOG("TrebleBass: Status is %d -> %s\n", gParamValue, HPEQStatusstr[gHPEQParam[gParamIndex].v]);
        return 0;
    default:
        LOG("TrebleBass: ParamIndex = %d invalid\n", gParamIndex);
        return -1;
    }
}

static void effectCallback(int32_t event, void* user, void *info)
{
    LOG("%s : %s():line:%d\n", __FILE__, __FUNCTION__, __LINE__);
}

static int create_audio_effect(AudioEffect **gAudioEffect, String16 name16, int index)
{
    status_t status = NO_ERROR;
    AudioEffect *pAudioEffect = NULL;
    audio_session_t gSessionId = AUDIO_SESSION_OUTPUT_MIX;

    if (*gAudioEffect != NULL)
        return 0;

    pAudioEffect = new AudioEffect(name16);
    if (!pAudioEffect) {
        LOG("create audio effect object failed\n");
        return -1;
    }

    status = pAudioEffect->set(NULL,
            &(gEffectStr[index]), // specific uuid
            0, // priority,
            effectCallback,
            NULL, // callback user data
            gSessionId,
            0); // default output device
    if (status != NO_ERROR) {
        LOG("set effect parameters failed\n");
        return -1;
    }

    status = pAudioEffect->initCheck();
    if (status != NO_ERROR) {
        LOG("init audio effect failed\n");
        return -1;
    }

    pAudioEffect->setEnabled(true);
    LOG("effect %d is %s\n", index, pAudioEffect->getEnabled()?"enabled":"disabled");

    *gAudioEffect = pAudioEffect;
    return 0;
}

int main(int argc,char **argv)
{
    int i;
    int ret = -1;
    int gEffectIndex = 0;
    int gParamIndex = 0;
    int gParamValue = 0;
    float gParamScale = 0.0;
    status_t status = NO_ERROR;
    String16 name16[EFFECT_MAX] = {String16("AudioEffectEQTest"), String16("AudioEffectSRSTest"), String16("AudioEffectHPEQTest")};
    AudioEffect* gAudioEffect[EFFECT_MAX] = {0};
    audio_session_t gSessionId = AUDIO_SESSION_OUTPUT_MIX;

    LOG("**********************************Balance***********************************\n");
    LOG("EffectIndex: 0\n");
    LOG("ParamIndex: 0 -> Level\n");
    LOG("ParamValue: 0 ~ 100\n");
    LOG("ParamIndex: 1 -> Enable\n");
    LOG("ParamValue: 0 -> Disable   1 -> Enable\n");
    LOG("****************************************************************************\n\n");

    LOG("********************************TruSurround*********************************\n");
    LOG("EffectIndex: 1\n");
    LOG("ParamIndex: 0 -> Mode\n");
    LOG("ParamValue: 0 -> Standard  1 -> Music   2 -> Movie   3 -> ClearVoice   4 -> EnhancedBass   5->Custom\n");
    LOG("ParamIndex: 1 -> DiglogClarity Mode\n");
    LOG("ParamValue: 0 -> OFF 1 -> LOW 2 -> HIGH\n");
    LOG("ParamIndex: 2 -> Surround Mode\n");
    LOG("ParamValue: 0 -> ON 1 -> OFF\n");
    LOG("ParamIndex: 4 -> Enable\n");
    LOG("ParamValue: 0 -> Disable   1 -> Enable\n");
    LOG("ParamIndex: 5 -> TrueBass\n");
    LOG("ParamValue: 0 -> Disable   1 -> Enable\n");
    LOG("ParamIndex: 6 -> TrueBassSpeakSize\n");
    LOG("ParamValue: 0 ~ 7\n");
    LOG("ParamIndex: 7 -> TrueBassGain\n");
    LOG("ParamScale: 0.0 ~ 1.0\n");
    LOG("ParamIndex: 8 -> DiglogClarity\n");
    LOG("ParamValue: 0 -> Disable   1 -> Enable\n");
    LOG("ParamIndex: 9 -> DiglogClarityGain\n");
    LOG("ParamScale: 0.0 ~ 1.0\n");
    LOG("ParamIndex: 10 -> Definition\n");
    LOG("ParamValue: 0 -> Disable   1 -> Enable\n");
    LOG("ParamIndex: 11 -> DefinitionGain\n");
    LOG("ParamScale: 0.0 ~ 1.0\n");
    LOG("ParamIndex: 12 -> Surround\n");
    LOG("ParamValue: 0 -> Disable   1 -> Enable\n");
    LOG("ParamIndex: 13 -> SurroundGain\n");
    LOG("ParamScale: 0.0 ~ 1.0\n");
    LOG("ParamIndex: 14 -> InputGain\n");
    LOG("ParamScale: 0.0 ~ 1.0\n");
    LOG("ParamIndex: 15 -> OuputGain\n");
    LOG("ParamScale: 0.0 ~ 1.0\n");
    LOG("ParamIndex: 16 -> OuputGainCompensation\n");
    LOG("ParamScale: -20.0dB ~ 20.0dB\n");
    LOG("****************************************************************************\n\n");

    LOG("*********************************TrebleBass*********************************\n");
    LOG("EffectIndex: 2\n");
    LOG("ParamIndex: 0 -> Bass\n");
    LOG("ParamValue: 0 ~ 100\n");
    LOG("ParamIndex: 1 -> Treble\n");
    LOG("ParamValue: 0 ~ 100\n");
    LOG("ParamIndex: 2 -> Enable\n");
    LOG("ParamValue: 0 -> Disable   1 -> Enable\n");
    LOG("****************************************************************************\n\n");

    if (argc != 4) {
        LOG("Usage: %s <EffectIndex> <ParamIndex> <ParamValue/ParamScale>\n", argv[0]);
        return -1;
    } else {
        LOG("start...\n");
        sscanf(argv[1], "%d", &gEffectIndex);
        sscanf(argv[2], "%d", &gParamIndex);
        if (gEffectIndex == 1 && (gParamIndex == 7 || gParamIndex == 9 || gParamIndex == 11 || (gParamIndex >= 13 && gParamIndex <= 16)))
            sscanf(argv[3], "%f", &gParamScale);
        else
            sscanf(argv[3], "%d", &gParamValue);
    }

    if (gEffectIndex >= (int)(sizeof(gEffectStr)/sizeof(gEffectStr[0]))) {
        LOG("Effect is not exist\n");
        return -1;
    }

    if (gEffectIndex == 1 && (gParamIndex == 7 || gParamIndex == 9 || gParamIndex == 11 || (gParamIndex >= 13 && gParamIndex <= 16)))
        LOG("EffectIndex:%d, ParamIndex:%d, ParamScale:%f\n", gEffectIndex, gParamIndex, gParamScale);
    else
        LOG("EffectIndex:%d, ParamIndex:%d, Paramvalue:%d\n", gEffectIndex, gParamIndex, gParamValue);
    while (1) {
        switch (gEffectIndex) {
        case EFFECT_BALANCE:
            ret = create_audio_effect(&gAudioEffect[EFFECT_BALANCE], name16[EFFECT_BALANCE], EFFECT_BALANCE);
            if (ret < 0) {
                LOG("create Balance effect failed\n");
                goto Error;
            }
            //------------set Balance parameters---------------------------------------
            if (Balance_effect_func(gAudioEffect[gEffectIndex], gParamIndex, gParamValue) < 0)
                LOG("Balance Test failed\n");
            break;
        case EFFECT_SRS:
            ret = create_audio_effect(&gAudioEffect[EFFECT_SRS], name16[EFFECT_SRS], EFFECT_SRS);
            if (ret < 0) {
                LOG("create TruSurround effect failed\n");
                goto Error;
            }
            //------------set TruSurround parameters-----------------------------------
            if (SRS_effect_func(gAudioEffect[gEffectIndex], gParamIndex, gParamValue, gParamScale) < 0)
                LOG("TruSurround Test failed\n");
            break;
        case EFFECT_HPEQ:
            ret = create_audio_effect(&gAudioEffect[EFFECT_HPEQ], name16[EFFECT_HPEQ], EFFECT_HPEQ);
            if (ret < 0) {
                LOG("create TrebleBass effect failed\n");
                goto Error;
            }
            //------------set TrebleBass parameters------------------------------------
            if (HPEQ_effect_func(gAudioEffect[gEffectIndex], gParamIndex, gParamValue) < 0)
                LOG("TrebleBass Test failed\n");
            break;
        default:
            LOG("EffectIndex = %d invalid\n", gEffectIndex);
            break;
        }

        LOG("Please enter param: <27> to Exit\n");
        LOG("Please enter param: <EffectIndex> <ParamIndex> <ParamValue/ParamScale>\n");
        scanf("%d", &gEffectIndex);
        if (gEffectIndex == 27) {
            LOG("Exit...\n");
            break;
        }
        scanf("%d", &gParamIndex);
        if (gEffectIndex == 1 && (gParamIndex == 7 || gParamIndex == 9 || gParamIndex == 11 || (gParamIndex >= 13 && gParamIndex <= 16)))
            scanf("%f", &gParamScale);
        else
            scanf("%d", &gParamValue);

        if (gEffectIndex >= (int)(sizeof(gEffectStr)/sizeof(gEffectStr[0]))) {
            LOG("Effect is not exist\n");
            goto Error;
        }
        if (gEffectIndex == 1 && (gParamIndex == 7 || gParamIndex == 9 || gParamIndex == 11 || (gParamIndex >= 13 && gParamIndex <= 16)))
            LOG("EffectIndex:%d, ParamIndex:%d, ParamScale:%f\n", gEffectIndex, gParamIndex, gParamScale);
        else
            LOG("EffectIndex:%d, ParamIndex:%d, ParamValue:%d\n", gEffectIndex, gParamIndex, gParamValue);
    }

    ret = 0;
Error:
    for (i = 0; i < EFFECT_MAX; i++) {
        if (gAudioEffect[i] != NULL) {
            delete gAudioEffect[i];
            gAudioEffect[i] = NULL;
        }
    }
    return ret;
}
