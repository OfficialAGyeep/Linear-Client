#include "ymsdk.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <exception>

static const YeepsModApi *g_api = nullptr;

// BNM looks every il2cpp function up through the game (see GlobalSettings.hpp).
extern "C" void *ymsdk_dlsym(void *handle, const char *name) {
    if (!name) return nullptr;
    if (g_api && strncmp(name, "il2cpp_", 7) == 0) return g_api->resolve(name);
    return dlsym(handle ? handle : (g_api ? g_api->il2cppHandle : RTLD_DEFAULT), name);
}
extern "C" int ymsdk_hook(void *target, void *replacement, void **original) {
    return g_api ? g_api->hook(target, replacement, original) : -1;
}
extern "C" int ymsdk_unhook(void *target) {
    return g_api ? g_api->unhook(target) : -1;
}

static void *Finder(const char *name, void *) { return ymsdk_dlsym(nullptr, name); }
static void NoOp() {}

bool ym::init(const YeepsModApi *api) {
    if (!api || api->version < 1 || !api->resolve || !api->hook) return false;
    g_api = api;
    BNM::Loading::AddOnLoadedEvent(NoOp);   // BNM needs at least one event
    BNM::Loading::SetMethodFinder(Finder, nullptr);
    BNM::Loading::TrySetupByUsersFinder();  // the game's il2cpp is already running
    return BNM::IsLoaded();
}
const YeepsModApi *ym::api() { return g_api; }
void ym::log(const char *fmt, ...) {
    if (!g_api || !g_api->log) return;
    char buf[320];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    g_api->log(buf);
}
bool ym::hasRole(const char *roleKey) { return g_api && g_api->hasRole && g_api->hasRole(roleKey); }

// Exceptions are caught on the mod's side of the boundary: the game and each
// mod have their own C++ runtime, so an exception must not cross into the game.
static constexpr int kMaxCallbacks = 32, kMaxErrors = 30;
static void (*g_updates[kMaxCallbacks])(float);
static int g_updateErrors[kMaxCallbacks], g_updateN = 0;
static void (*g_rooms[kMaxCallbacks])();
static int g_roomN = 0;
static void Dispatch(float dt) {
    for (int i = 0; i < g_updateN; i++) {
        if (!g_updates[i] || g_updateErrors[i] > kMaxErrors) continue;
        try { g_updates[i](dt); }
        catch (const std::exception &e) { if (++g_updateErrors[i] <= 3) ym::log("update error: %s", e.what()); }
        catch (...) { if (++g_updateErrors[i] <= 3) ym::log("update error (unknown)"); }
        if (g_updateErrors[i] == kMaxErrors + 1) ym::log("update callback %d turned off: too many errors", i);
    }
}
static void DispatchRoom() {
    for (int i = 0; i < g_roomN; i++) {
        try { g_rooms[i](); } catch (...) { ym::log("room-change callback %d threw", i); }
    }
}
void ym::onUpdate(void (*fn)(float)) {
    if (!g_api || !fn || g_updateN >= kMaxCallbacks) return;
    if (g_updateN == 0) g_api->onUpdate(Dispatch);
    g_updates[g_updateN++] = fn;
}
void ym::onRoomChange(void (*fn)()) {
    if (!g_api || !fn || g_roomN >= kMaxCallbacks) return;
    if (g_roomN == 0) g_api->onRoomChange(DispatchRoom);
    g_rooms[g_roomN++] = fn;
}
