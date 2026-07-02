#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

// WS2812 数据引脚和灯珠数量。
constexpr uint8_t LED_PIN = 18;
constexpr uint16_t LED_COUNT = 8;

// 渐变步数，以及每帧之间的延时。
constexpr uint16_t FADE_STEPS = 255;
constexpr uint16_t STEP_DELAY_MS = 20;

// 使用 Arduino 的 Adafruit NeoPixel 库，不再手动用 GPIO 卡时序。
// NEO_GRB 对应常见 WS2812 颜色顺序，NEO_KHZ800 对应 WS2812 通信速率。
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// 设置所有灯珠为同一种颜色，并刷新 strip 显示。
void fillAll(uint8_t red, uint8_t green, uint8_t blue)
{
  const uint32_t color = strip.Color(red, green, blue);

  for (uint16_t i = 0; i < LED_COUNT; ++i) {
    strip.setPixelColor(i, color);
  }

  strip.show();
}

void setup()
{
  strip.begin();
  strip.clear();
  strip.show();

  // 上电后先显示纯绿色。
  fillAll(0, 255, 0);
}

void loop()
{
  // 从绿色渐变到红色。
  for (uint16_t step = 0; step <= FADE_STEPS; ++step) {
    const uint8_t red = step;
    const uint8_t green = FADE_STEPS - step;

    fillAll(red, green, 0);
    delay(STEP_DELAY_MS);
  }

  // 停留 1 秒，然后重新开始渐变。
  delay(1000);
}
