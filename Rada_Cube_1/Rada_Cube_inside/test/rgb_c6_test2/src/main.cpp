#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

// Number of data pins and lamp beads of WS2812.
constexpr uint8_t LED_PIN = 18;
constexpr uint16_t LED_COUNT = 8;

// Number of gradient steps, and delay between each frame.
constexpr uint16_t FADE_STEPS = 255;
constexpr uint16_t STEP_DELAY_MS = 20;

// Use the Adafruit NeoPixel library for Arduino, no more manual GPIO card timing.
// NEO_GRB corresponds to the common WS2812 color sequence, and NEO_KHZ800 corresponds to the WS2812 communication rate.
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// Set all lamp beads to the same color and refresh the strip display.
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

  // After powering on, it will first display pure green.
  fillAll(0, 255, 0);
}

void loop()
{
  // Gradient from green to red.
  for (uint16_t step = 0; step <= FADE_STEPS; ++step) {
    const uint8_t red = step;
    const uint8_t green = FADE_STEPS - step;

    fillAll(red, green, 0);
    delay(STEP_DELAY_MS);
  }

  // Pause for 1 second, then restart the fade.
  delay(1000);
}
