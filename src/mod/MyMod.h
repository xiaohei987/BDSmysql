#pragma once

#include "ll/api/mod/NativeMod.h"
#include "ll/api/event/Listener.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/form/SimpleForm.h"
#include "mc/world/actor/player/Player.h"
#include "mc/server/ServerPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/Item.h"
#include "mc/world/level/Level.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/MutableAttributeWithContext.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/deps/core/utility/optional_ref.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/deps/shared_types/legacy/item/EquipmentSlot.h"
#include "mc/util/ActorInventoryUtils.h"
#include "mod/Database.h"
#include "mod/Config.h"
#include "mod/ServerConfig.h"
#include <unordered_map>
#include <string>
#include <bitset>

namespace bdsmysql {

// 命令参数结构体
struct ServerName {
    std::string name;
};

struct List {};

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
    void onServerStopping();
    void registerCommands();
    void showServerListForm(Player& player);

private:
    ll::mod::NativeMod& mSelf;

    // Map to track player join times for playtime calculation
    std::unordered_map<std::string, std::chrono::system_clock::time_point> mPlayerJoinTimes;
    
    void setPlayerAttributesDelayed(Player& player, const PlayerSyncData& syncData);
};

} // namespace bdsmysql
