// Exercise the real prompt/toggle implementation with simulated guest UI calls and Vita hardware.
#include "../src/nfsu/prompts.h"
#include <lib/controls.h>
#include <platform/config.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
constexpr unsigned MemorySize = 0x1000000;
constexpr unsigned Record = 0xa00000, Controller = 0xa00100, Package = 0xa01000;
constexpr unsigned Text = 0xa02000, TextBuffer = 0xa03000, Bar = 0xa04000, Quit = 0xa05000, Back = 0xa05100;
unsigned get(unsigned a) { unsigned n; memcpy(&n, win32::g_guestBase + a, 4); return n; }
void put(unsigned a, unsigned n) { memcpy(win32::g_guestBase + a, &n, 4); }
void screen(const char* name) { strcpy(reinterpret_cast<char*>(win32::g_guestBase + 0x72cdd0), name); }
void resource(win32::WinApplication*, x86::CPU& cpu)
{
    assert(cpu.eax == 0x72cdd0 && get(cpu.esp + 4) == 0x746104);
    cpu.eax = Record;
    cpu.esp += 8;
}
void package(win32::WinApplication*, x86::CPU& cpu)
{
    assert(cpu.eax == 0x6c3bf0);
    cpu.eax = Package;
    cpu.esp += 4;
}
void object(win32::WinApplication*, x86::CPU& cpu)
{
    assert(cpu.edx == Package && cpu.ecx == 0x548f2015);
    cpu.eax = Text;
    cpu.esp += 4;
}
void setText(win32::WinApplication*, x86::CPU& cpu)
{
    assert(get(cpu.esp + 4) == Text);
    const unsigned source = get(cpu.esp + 8);
    for (unsigned i = 0;; ++i)
    {
        const unsigned char c = win32::g_guestBase[source + i];
        win32::g_guestBase[TextBuffer + i * 2] = c;
        win32::g_guestBase[TextBuffer + i * 2 + 1] = 0;
        if (!c) break;
    }
    cpu.esp += 12;
}
bool labelIs(const char* text)
{
    for (unsigned i = 0; i <= strlen(text); ++i)
        if (win32::g_guestBase[TextBuffer + i * 2] != text[i]) return false;
    return true;
}
}

namespace pf {
void logInfo(const char*, ...) {}
void logError(const char*, ...) {}
}
extern "C" int sceCtrlPeekBufferPositive(int, SceCtrlData*, int) { return 0; }
extern "C" int sceTouchSetSamplingState(SceUInt32, SceTouchSamplingState) { return 0; }
extern "C" int sceTouchPeek(SceUInt32, SceTouchData*, SceUInt32) { return 0; }

namespace win32 {
x86::reg8* const g_guestBase = new x86::reg8[MemorySize]();
x86::reg32 MemMap::s_addressOffset = 0x900000;
MemMap::MemMap(x86::reg32, Placement, pf::perf::Alloc) : m_block(0), m_blockCount(1), m_kind(0) {}
MemMap::~MemMap() {}
bool MemMap::isGuestRange(x86::reg32 a, x86::reg32 n) { return a < MemorySize && n <= MemorySize - a; }
WinApplication::WinApplication(const char*, x86::reg32, const std::vector<Section>&)
    : m_preemptWaiters(0), m_methodSlots(3), m_methodShift(31) {}
WinApplication::~WinApplication() {}
void WinApplication::yieldContext(const x86::CPU&) { assert(false); }
MethodPtr WinApplication::findMethod(x86::reg32 a) const
{
    switch (a) {
    case 0x4f3f90: return resource;
    case 0x4f65d0: return package;
    case 0x4ffb70: return object;
    case 0x41f060: return setText;
    default: assert(false); return nullptr;
    }
}
}
struct App : win32::WinApplication { App() : WinApplication("test", 0, {}) {} };

int main(int argc, char** argv)
{
    assert(argc == 2);
    pf::Config::load(argv[1]);
    put(0x706fd8, 0x8bdc3c7d);
    put(Record + 0x14, Controller);
    put(Controller, 0x6cbf14);
    put(Text + 0x18, 2);
    put(Text + 0x60, TextBuffer);
    put(Bar + 0x40, 2);
    put(Bar + 0x44, Quit);
    put(Bar + 0x48, Back);
    put(Quit + 0x10, 0x46fce6dd);
    put(Back + 0x10, 0x1c7fcf8b);
    win32::g_guestBase[Bar + 0x144] = win32::g_guestBase[Bar + 0x145] = 1;
    nfsu::prompts::configure();
    App app;
    x86::CPU cpu{};
    cpu.esp = 0xb00000;
    cpu.eax = Bar;
    cpu.ebx = 0x12345678;
    const auto before = cpu;
    screen("PC_joypad_config.fng");
    nfsu::prompts::prepareMenuBar(&app, cpu, Bar);
    assert(cpu.esp == before.esp && cpu.eax == before.eax && cpu.ebx == before.ebx);
    assert(get(0x706fd8) == 0x8bdc3c7d && labelIs("REAR TOUCH: OFF"));
    nfsu::prompts::finishMenuBar(Bar);
    assert(win32::g_guestBase[Bar + 0x144] == 0); // obsolete Quit activation is suppressed
    assert(win32::g_guestBase[Bar + 0x145] == 1); // Back remains usable
    assert(nfsu::prompts::menuSquare(true));
    assert(win32::controls::rearTouchEnabled());
    nfsu::prompts::prepareMenuBar(&app, cpu, Bar);
    assert(labelIs("REAR TOUCH: ON"));
    assert(nfsu::prompts::menuSquare(false)); // holding Square does not toggle again
    assert(win32::controls::rearTouchEnabled());
    win32::g_guestBase[Controller + 0x80] = 1;
    nfsu::prompts::prepareMenuBar(&app, cpu, Bar);
    assert(!nfsu::prompts::menuSquare(true)); // binding capture keeps its button input
    win32::g_guestBase[Controller + 0x80] = 0;
    nfsu::prompts::prepareMenuBar(&app, cpu, Bar);
    assert(nfsu::prompts::menuSquare(true));
    assert(!win32::controls::rearTouchEnabled());
    pf::Config::load(argv[1]);
    assert(!pf::Config::getBool("rear_touch", true));
    pf::Config::load("/no-such-directory/nfsu/settings.cfg");
    assert(nfsu::prompts::menuSquare(true));
    assert(!win32::controls::rearTouchEnabled()); // a failed save rolls back the live change
    nfsu::prompts::prepareMenuBar(&app, cpu, Bar);
    assert(labelIs("REAR TOUCH: ERROR"));
    screen("MU_CustomizeMain.fng");
    nfsu::prompts::prepareMenuBar(&app, cpu, Bar);
    assert(get(0x706fd8) == 0 && !nfsu::prompts::menuSquare(true));
    // The page actually reached by Options -> Controller is the shared options page, category 3.
    pf::Config::load(argv[1]);
    screen("MU_Options_PC.fng");
    put(Controller, 0x6c3e3c);
    for (unsigned category = 0; category <= 10; ++category)
    {
        put(0x735e20, category);
        nfsu::prompts::prepareMenuBar(&app, cpu, Bar);
        assert(bool(get(0x706fd8)) == (category == 3));
        assert(nfsu::prompts::menuSquare(false) == (category == 3));
    }
    put(0x735e20, 3);
    nfsu::prompts::prepareMenuBar(&app, cpu, Bar);
    assert(labelIs("REAR TOUCH: OFF"));
    assert(nfsu::prompts::menuSquare(true));
    nfsu::prompts::prepareMenuBar(&app, cpu, Bar);
    assert(labelIs("REAR TOUCH: ON"));
    std::puts("controls menu: scoped native footer, live/persisted toggle, binding capture, safe Quit suppression");
}
