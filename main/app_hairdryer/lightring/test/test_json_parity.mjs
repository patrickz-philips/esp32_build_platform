import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL("../", import.meta.url));
const readJson = (name) => JSON.parse(readFileSync(join(root, "..", name), "utf8"));
const setting = readJson("Nairobi Device.ledsetting.json");
const effects = [
  ["thermo-lowspeed-blend-rotation.effect.json", "thermo-highspeed-blend-rotation.effect.json"],
  ["hot-lowspeed.led.json", "hot-highspeed.led.json"],
  ["warm-lowspeed.led.json", "warm-highspeed.led.json"],
].map((files) => files.map(readJson));
const coldEffects = ["cold-lowspeed.led.json", "cold-highspeed.led.json"].map(readJson);
const calibration = readFileSync(join(root, "src/lightring_calibration.inc"), "utf8");
const luts = Object.fromEntries(
  ["srgb", "brightness", "red", "green", "blue"].map((name) => {
    const values = calibration.match(new RegExp(`s_${name}_lut\\[256\\] = \\{([^}]+)\\}`));
    assert.ok(values, `Missing ${name} calibration LUT`);
    const lut = [...values[1].matchAll(/(\d+)U/g)].map((match) => Number(match[1]));
    assert.equal(lut.length, 256);
    return [name, lut];
  }),
);

function sample(keys, phase, channel) {
  const value = (key) => channel === undefined ? key.value : key.value[channel];
  const next = keys.findIndex((key) => key.timeMs > phase);
  if (next < 0) return value(keys.at(-1));
  const from = keys[next - 1];
  const to = keys[next];
  if (!from.transition || from.transition.type === "hold") return value(from);
  const elapsed = phase - from.timeMs;
  const duration = to.timeMs - from.timeMs;
  return Math.floor((value(from) * (duration - elapsed) + value(to) * elapsed + duration / 2) / duration);
}

function expectedFrame(effect, phaseMs) {
  const bytes = Buffer.alloc(72);
  if (!effect) return bytes;
  const phase = phaseMs % effect.durationMs;
  for (const track of effect.timeline) {
    const [r, g, b] = [0, 1, 2].map((channel) => luts.srgb[sample(track.color, phase, channel)]);
    const level = luts.brightness[sample(track.brightness, phase)];
    const corrected = [
      2342 * r + 1302 * g + 157 * b,
      141 * r + 2339 * g - 52 * b,
      18 * r + 24 * g + 1351 * b,
    ].map((value, channel) => {
      const clamped = Math.max(0, Math.min(255, Math.trunc(value / 4096)));
      return luts[["red", "green", "blue"][channel]][Math.floor(clamped * level / 255)];
    });
    const physical = setting.layout.leds.find((led) => led.index === track.index).physicalSlot;
    bytes.set([corrected[1], corrected[0], corrected[2]], physical * 3);
  }
  return bytes;
}

const temp = mkdtempSync(join(tmpdir(), "lightring-test-"));
try {
  const binary = join(temp, "effects");
  execFileSync("cc", [
    "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", join(root, "include"),
    join(root, "test/test_effects_host.c"), join(root, "src/lightring_effects.c"),
    "-lm", "-o", binary,
  ], { stdio: "inherit" });
  execFileSync(binary, [], { stdio: "inherit" });
  const actual = execFileSync(binary, ["--dump"], { maxBuffer: 4 * 1024 * 1024 });
  let offset = 0;
  for (let mode = 0; mode < 3; mode++) {
    for (let speed = 0; speed < 3; speed++) {
      for (let cold = 0; cold <= 1; cold++) {
        const effect = speed === 0 ? undefined : (cold ? coldEffects : effects[mode])[speed - 1];
        for (let phase = 0; phase <= 20010; phase += 10) {
          assert.deepEqual(
            actual.subarray(offset, offset + 72), expectedFrame(effect, phase),
            `mode=${mode} speed=${speed} cold=${cold} phase=${phase}`,
          );
          offset += 72;
        }
      }
    }
  }
  assert.equal(actual.length, offset);
  console.log(`JSON parity: ${offset / 72} calibrated frames match`);

  writeFileSync(join(temp, "FreeRTOS.h"), `#pragma once
#include <stdint.h>
typedef uint32_t TickType_t;
typedef unsigned UBaseType_t;
typedef void *TaskHandle_t;
extern TickType_t test_now;
#define configTICK_RATE_HZ 1000U
#define pdMS_TO_TICKS(ms) (ms)
#define pdPASS 1
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
static inline TickType_t xTaskGetTickCount(void) { return test_now; }
static inline void vTaskDelay(TickType_t ticks) { (void)ticks; }
static inline void vTaskDelete(TaskHandle_t task) { (void)task; }
static inline int xTaskCreate(void (*fn)(void *), const char *name,
    uint16_t stack, void *arg, UBaseType_t priority, TaskHandle_t *handle)
{
    (void)fn; (void)name; (void)stack; (void)arg; (void)priority;
    *handle = &test_now;
    return pdPASS;
}
`);
  writeFileSync(join(temp, "task.h"), '#include "FreeRTOS.h"\n');
  writeFileSync(join(temp, "transitions.c"), `#include <assert.h>
#include <string.h>
#include "lightring.c"
TickType_t test_now;
int ws2812_init(const ws2812_config_t *config) { (void)config; return 0; }
int ws2812_send(const uint8_t *pixels, uint16_t count)
{ (void)pixels; (void)count; return 0; }
void ws2812_deinit(void) {}

static void check_frame(uint32_t now, lightring_speed_t speed)
{
    test_now = now;
    render_current_frame(test_now);
    assert(lightring_get_status() == LIGHTRING_STATUS_IDLE);
    assert(lightring_get_speed() == speed);
    assert(current_phase_ms(test_now) == now % LIGHTRING_PHASE_PERIOD_MS);
    uint8_t actual[sizeof(lightring_framebuffer)];
    memcpy(actual, lightring_framebuffer, sizeof(actual));
    lightring_frame_t expected = {
        .from_phase_ms = now, .to_phase_ms = now,
        .from_mode = LIGHTRING_MODE_THERMO, .to_mode = LIGHTRING_MODE_THERMO,
        .from_speed = speed, .to_speed = speed,
    };
    lightring_render_frame(&expected);
    assert(memcmp(actual, lightring_framebuffer, sizeof(actual)) == 0);
}

int main(void)
{
    lightring_config_t config = {
        .ws2812 = { .led_count = LIGHTRING_LED_COUNT },
        .initial_mode = LIGHTRING_MODE_THERMO,
        .initial_speed = LIGHTRING_SPEED_LOW,
    };
    assert(lightring_init(&config) == 0);
    check_frame(2500U, LIGHTRING_SPEED_LOW);
    test_now = 2710U;
    assert(lightring_change_speed(LIGHTRING_SPEED_HIGH) == 0);
    assert(lightring_get_status() == LIGHTRING_STATUS_SPEED_EXPANDING);
    check_frame(3010U, LIGHTRING_SPEED_HIGH);
    test_now = 3500U;
    assert(lightring_change_speed(LIGHTRING_SPEED_LOW) == 0);
    check_frame(3800U, LIGHTRING_SPEED_LOW);
    test_now = 4100U;
    assert(lightring_change_speed(LIGHTRING_SPEED_OFF) == 0);
    check_frame(4400U, LIGHTRING_SPEED_OFF);
    test_now = 4600U;
    assert(lightring_change_speed(LIGHTRING_SPEED_LOW) == 0);
    check_frame(4900U, LIGHTRING_SPEED_LOW);
    test_now = 5100U;
    assert(lightring_change_speed(LIGHTRING_SPEED_OFF) == 0);
    check_frame(5400U, LIGHTRING_SPEED_OFF);
    test_now = 5700U;
    assert(lightring_change_speed(LIGHTRING_SPEED_HIGH) == 0);
    check_frame(6000U, LIGHTRING_SPEED_HIGH);
    check_frame(19990U, LIGHTRING_SPEED_HIGH);
    check_frame(20000U, LIGHTRING_SPEED_HIGH);
    check_frame(20010U, LIGHTRING_SPEED_HIGH);
    lightring_deinit();

    test_now = 0U;
    assert(lightring_init(&config) == 0);
    test_now = 2500U;
    assert(lightring_cold_on() == 0);
    test_now = 2800U;
    render_current_frame(test_now);
    assert(lightring_is_cold());
    test_now = 5100U;
    assert(lightring_change_speed(LIGHTRING_SPEED_HIGH) == 0);
    render_current_frame(test_now);
    assert(lightring_get_status() == LIGHTRING_STATUS_SPEED_WAITING_MAX);
    test_now = 6000U;
    render_current_frame(test_now);
    assert(lightring_get_status() == LIGHTRING_STATUS_SPEED_EXPANDING);
    test_now = 6300U;
    render_current_frame(test_now);
    assert(lightring_get_status() == LIGHTRING_STATUS_IDLE);
    assert(lightring_get_speed() == LIGHTRING_SPEED_HIGH);
    lightring_deinit();
    return 0;
}
`);
  const transitions = join(temp, "transitions");
  execFileSync("cc", [
    "-std=c11", "-Wall", "-Wextra", "-Werror",
    "-I", temp, "-I", join(root, "include"), "-I", join(root, "src"),
    join(temp, "transitions.c"), join(root, "src/lightring_effects.c"),
    "-lm", "-o", transitions,
  ], { stdio: "inherit" });
  execFileSync(transitions, [], { stdio: "inherit" });
  console.log("Phase and speed transitions: ok");
} finally {
  rmSync(temp, { recursive: true, force: true });
}
