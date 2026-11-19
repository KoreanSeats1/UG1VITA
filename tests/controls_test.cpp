// Host-side tests with the real VitaSDK declarations and simulated hardware calls.
#include <lib/controls.h>
#include <platform/config.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

static SceCtrlData s_pad;
static int s_samplingCalls = 0, s_touchReads = 0;

extern "C" int sceCtrlPeekBufferPositive(int, SceCtrlData* pad, int)
{
    *pad = s_pad;
    return 1;
}

extern "C" int sceTouchSetSamplingState(SceUInt32 port, SceTouchSamplingState state)
{
    assert(port == SCE_TOUCH_PORT_BACK && state == SCE_TOUCH_SAMPLING_STATE_START);
    ++s_samplingCalls;
    return 0;
}

extern "C" int sceTouchPeek(SceUInt32 port, SceTouchData* touch, SceUInt32)
{
    assert(port == SCE_TOUCH_PORT_BACK);
    ++s_touchReads;
    memset(touch, 0, sizeof(*touch));
    touch->reportNum = 1;
    touch->report[0].x = 100; // an accidental touch on the left half
    return 1;
}

namespace pf {
void logInfo(const char*, ...) {}
void logError(const char*, ...) {}
}

int main(int argc, char** argv)
{
    const bool rear = argc > 2 && strcmp(argv[2], "on") == 0;
    pf::Config::load(argc > 1 ? argv[1] : "");
    s_pad.lx = s_pad.ly = s_pad.rx = s_pad.ry = 128;
    s_pad.buttons = SCE_CTRL_UP;
    using namespace win32::controls;
    Mapper mapper;
    PadSample pad = readPad();
    assert(bool(pad.inputs & bit(RearLeft)) == rear);
    assert(s_samplingCalls == (rear ? 1 : 0));
    assert(s_touchReads == (rear ? 1 : 0));
    State state = mapper.update(pad, 0);
    assert(state.buttons & (1u << 12)); // up: shift up in the race map, navigate up in menus
    assert(!(state.buttons & (1u << 14)));
    assert(state.pov == 0);

    s_pad.buttons = SCE_CTRL_DOWN;
    state = mapper.update(readPad(), 1000);
    assert(state.buttons & (1u << 14)); // down: shift down
    assert(!(state.buttons & (1u << 12)));
    assert(state.pov == 18000);
    s_pad.buttons = 0;
    state = mapper.update(readPad(), 2000);
    assert(!(state.buttons & ((1u << 12) | (1u << 14))));
    assert(state.pov == -1);

    float x, y;
    shapeStick(state.axes[AxisZ], state.axes[AxisRz], 0, 10000, &x, &y);
    assert(x == 0 && y == 0); // neutral hardware must not drift
    s_pad.rx = 255;
    s_pad.ry = 0;
    state = mapper.update(readPad(), 3000);
    assert(state.axes[AxisZ] == 1 && state.axes[AxisRz] == -1);
    assert(axisPresent(AxisZ) && axisPresent(AxisRz) && presentAxisCount() == 4);
    shapeStick(state.axes[AxisZ], state.axes[AxisRz], 0, 10000, &x, &y);
    assert(x > 0 && y < 0 && std::fabs(x * x + y * y - 1.0f) < 0.001f);
    s_pad.rx = 0;
    s_pad.ry = 128;
    state = mapper.update(readPad(), 4000);
    assert(state.axes[AxisZ] == -1); // left must survive the mapper too
    shapeStick(state.axes[AxisZ], state.axes[AxisRz], 0, 10000, &x, &y);
    assert(x < -0.99f);
    std::puts(rear ? "controls: rear-touch opt-in passed" : "controls: rear-touch-off and stick/d-pad checks passed");
}
