// Yeeps mod SDK helpers. Include this in every mod.
#pragma once
#include "yeeps_mod_api.h"
#include <BNM/Loading.hpp>
#include <BNM/Class.hpp>
#include <BNM/Method.hpp>
#include <BNM/Field.hpp>
#include <BNM/Property.hpp>
#include <BNM/UnityStructures.hpp>
#include <BNM/Utils.hpp>

namespace ym {
    // Call first thing in YeepsMod_Init. Sets up BNM through the game.
    bool init(const YeepsModApi *api);
    const YeepsModApi *api();
    // Game log, max 20 lines a second for all mods together.
    void log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
    // 1 if the server gave this account the role (call from onUpdate only).
    bool hasRole(const char *roleKey);
    // Run fn every frame on the Unity main thread. Errors thrown inside fn are
    // caught and logged here (C++ exceptions must never reach the game); after
    // 30 of them the callback is turned off.
    void onUpdate(void (*fn)(float dt));
    // Run fn when the local player changes (room/scene load).
    void onRoomChange(void (*fn)());
}

#define YM_EXPORT extern "C" __attribute__((visibility("default")))
// YEEPS_MOD("My Mod") declares the mod's name.
#define YEEPS_MOD(NAME) YM_EXPORT const char *YeepsMod_Name() { return NAME; }
