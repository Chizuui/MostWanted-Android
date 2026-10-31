// Need for Speed: Carbon boots on the host's XDK like Most Wanted does, but every hook in src/game is cut for Most Wanted's
// addresses. Until Carbon's are found these are the calls the common code makes, doing nothing
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
    // Keep Carbon's default HUD/minimap transform intact (853.0f)
    // Overwriting 0x3F98E0 caused the GPS minimap roads to shift away from its circular rim!
}

// Carbon render switches:
// 0x401D14: World Bloom switch (checked at 0x112AF9, calls 0x110F50 / 0x110E00)
// 0x3F96B3: Particles - Main View switch (checked at 0x112A63)
constexpr uint32_t kWorldBloomSwitch = 0x401D14;
constexpr uint32_t kParticleSwitch = 0x3F96B3;

void ApplyGraphicsSwitches() {
    const Options& o = Opt();
    // When bloom is disabled, turn off Carbon's World Bloom pass to eliminate the blinding overbright flare
    if (mem::IsCommitted(kWorldBloomSwitch)) {
        kernel::G<uint8_t>(kWorldBloomSwitch) = o.bloom ? 1 : 0;
    }
    if (mem::IsCommitted(kParticleSwitch)) {
        kernel::G<uint8_t>(kParticleSwitch) = o.particles ? 1 : 0;
    }
}

// Carbon cheats:
// 0x46D944 holds AICopManager / ICopMgr singleton pointer
// 0x456830 holds the Profile Manager (SetAttribute at 0x7DD50, attr 0x1778 is Cash)
constexpr uint32_t kCopManager = 0x46D944;
constexpr uint32_t kProfileManager = 0x456830;
constexpr uint32_t kSetProfileAttribute = 0x7DD50;
constexpr uint32_t kAttrCash = 0x1778;
constexpr uint32_t kCheatCash = 9999999;

void ApplyCheats() {
    // Cheats in Carbon will be written directly as memory bytes (as in Most Wanted),
    // never by calling guest functions from present (which corrupts the guest stack and causes SIGSEGV).
}

void CaptureFont() {}
bool TakeFont(FontImage&) { return false; }

// In Carbon: 0x46D280 is GameFlowState.
// State 6 is racing/driving. Any other state is menu/loading/FMV.
constexpr uint32_t kGameFlowState = 0x46D280;
constexpr uint32_t kGameFlowRacing = 6;

bool Racing() {
    return mem::IsCommitted(kGameFlowState) && kernel::R32(kGameFlowState) == kGameFlowRacing;
}

// Custom strings for button prompts
struct Prompt {
    uint32_t hash;
    const char* text;
};
const Prompt kPrompts[] = {
    {0x9B580A55, "Tap anywhere"},          // Press START
    {0xDC64C04C, "Tap anywhere"},          // Press START button
    {0xD18D4C4C, "Tap anywhere to begin"}, // Press START to begin
    {0x9D8F2513, "Tap or press"},          // Press
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

// FeMenuShown: true whenever we are not in a race, so the pad buttons hide
// and give way to menu navigation (pills in the corner, swipe, tap anywhere)
bool FeMenuShown() {
    return !Racing();
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

// SearchForString override in Carbon: replace button prompts
X86_EXTERN_FUNC(__imp__sub_0011F9A0);
X86_FUNC(sub_0011F9A0) {
    uint32_t hash = ctx.edx;
    if (const char* text = game::OwnString(hash)) {
        if (uint32_t va = game::GuestText(text)) {
            ctx.eax = va;
            ctx.esp += 4;
            return;
        }
    }
    __imp__sub_0011F9A0(ctx, base);
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
