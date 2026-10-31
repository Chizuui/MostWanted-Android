// Need for Speed: Carbon boots on the host's XDK like Most Wanted does, but every hook in src/game is cut for Most Wanted's
// addresses. Until Carbon's are found these are the calls the common code makes, doing nothing
#include <android/log.h>
#include <cstring>
#include <string>

#include "common/aspect.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
namespace dispatch {
uint32_t HostThunk(X86Func* fn);
}

#include "common/fe_mouse.h"
#include "common/game_font.h"
#include "common/hud_layout.h"
#include "common/options.h"

namespace game {

// Carbon's widescreen:
// 0x45D294: byte flag set when widescreen is enabled at boot (mov al, [0x45D294]; and al, 1; ret at 0x22C130)
// 0x3F98E0: float width transform (853.0f), read by front-end and HUD transform (0x10F094, 0x111749)
constexpr uint32_t kVideoFlags = 0x45D294, kWideWidth = 0x3F98E0;

void WidenAspect() {
    if (!mem::IsCommitted(kVideoFlags) || !(kernel::G<uint8_t>(kVideoFlags) & 1)) return;
    float w = 480.0f * Aspect();
    uint32_t bits;
    std::memcpy(&bits, &w, 4);
    kernel::W32(kWideWidth, bits);
}

// Carbon render switches:
// - 0x401D14: World Bloom switch (checked at 0x112AF9, calls 0x110F50 / 0x110E00)
// - 0x3F96B3: Particles - Main View switch (checked at 0x112A63)
// - 0x3F96BA: Car Reflections (EnvMap) switch (checked at 0x1121BB)
// - 0x492F8C: Road Reflection active flag (checked at 0x111FF0)
constexpr uint32_t kWorldBloomSwitch = 0x401D14;
constexpr uint32_t kParticleSwitch = 0x3F96B3;
constexpr uint32_t kEnvMapSwitch = 0x3F96BA;
constexpr uint32_t kRoadReflectionSwitch = 0x492F8C;

void ApplyGraphicsSwitches() {
    const Options& o = Opt();
    if (mem::IsCommitted(kWorldBloomSwitch)) {
        kernel::G<uint8_t>(kWorldBloomSwitch) = o.bloom ? 1 : 0;
    }
    if (mem::IsCommitted(kParticleSwitch)) {
        kernel::G<uint8_t>(kParticleSwitch) = o.particles ? 1 : 0;
    }
    if (mem::IsCommitted(kEnvMapSwitch)) {
        static uint32_t frame = 0;
        uint32_t rate = o.reflections;
        uint32_t every = rate < 3 ? 1u << rate : 512;
        kernel::G<uint8_t>(kEnvMapSwitch) = (frame++ % every == 0) ? 1 : 0;
    }
    if (mem::IsCommitted(kRoadReflectionSwitch)) {
        kernel::G<uint8_t>(kRoadReflectionSwitch) = o.road_reflections ? 1 : 0;
    }
}

// Carbon cheats:
// 0x46D944 holds AICopManager / ICopMgr singleton pointer
constexpr uint32_t kCopManager = 0x46D944;

// Carbon's speedbreaker meter (update 0x23F150, amount [this + 0x7C], 0..1) skips its drain when this debug
// byte, which the game never writes, is set. Not the cheat table's check (0x863A0, id 0xB) it also asks:
// that is the game's own cheat codes
constexpr uint32_t kInfiniteSpeedbreaker = 0x497D5C;

void ApplyCheats() {
    const Options& o = Opt();
    if (mem::IsCommitted(kInfiniteSpeedbreaker))
        kernel::G<uint8_t>(kInfiniteSpeedbreaker) = o.cheat_speedbreaker ? 1 : 0;
    if (mem::IsCommitted(kCopManager)) {
        uint32_t mgr = kernel::R32(kCopManager);
        if (mgr && mem::IsCommitted(mgr + 0x60)) {
            kernel::G<uint8_t>(mgr + 0x60) = o.cheat_no_police ? 1 : 0;
        }
    }
}

void CaptureFont() {}

bool TakeFont(FontImage&) { return false; }

// In Carbon: 0x46D280 is GameFlowState.
// State 6 is racing/driving. Any other state is menu/loading/FMV.
constexpr uint32_t kGameFlowState = 0x46D280;
constexpr uint32_t kGameFlowRacing = 6;
constexpr uint32_t kCFEng = 0x46A7C8;

// Returns true if name is any menu/dialog/options package (anything that is NOT racing HUD or song banner)
bool IsMenuPackageName(const char* name) {
    if (!name || *name == '\0') return false;
    if (std::strstr(name, "HUD_") != nullptr) return false;
    if (std::strstr(name, "EA_TRAX") != nullptr || std::strstr(name, "EA_Trax") != nullptr) return false;
    return true;
}

// Directly check if ANY non-HUD package is currently active in FEngine.
// When in race and paused or in ANY menu (Pause, Options, Audio, Gameplay, Controller, Video, Dialog):
// this returns true, hiding the racing pad buttons immediately!
bool IsPaused() {
    if (!mem::IsCommitted(kCFEng)) return false;
    uint32_t cfeng = kernel::R32(kCFEng);
    uint32_t fe = (cfeng && mem::IsCommitted(cfeng)) ? kernel::R32(cfeng) : 0;
    if (!fe || !mem::IsCommitted(fe + 0xE4)) return false;
    int count = 0;
    for (uint32_t p = kernel::R32(fe + 0xE4); p && count < 32; p = kernel::R32(p + 8), count++) {
        if (!mem::IsCommitted(p + 0xC)) continue;
        uint32_t name_ptr = kernel::R32(p + 0xC);
        if (name_ptr && mem::IsCommitted(name_ptr)) {
            const char* name = (const char*)kernel::HostPtr(name_ptr);
            if (IsMenuPackageName(name)) {
                return true;
            }
        }
    }
    return false;
}

bool Racing() {
    return mem::IsCommitted(kGameFlowState) && kernel::R32(kGameFlowState) == kGameFlowRacing;
}
// Custom strings for button prompts
struct Prompt {
    uint32_t hash;
    const char* text;
};
const Prompt kPrompts[] = {
    // Carbon-specific title screen hashes (extracted from ZZDATA Global strings chunk):
    {0x8CD6BD6D, "Tap anywhere to begin"}, // Press START
    {0x8CD63CB8, "Tap anywhere to begin"}, // Press START button
    {0x0BE9C967, "Tap anywhere to begin"}, // Press START to begin
    {0x1BAD0AEA, "Tap anywhere to begin"}, // Press START
    // Fallback Most Wanted hashes:
    {0x9B580A55, "Tap anywhere to begin"},
    {0xDC64C04C, "Tap anywhere to begin"},
    {0xD18D4C4C, "Tap anywhere to begin"},
    {0x9D8F2513, "Tap or press"},
};

uint32_t GuestText(const char* text) {
    static std::mutex lock;
    static std::unordered_map<const char*, uint32_t> copies;
    std::lock_guard guard(lock);
    uint32_t& va = copies[text];
    if (!va) {
        size_t n = std::strlen(text) + 1;
        va = mem::AllocSystem(uint32_t(n));
        if (va) std::memcpy(kernel::HostPtr(va), text, n);
    }
    return va;
}

const char* OwnString(uint32_t hash) {
    for (const Prompt& p : kPrompts)
        if (p.hash == hash) return p.text;
    return nullptr;
}

// FEng Touch & Mouse Hook for Carbon:
// 0x46A8E4: FEGameInterface pointer (vtable slot +0x44 is GetMouseInfo)
// 0x46A53C: active FEPackage pointer (byte +1 has bit 0x80 for mouse enabled)
constexpr uint32_t kFEInterfacePtr = 0x46A8E4;
constexpr uint32_t kFEActivePackage = 0x46A53C;
constexpr uint32_t kGetMouseInfoSlot = 0x44;

static std::atomic<bool> g_tap_accept{false};
std::atomic<bool> g_menu{true};
std::atomic<int32_t> g_x{-2000}, g_y{-2000};
std::atomic<bool> g_down{false};
std::atomic<uint32_t> g_presses{0};
constexpr int kMouseFrames = 30;
std::atomic<int> g_mouse_frames{0};

void GetMouseInfo(X86Context& ctx, uint8_t*) {
    constexpr int kHoldUpdates = 2, kStayUpdates = 4;
    constexpr int16_t kAway = -2000;
    static uint32_t seen = 0;
    static int hold = 0, stay = 0;
    uint32_t info = kernel::Args{ctx}[0];
    uint32_t presses = g_presses;
    if (presses != seen) {
        seen = presses;
        hold = kHoldUpdates;
    }
    bool down = g_down || hold > 0;
    if (hold > 0) hold--;
    if (down) stay = kStayUpdates;
    else if (stay > 0) stay--;
    bool here = down || stay > 0;
    kernel::W16(info, uint16_t(here ? int16_t(g_x.load()) : kAway));
    kernel::W16(info + 2, uint16_t(here ? int16_t(g_y.load()) : kAway));
    kernel::W16(info + 4, 0);
    kernel::W16(info + 6, down ? 1 : 0);  // Left button
    kernel::Return(ctx, 0, 1);
}

void FeFrame() {
    if (!mem::IsCommitted(kFEInterfacePtr)) {
        g_menu = false;
        return;
    }
    uint32_t iface = kernel::R32(kFEInterfacePtr);
    if (iface && mem::IsCommitted(iface)) {
        uint32_t vtbl = kernel::R32(iface);
        if (vtbl && mem::IsCommitted(vtbl)) {
            static const uint32_t thunk = dispatch::HostThunk(GetMouseInfo);
            uint32_t slot = vtbl + kGetMouseInfoSlot;
            if (kernel::R32(slot) != thunk) kernel::W32(slot, thunk);
        }
    }
    uint32_t pkg = mem::IsCommitted(kFEActivePackage) ? kernel::R32(kFEActivePackage) : 0;
    bool menu = pkg != 0;
    g_menu = menu;
    int frames = g_mouse_frames.load();
    if (frames > 0) g_mouse_frames.fetch_sub(1);
    if (pkg && mem::IsCommitted(pkg)) {
        // Enable mouse flag bit 0x80 on the active package
        kernel::G<uint8_t>(pkg + 1) |= 0x80;
    }
}

// FeMenuShown: true whenever we are not in a race OR when paused, so the pad buttons hide
// and give way to menu navigation (pills in the corner, swipe, tap anywhere)
bool FeMenuShown() {
    if (!Racing()) return true;
    return IsPaused();
}

bool FeMouseDown() { return g_down.load(); }
bool FeFinger(float& x, float& y) {
    if (!g_down) return false;
    x = float(g_x.load());
    y = float(g_y.load());
    return true;
}

// When in menu, show ENTER (A) and CLOSE (B) pills in the bottom-left corner,
// exactly as Most Wanted does!
FePrompts CurrentFePrompts() {
    FePrompts p{};
    if (FeMenuShown()) {
        p.shown[0] = true; // A - ENTER
        p.shown[1] = true; // B - CLOSE
    }
    return p;
}

void FeTouch(bool down, float u, float v) {
    float wide = 480.0f * Aspect();
    g_x = int32_t((u - 0.5f) * wide);
    g_y = int32_t((v - 0.5f) * 480.0f);
    if (down && !g_down) {
        g_presses++;
        if (FeMenuShown()) {
            g_tap_accept.store(true);
        }
    }
    g_down = down;
    g_mouse_frames = kMouseFrames;
}


bool FeTakeAccept() {
    return g_tap_accept.exchange(false);
}

bool LanguageAvailable(const std::string& code) { return code == "int" || code == "ind"; }

}  // namespace game

namespace app {

// the console's XC_LANGUAGE the game reads at boot: English
uint32_t ConsoleLanguage() { return 1; }

}  // namespace app

// SearchForString overrides in Carbon:
// 1. sub_0011F9A0 (low-level binary search, hash in edx)
X86_EXTERN_FUNC(__imp__sub_0011F9A0);
X86_FUNC(sub_0011F9A0) {
    uint32_t hash = ctx.edx;
    // the title screen's string is edited in place: a string of ours handed back crashed the title screen
    if (hash == 0x186AAECC) {
        __imp__sub_0011F9A0(ctx, base);
        if (ctx.eax && mem::IsCommitted(ctx.eax)) {
            char* s = (char*)kernel::HostPtr(ctx.eax);
            static const char kText[] = "TAP to begin"; // only letters the original has: the title font's glyph set is cut down
            if (std::strlen(s) >= sizeof(kText) - 1) std::memcpy(s, kText, sizeof(kText));
        }
        return;
    }
    if (const char* text = game::OwnString(hash)) {
        if (uint32_t va = game::GuestText(text)) {
            ctx.eax = va;
            ctx.esp += 4;
            return;
        }
    }
    __imp__sub_0011F9A0(ctx, base);
}

// 2. sub_0011FAB0 (high-level SearchForString, hash on stack [esp+4])
X86_EXTERN_FUNC(__imp__sub_0011FAB0);
X86_FUNC(sub_0011FAB0) {
    uint32_t hash = kernel::R32(ctx.esp + 4);
    if (const char* text = game::OwnString(hash)) {
        if (uint32_t va = game::GuestText(text)) {
            ctx.eax = va;
            ctx.esp += 4;
            return;
        }
    }
    __imp__sub_0011FAB0(ctx, base);
}

// Coronas / Headlight Flares in Carbon:
// - sub_00108690: draws Coronas and Headlight Flares
// - sub_0010EC90: "eRenderLightFlares" pass (draws street light flares and neon light trails!)
// Both use cdecl convention without arguments on return (plain 'ret').
// Bypassing both completely eliminates the giant green/white neon circles and connecting trails!
X86_EXTERN_FUNC(__imp__sub_00108690);
X86_FUNC(sub_00108690) {
    ctx.esp += 4;
}

X86_EXTERN_FUNC(__imp__sub_0010EC90);
X86_FUNC(sub_0010EC90) {
    ctx.esp += 4;
}
// Falling Rain in Carbon: sub_00278460 (thiscall, 2 args, ret 8)
X86_EXTERN_FUNC(__imp__sub_00278460);
X86_FUNC(sub_00278460) {
    if (game::Opt().rain) return __imp__sub_00278460(ctx, base);
    ctx.esp += 12; // 4 bytes return address + 8 bytes arguments
}

// Motion Blur in Carbon: sub_0010C5F0 (cdecl, 4 args, pops 4 bytes return address)
// Returns bool in al whether motion blur was drawn.
X86_EXTERN_FUNC(__imp__sub_0010C5F0);
X86_FUNC(sub_0010C5F0) {
    if (game::Opt().motion_blur) return __imp__sub_0010C5F0(ctx, base);
    ctx.eax &= ~0xFFu; // return false
    ctx.esp += 4;
}

// GetAttribute in Profile Manager: sub_0007DD00
// When cheat_cash is enabled, reading Cash (0x1778) returns 9,999,999!
X86_EXTERN_FUNC(__imp__sub_0007DD00);
X86_FUNC(sub_0007DD00) {
    uint32_t attr = kernel::R32(ctx.esp + 4);
    uint32_t out_ptr = kernel::R32(ctx.esp + 8);
    __imp__sub_0007DD00(ctx, base);
    if (game::Opt().cheat_cash && attr == 0x1778 && out_ptr && mem::IsCommitted(out_ptr)) {
        kernel::W32(out_ptr, 9999999);
        ctx.eax = 1;
    }
}

// FEng SwitchPackage hook in Carbon: sub_0012FCD0
X86_EXTERN_FUNC(__imp__sub_0012FCD0);
X86_FUNC(sub_0012FCD0) {
    __imp__sub_0012FCD0(ctx, base);
}

// Unpause in Carbon: sub_0012DFE0
X86_EXTERN_FUNC(__imp__sub_0012DFE0);
X86_FUNC(sub_0012DFE0) {
    __imp__sub_0012DFE0(ctx, base);
}

// Speedbreaker update in Carbon: sub_0023F150 (thiscall, ret 8). Cheat on keeps the meter full across the
// update, which also ends the break on 0
X86_EXTERN_FUNC(__imp__sub_0023F150);
X86_FUNC(sub_0023F150) {
    const uint32_t self = ctx.ecx;
    const bool full = game::Opt().cheat_speedbreaker && mem::IsCommitted(self + 0x80);
    if (full) kernel::W32(self + 0x7C, 0x3F800000u);
    __imp__sub_0023F150(ctx, base);
    if (full) kernel::W32(self + 0x7C, 0x3F800000u);
}

// Nitrous update in Carbon: sub_00207BD0 (thiscall, ret 0xC). The tank is [this + 0xFC], 0..1, drained
// there by the frame's time and floored at 0; cheat on keeps it full across the update
X86_EXTERN_FUNC(__imp__sub_00207BD0);
X86_FUNC(sub_00207BD0) {
    const uint32_t self = ctx.ecx;
    const bool full = game::Opt().cheat_nos && mem::IsCommitted(self + 0x100);
    if (full) kernel::W32(self + 0xFC, 0x3F800000u);
    __imp__sub_00207BD0(ctx, base);
    if (full) kernel::W32(self + 0xFC, 0x3F800000u);
}
