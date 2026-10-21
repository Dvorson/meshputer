#include "port.h"

#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

#include "log.h"
#include "power.h"
#include "settings.h"

namespace {

constexpr int kBufferLines = 160;          // partial render buffer, in rows of the long side
constexpr uint32_t kImuPeriodMs = 150;
constexpr uint32_t kRotationSettleMs = 500; // the device must rest in the new orientation this long
constexpr float kTiltG = 0.40f;            // gravity along the dominant edge axis, in g
constexpr float kFlatG = 0.90f;            // lying flat: keep whatever is shown

// Panel rotation (M5GFX) for each direction gravity can point along the
// panel axes: +x, -x, +y, -y. Rotations 0/2 are portrait, 1/3 landscape.
// Checked on a Tab5: upright with USB-C at the bottom reads +y.
constexpr uint8_t kRotationForGravity[4] = {3, 1, 0, 2};

struct Sample {
    int16_t x, y;
    bool pressed;
};

lv_display_t* sDisplay = nullptr;
int sRotation = 0;
std::deque<Sample> sScripted;
bool sSwallow = false;  // the touch that woke the screen is not a tap
lv_point_t sLastPoint = {0, 0};

int sForced = -1;
int sCandidate = -1;
uint32_t sCandidateSince = 0;
uint32_t sLastImuMs = 0;

void flush(lv_display_t* disp, const lv_area_t* area, uint8_t* pixels) {
    M5.Display.pushImage(area->x1, area->y1, lv_area_get_width(area), lv_area_get_height(area),
                         reinterpret_cast<const lgfx::rgb565_t*>(pixels));
    lv_display_flush_ready(disp);
}

void readTouch(lv_indev_t*, lv_indev_data_t* data) {
    if (!sScripted.empty()) {
        Sample s = sScripted.front();
        sScripted.pop_front();
        if (s.pressed) Power::onKey();
        sLastPoint = {s.x, s.y};
        data->point = sLastPoint;
        data->state = s.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        return;
    }
    auto t = M5.Touch.getDetail();
    bool pressed = t.isPressed();
    if (pressed) {
        if (Power::onKey()) sSwallow = true;  // screen was off: this touch only wakes it
        sLastPoint = {static_cast<int32_t>(t.x), static_cast<int32_t>(t.y)};
    } else {
        sSwallow = false;
    }
    data->point = sLastPoint;
    data->state = pressed && !sSwallow ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void applyRotation(int rotation) {
    sRotation = rotation;
    M5.Display.setRotation(rotation);
    lv_display_set_resolution(sDisplay, M5.Display.width(), M5.Display.height());
    LOG("UI", "rotation %d (%dx%d)", rotation, static_cast<int>(M5.Display.width()),
        static_cast<int>(M5.Display.height()));
}

// Rotation that puts the UI upright for the current gravity vector, or -1
// while the device lies flat or sits between two orientations.
int rotationFromGravity() {
    float ax = 0, ay = 0, az = 0;
    M5.Imu.update();
    if (!M5.Imu.getAccel(&ax, &ay, &az)) return -1;
    if (std::fabs(az) > kFlatG) return -1;
    float fx = std::fabs(ax), fy = std::fabs(ay);
    int axis = -1;  // index into kRotationForGravity
    if (fx > kTiltG && fx > fy * 1.5f) axis = ax > 0 ? 0 : 1;
    else if (fy > kTiltG && fy > fx * 1.5f) axis = ay > 0 ? 2 : 3;
    static int sLoggedAxis = -2;
    if (axis != sLoggedAxis) {
        sLoggedAxis = axis;
        LOG("UI", "gravity %.2f %.2f %.2f (axis %d)", ax, ay, az, axis);
    }
    return axis < 0 ? -1 : kRotationForGravity[axis];
}

} // namespace

namespace Port {

void begin() {
    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return millis(); });

    sRotation = Settings::get().orientation == 2 ? 1 : 0;
    M5.Display.setRotation(sRotation);
    sDisplay = lv_display_create(M5.Display.width(), M5.Display.height());
    lv_display_set_color_format(sDisplay, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(sDisplay, flush);
    int longSide = std::max(M5.Display.width(), M5.Display.height());
    size_t bytes = static_cast<size_t>(longSide) * kBufferLines * 2;
    void* buf = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    lv_display_set_buffers(sDisplay, buf, nullptr, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t* touch = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, readTouch);
}

void tick() {
    lv_timer_handler();
}

bool updateRotation() {
    uint8_t mode = Settings::get().orientation;
    int want;
    if (sForced >= 0) want = sForced;
    else if (mode == 1) want = 0;
    else if (mode == 2) want = 1;
    else {
        uint32_t now = millis();
        if (now - sLastImuMs < kImuPeriodMs) return false;
        sLastImuMs = now;
        int seen = rotationFromGravity();
        if (seen < 0 || seen == sRotation) { sCandidate = -1; return false; }
        if (seen != sCandidate) {
            sCandidate = seen;
            sCandidateSince = now;
            return false;
        }
        if (now - sCandidateSince < kRotationSettleMs) return false;
        want = seen;
        sCandidate = -1;
    }
    if (want == sRotation) return false;
    applyRotation(want);
    return true;
}

bool landscape() { return (sRotation & 1) != 0; }

void forceRotation(int rotation) { sForced = rotation < 0 ? -1 : rotation & 3; }

void injectTap(int x, int y) {
    Sample down{static_cast<int16_t>(x), static_cast<int16_t>(y), true};
    sScripted.push_back(down);
    sScripted.push_back(down);
    sScripted.push_back({down.x, down.y, false});
}

void injectSwipe(int x1, int y1, int x2, int y2) {
    constexpr int kSteps = 12;
    for (int i = 0; i <= kSteps; ++i) {
        sScripted.push_back({static_cast<int16_t>(x1 + (x2 - x1) * i / kSteps),
                             static_cast<int16_t>(y1 + (y2 - y1) * i / kSteps), true});
    }
    sScripted.push_back({static_cast<int16_t>(x2), static_cast<int16_t>(y2), false});
}

void dumpFrame(int step) {
    if (step < 1) step = 1;
    int w = M5.Display.width(), h = M5.Display.height();
    std::vector<uint16_t> row(w);
    std::string line;
    line.reserve(static_cast<size_t>(w / step + 1) * 4);
    static const char* kHex = "0123456789abcdef";
    Serial.printf("SHOT %d %d rgb565be\n", (w + step - 1) / step, (h + step - 1) / step);
    for (int y = 0; y < h; y += step) {
        M5.Display.readRect(0, y, w, 1, reinterpret_cast<lgfx::rgb565_t*>(row.data()));
        line.clear();
        for (int x = 0; x < w; x += step) {
            uint16_t v = row[x];
            line += kHex[(v >> 12) & 0xF];
            line += kHex[(v >> 8) & 0xF];
            line += kHex[(v >> 4) & 0xF];
            line += kHex[v & 0xF];
        }
        Serial.println(line.c_str());
    }
    Serial.println("SHOT END");
}

} // namespace Port
