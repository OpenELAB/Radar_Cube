#ifndef RADAR_AUDIO_CATALOG_H
#define RADAR_AUDIO_CATALOG_H

#include <Arduino.h>

// 雷达业务音效目录 ID。
// 第一阶段只覆盖当前已有声音，第二阶段再扩展左右/双侧和完整规范文件名。
enum class RadarAudioId : uint8_t {
    None,
    SysBoot,
    SysShutdown,
    PairOk,
    PairFail,
    LinkLost,
    PowerLow,
    PowerCritical,
    Fault,
    DistBeepFar,
    DistBeepMidFar,
    DistBeepMid,
    DistBeepNear,
    DistBeepDanger
};

// 具体 WAV 文件名集中在 catalog，SpeakerController 不知道业务文件名。
const char* radarAudioPath(RadarAudioId id);
const char* radarAudioName(RadarAudioId id);

#endif
