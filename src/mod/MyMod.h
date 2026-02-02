#pragma once

#include "ll/api/mod/NativeMod.h"
#include "ll/api/event/Listener.h"
#include "mc/world/actor/player/Player.h"
#include "mod/Database.h"
#include "mod/Config.h"
#include <unordered_map>
#include <string>

namespace bdsmysql {

class MyMod {

public:
    static MyMod& getInstance();

    MyMod() : mSelf(*ll::mod::NativeMod::current()) {}

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

    /// @return True if the mod is loaded successfully.
    bool load();

    /// @return True if the mod is enabled successfully.
    bool enable();

    /// @return True if the mod is disabled successfully.
    bool disable();

    void onPlayerJoin(Player& player);
    void onPlayerLeft(Player& player);

private:
    ll::mod::NativeMod& mSelf;

    // Map to track player join times for playtime calculation
    std::unordered_map<std::string, std::chrono::system_clock::time_point> mPlayerJoinTimes;
};

} // namespace bdsmysql
