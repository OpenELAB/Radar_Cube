#include <Arduino.h>
#include <ESP_I2S.h>
#include <LittleFS.h>
#include <string.h>

// MAX98357A I2S pins.
static constexpr int I2S_LRC = 21;
static constexpr int I2S_BCLK = 22;
static constexpr int I2S_DIN = 23;

// Add more WAV files here after putting them in the data/ folder.
static constexpr const char *AUDIO_FILES[] = {
    "/kong.wav",
    "/output.wav",
};

static constexpr size_t AUDIO_BUFFER_SIZE = 1024;
static constexpr float VOLUME_STEP = 0.1f;
static constexpr float MIN_VOLUME_GAIN = 0.0f;
static constexpr float MAX_VOLUME_GAIN = 3.0f;

I2SClass i2s;
uint8_t audioBuffer[AUDIO_BUFFER_SIZE];
int16_t stereoBuffer[AUDIO_BUFFER_SIZE];
bool i2sStarted = false;
uint32_t currentSampleRate = 0;
float volumeGain = 0.5f;

struct WavInfo {
  uint16_t audioFormat = 0;
  uint16_t channels = 0;
  uint32_t sampleRate = 0;
  uint16_t bitsPerSample = 0;
  uint32_t dataStart = 0;
  uint32_t dataSize = 0;
};

static uint16_t readLE16(File &file) {
  uint8_t bytes[2];
  if (file.read(bytes, sizeof(bytes)) != sizeof(bytes)) {
    return 0;
  }
  return static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8);
}

static uint32_t readLE32(File &file) {
  uint8_t bytes[4];
  if (file.read(bytes, sizeof(bytes)) != sizeof(bytes)) {
    return 0;
  }
  return static_cast<uint32_t>(bytes[0]) |
         (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) |
         (static_cast<uint32_t>(bytes[3]) << 24);
}

static bool readChunkId(File &file, char id[5]) {
  if (file.read(reinterpret_cast<uint8_t *>(id), 4) != 4) {
    return false;
  }
  id[4] = '\0';
  return true;
}

static bool readWavInfo(File &file, WavInfo &info) {
  char id[5];

  if (!readChunkId(file, id) || strcmp(id, "RIFF") != 0) {
    return false;
  }

  readLE32(file);

  if (!readChunkId(file, id) || strcmp(id, "WAVE") != 0) {
    return false;
  }

  bool foundFmt = false;
  bool foundData = false;

  while (file.available()) {
    if (!readChunkId(file, id)) {
      break;
    }

    const uint32_t chunkSize = readLE32(file);
    const uint32_t chunkDataStart = file.position();

    if (strcmp(id, "fmt ") == 0) {
      info.audioFormat = readLE16(file);
      info.channels = readLE16(file);
      info.sampleRate = readLE32(file);
      readLE32(file);
      readLE16(file);
      info.bitsPerSample = readLE16(file);
      foundFmt = true;
    } else if (strcmp(id, "data") == 0) {
      info.dataStart = chunkDataStart;
      info.dataSize = chunkSize;
      foundData = true;
      break;
    }

    file.seek(chunkDataStart + chunkSize + (chunkSize & 1));
  }

  return foundFmt && foundData &&
         info.audioFormat == 1 &&
         info.bitsPerSample == 16 &&
         (info.channels == 1 || info.channels == 2);
}

static bool startI2S(const WavInfo &info) {
  if (i2sStarted && currentSampleRate == info.sampleRate) {
    return true;
  }

  if (i2sStarted) {
    i2s.end();
    i2sStarted = false;
  }

  i2s.setPins(I2S_BCLK, I2S_LRC, I2S_DIN);

  i2sStarted = i2s.begin(I2S_MODE_STD,
                         info.sampleRate,
                         I2S_DATA_BIT_WIDTH_16BIT,
                         I2S_SLOT_MODE_STEREO);

  currentSampleRate = i2sStarted ? info.sampleRate : 0;
  return i2sStarted;
}

static int16_t applyVolume(int16_t sample) {
  const int32_t scaled = static_cast<int32_t>(sample * volumeGain);

  if (scaled > INT16_MAX) {
    return INT16_MAX;
  }
  if (scaled < INT16_MIN) {
    return INT16_MIN;
  }
  return static_cast<int16_t>(scaled);
}

static void applyVolumeInPlace(uint8_t *data, size_t bytesRead) {
  int16_t *samples = reinterpret_cast<int16_t *>(data);
  const size_t sampleCount = bytesRead / sizeof(int16_t);

  for (size_t i = 0; i < sampleCount; i++) {
    samples[i] = applyVolume(samples[i]);
  }
}

static void printVolume() {
  Serial.printf("Volume gain: %.1fx\n", volumeGain);
}

static void handleVolumeControl() {
  while (Serial.available() > 0) {
    const char command = static_cast<char>(Serial.read());

    if (command == '+') {
      volumeGain = min(volumeGain + VOLUME_STEP, MAX_VOLUME_GAIN);
      printVolume();
    } else if (command == '-') {
      volumeGain = max(volumeGain - VOLUME_STEP, MIN_VOLUME_GAIN);
      printVolume();
    } else if (command == '0') {
      volumeGain = 1.0f;
      printVolume();
    }
  }
}

static void writeAudioChunk(uint8_t *data, size_t bytesRead, const WavInfo &info) {
  if (info.channels == 2) {
    applyVolumeInPlace(data, bytesRead);
    i2s.write(data, bytesRead);
    return;
  }

  const size_t sampleCount = bytesRead / sizeof(int16_t);
  const int16_t *monoSamples = reinterpret_cast<const int16_t *>(data);

  for (size_t i = 0; i < sampleCount; i++) {
    const int16_t sample = applyVolume(monoSamples[i]);
    stereoBuffer[i * 2] = sample;
    stereoBuffer[i * 2 + 1] = sample;
  }

  i2s.write(reinterpret_cast<const uint8_t *>(stereoBuffer),
            sampleCount * 2 * sizeof(int16_t));
}

static void playWav(const char *audioFile) {
  File file = LittleFS.open(audioFile, "r");
  if (!file) {
    Serial.print("Could not open audio file: ");
    Serial.println(audioFile);
    return;
  }

  WavInfo wavInfo;
  if (!readWavInfo(file, wavInfo)) {
    Serial.print("Unsupported WAV: ");
    Serial.println(audioFile);
    Serial.println("Use PCM 16-bit mono/stereo WAV files.");
    file.close();
    return;
  }

  if (!startI2S(wavInfo)) {
    Serial.println("I2S init failed");
    file.close();
    return;
  }

  Serial.printf("Playing %s: %lu Hz, %u channel(s), %lu bytes\n",
                audioFile,
                static_cast<unsigned long>(wavInfo.sampleRate),
                wavInfo.channels,
                static_cast<unsigned long>(wavInfo.dataSize));
  printVolume();

  file.seek(wavInfo.dataStart);
  uint32_t bytesLeft = wavInfo.dataSize;

  while (bytesLeft > 0) {
    const size_t toRead = min(static_cast<uint32_t>(sizeof(audioBuffer)), bytesLeft);
    const size_t bytesRead = file.read(audioBuffer, toRead);
    if (bytesRead == 0) {
      break;
    }

    handleVolumeControl();
    writeAudioChunk(audioBuffer, bytesRead, wavInfo);
    bytesLeft -= bytesRead;
  }

  file.close();
  Serial.println("Playback finished");
}

static void playPlaylist() {
  for (const char *audioFile : AUDIO_FILES) {
    playWav(audioFile);
    delay(300);
  }
}

static void checkAudioFiles() {
  bool missingAnyFile = false;

  for (const char *audioFile : AUDIO_FILES) {
    if (!LittleFS.exists(audioFile)) {
      Serial.print("Missing audio file in LittleFS: ");
      Serial.println(audioFile);
      missingAnyFile = true;
    }
  }

  if (missingAnyFile) {
    Serial.println("Run: platformio run -t uploadfs");
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("Send + / - in Serial Monitor to change volume, 0 to reset.");

  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed. Upload filesystem image first.");
    while (true) {
      delay(1000);
    }
  }

  checkAudioFiles();
  playPlaylist();
}

void loop() {
  // delay(1000);
  playPlaylist();
}
