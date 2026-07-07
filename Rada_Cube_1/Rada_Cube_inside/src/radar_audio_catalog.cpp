#include "radar_audio_catalog.h"

const char* radarAudioPath(RadarAudioId id)
{
    switch (id) {
    case RadarAudioId::SysBoot:
    case RadarAudioId::SysShutdown:
        return "/boot.wav";
    case RadarAudioId::PairOk:
        return "/pair_ok.wav";
    case RadarAudioId::PairFail:
        return "/pair_fail.wav";
    case RadarAudioId::LinkLost:
        return "/connection_lost.wav";
    case RadarAudioId::PowerLow:
    case RadarAudioId::PowerCritical:
        return "/low_battery.wav";
    case RadarAudioId::Fault:
        return "/fault.wav";
    case RadarAudioId::DistBeepFar:
        return "/beep_slow.wav";
    case RadarAudioId::DistBeepMidFar:
        return "/beep_medium_slow.wav";
    case RadarAudioId::DistBeepMid:
        return "/beep_medium.wav";
    case RadarAudioId::DistBeepNear:
        return "/beep_fast.wav";
    case RadarAudioId::DistBeepDanger:
        return "/beep_continuous.wav";
    case RadarAudioId::None:
    default:
        return nullptr;
    }
}

const char* radarAudioName(RadarAudioId id)
{
    switch (id) {
    case RadarAudioId::SysBoot:
        return "SysBoot";
    case RadarAudioId::SysShutdown:
        return "SysShutdown";
    case RadarAudioId::PairOk:
        return "PairOk";
    case RadarAudioId::PairFail:
        return "PairFail";
    case RadarAudioId::LinkLost:
        return "LinkLost";
    case RadarAudioId::PowerLow:
        return "PowerLow";
    case RadarAudioId::PowerCritical:
        return "PowerCritical";
    case RadarAudioId::Fault:
        return "Fault";
    case RadarAudioId::DistBeepFar:
        return "DistBeepFar";
    case RadarAudioId::DistBeepMidFar:
        return "DistBeepMidFar";
    case RadarAudioId::DistBeepMid:
        return "DistBeepMid";
    case RadarAudioId::DistBeepNear:
        return "DistBeepNear";
    case RadarAudioId::DistBeepDanger:
        return "DistBeepDanger";
    case RadarAudioId::None:
    default:
        return "None";
    }
}
