#include "mod/MyMod.h"

#include "ll/api/mod/RegisterHelper.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/server/ServerStoppingEvent.h"
#include "ll/api/command/CommandHandle.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/Item.h"
#include "mc/world/level/Level.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/MutableAttributeWithContext.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/deps/core/utility/optional_ref.h"
#include "mc/nbt/CompoundTag.h"
#include "mc/network/packet/TransferPacket.h"
#include "mc/network/packet/UpdateAttributesPacket.h"
#include "mc/server/commands/MinecraftCommands.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/world/level/CommandOriginSystem.h"
#include "mc/server/commands/CurrentCmdVersion.h"
#include "mod/ServerConfig.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <bitset>
#include <thread>
#include <mutex>
#include <future>

namespace bdsmysql {

MyMod& MyMod::getInstance() {
    static MyMod instance;
    return instance;
}

bool MyMod::load() {
    getSelf().getLogger().debug("\033[33m[BDSmysql] 正在加载插件...\033[0m");

    if (!Config::getInstance().load()) {
        getSelf().getLogger().error("\033[31m[BDSmysql] 加载配置文件失败！\033[0m");
        return false;
    }

    return true;
}

bool MyMod::enable() {
    getSelf().getLogger().debug("\033[33m[BDSmysql] 正在启用插件...\033[0m");

    if (!Config::getInstance().load()) {
        getSelf().getLogger().error("\033[31m[BDSmysql] 加载配置文件失败！\033[0m");
        return false;
    }

    if (!ServerConfigManager::getInstance().load()) {
        getSelf().getLogger().error("\033[31m[BDSmysql] 加载服务器配置文件失败！\033[0m");
        return false;
    }

    if (!Database::getInstance().connect()) {
        getSelf().getLogger().error("\033[31m[BDSmysql] 连接数据库失败！\033[0m");
        return false;
    }

    if (!Database::getInstance().initTables()) {
        getSelf().getLogger().error("\033[31m[BDSmysql] 初始化数据表失败！\033[0m");
        return false;
    }

    auto& eventBus = ll::event::EventBus::getInstance();

    eventBus.emplaceListener<ll::event::PlayerJoinEvent>(
        [this](ll::event::PlayerJoinEvent& event) {
            auto& player = event.self();
            onPlayerJoin(player);
        }
    );

    eventBus.emplaceListener<ll::event::PlayerDisconnectEvent>(
        [this](ll::event::PlayerDisconnectEvent& event) {
            auto& player = event.self();
            onPlayerLeft(player);
        }
    );

    eventBus.emplaceListener<ll::event::ServerStoppingEvent>(
        [this](ll::event::ServerStoppingEvent& event) {
            onServerStopping();
        }
    );

    // 注册 /tpserver 命令
    registerCommands();

    getSelf().getLogger().info("\033[1;32m[BDSmysql] 插件启用成功！\033[0m");
    return true;
}

bool MyMod::disable() {
    getSelf().getLogger().debug("\033[33m[BDSmysql] 正在禁用插件...\033[0m");

    Database::getInstance().disconnect();
    getSelf().getLogger().info("\033[32m[BDSmysql] 插件禁用成功！\033[0m");
    return true;
}

void MyMod::onPlayerJoin(Player& player) {
    std::string uuid = player.getUuid().asString();
    std::string name = player.getRealName();
    std::string xuid = player.getXuid();

    auto now = std::chrono::system_clock::now();
    mPlayerJoinTimes[uuid] = now;

    getSelf().getLogger().info("\033[32m[玩家] 玩家 {} ({}) 加入了服务器\033[0m", name, uuid);

    auto& serverPlayer = static_cast<ServerPlayer&>(player);
    std::string serverName = Config::getInstance().getDatabaseConfig().serverName;

    // ===== 处理玩家属性数据（生命值、饱食度、经验） =====
    PlayerSyncData syncData;
    bool hasSyncData = Database::getInstance().isPlayerExists(uuid) && Database::getInstance().loadPlayerSyncData(uuid, serverName, syncData);

    if (!hasSyncData) {
        // 玩家没有数据库数据：创建默认记录
        getSelf().getLogger().info("\033[33m[经验同步] 玩家 {} 没有数据库数据，创建默认记录\033[0m", name);
        
        // 收集玩家当前状态并保存
        syncData.uuid = uuid;
        syncData.serverName = serverName;
        syncData.gamemode = static_cast<int>(player.getPlayerGameType());

        auto healthAttr = player.getAttribute(SharedAttributes::HEALTH());
        syncData.health = static_cast<int>(healthAttr.mCurrentValue);
        syncData.maxHealth = static_cast<int>(healthAttr.mCurrentMaxValue);

        auto hungerAttr = player.getAttribute(Player::HUNGER());
        syncData.food = static_cast<int>(hungerAttr.mCurrentValue);

        auto saturationAttr = player.getAttribute(Player::SATURATION());
        syncData.foodSaturation = static_cast<float>(saturationAttr.mCurrentValue);  // 修复：改为float类型

        auto xpAttr = player.getAttribute(Player::EXPERIENCE());
        syncData.expLevel = static_cast<int>(xpAttr.mCurrentMaxValue);
        syncData.expPoints = static_cast<int>(xpAttr.mCurrentValue * 100);

        Database::getInstance().savePlayerSyncData(syncData);
        getSelf().getLogger().info("\033[32m[数据同步] 已创建玩家 {} 的默认数据记录\033[0m", name);
    } else {
        // 玩家有数据库数据：直接加载属性（在主线程中）
        getSelf().getLogger().info("\033[33m[数据同步] 玩家 {} 有数据库数据，正在加载\033[0m", name);
        getSelf().getLogger().info("\033[33m[数据同步] 数据库数据 - 生命值: " + std::to_string(syncData.health) + 
            ", 饱食度: " + std::to_string(syncData.food) + 
            ", 饱和度: " + std::to_string(syncData.foodSaturation) + 
            ", 经验等级: " + std::to_string(syncData.expLevel) + 
            ", 经验点数: " + std::to_string(syncData.expPoints) + "\033[0m");

        // 直接设置玩家属性
        setPlayerAttributesDelayed(player, syncData);
        getSelf().getLogger().info("\033[32m[数据同步] 属性加载完成\033[0m");
    }

    // ===== 处理背包和装备数据 =====
    // 先检查是否有数据库数据
    std::vector<PlayerBackpackItem> tempBackpack;
    std::vector<PlayerEquipmentItem> tempEquipment;
    bool hasBackpackData = Database::getInstance().loadPlayerBackpack(uuid, serverName, tempBackpack);
    bool hasEquipmentData = Database::getInstance().loadPlayerEquipment(uuid, serverName, tempEquipment);
    bool hasInventoryData = hasBackpackData || hasEquipmentData;

    if (!hasInventoryData) {
        // ===== 玩家没有数据库数据：保存当前背包和装备到数据库 =====
        getSelf().getLogger().info("\033[33m[数据同步] 玩家 {} 没有背包/装备数据，保存当前数据到数据库\033[0m", name);

        // ===== 保存背包数据（槽位 0-35） =====
        getSelf().getLogger().info("\033[33m[背包同步] 正在保存玩家 {} 的背包数据（槽位 0-35）...\033[0m", name);
        std::vector<PlayerBackpackItem> backpackItems;
        auto& playerInv = player.getInventory();
        int containerSize = playerInv.getContainerSize();

        int inventorySlots = std::min(containerSize, 36);
        for (int i = 0; i < inventorySlots; i++) {
            auto& itemStack = playerInv.getItem(i);
            
            PlayerBackpackItem item;
            item.slot = i;
            item.itemType = "";  // 空槽位的类型为空字符串
            item.count = 0;
            item.damage = 0;
            item.nbt = "";
            
            if (!itemStack.isNull()) {
                if (itemStack.mItem) {
                    item.itemType = itemStack.mItem->getSerializedName();
                }
                item.count = static_cast<int>(itemStack.mCount);
                item.damage = itemStack.mAuxValue;
                if (itemStack.mUserData) {
                    try {
                        item.nbt = itemStack.mUserData->toSnbt();
                    } catch (const std::exception& e) {
                        getSelf().getLogger().warn("\033[33m[背包同步] 序列化背包物品 (槽位 {}) 的 NBT 数据失败: {}\033[0m", i, e.what());
                    }
                }
            }
            
            backpackItems.push_back(item);
        }

        if (Database::getInstance().savePlayerBackpack(uuid, serverName, backpackItems)) {
            getSelf().getLogger().info("\033[32m[背包同步] 已保存玩家 {} 的 {} 个背包槽位\033[0m", name, backpackItems.size());
        }

        // ===== 保存装备数据（槽位 36-40） =====
        getSelf().getLogger().info("\033[33m[装备同步] 正在保存玩家 {} 的装备数据（槽位 36-40）...\033[0m", name);
        std::vector<PlayerEquipmentItem> equipmentItems;

        // 使用 ActorInventoryUtils 获取装备数据
        auto* headItem = ActorInventoryUtils::getItem(player, SharedTypes::Legacy::EquipmentSlot::Head, 0);
        auto* torsoItem = ActorInventoryUtils::getItem(player, SharedTypes::Legacy::EquipmentSlot::Torso, 0);
        auto* legsItem = ActorInventoryUtils::getItem(player, SharedTypes::Legacy::EquipmentSlot::Legs, 0);
        auto* feetItem = ActorInventoryUtils::getItem(player, SharedTypes::Legacy::EquipmentSlot::Feet, 0);
        auto* offhandItem = ActorInventoryUtils::getItem(player, SharedTypes::Legacy::EquipmentSlot::Offhand, 0);

        // 保存头盔（槽位 36）
        if (headItem && !headItem->isNull()) {
            PlayerEquipmentItem item;
            item.slot = 36;
            if (headItem->mItem) {
                item.itemType = headItem->mItem->getSerializedName();
                getSelf().getLogger().info("\033[33m[装备同步] 保存装备: {} 在槽位 {}\033[0m", item.itemType, 36);
            }
            item.count = static_cast<int>(headItem->mCount);
            item.damage = headItem->mAuxValue;
            if (headItem->mUserData) {
                try {
                    item.nbt = headItem->mUserData->toSnbt();
                } catch (const std::exception& e) {
                    getSelf().getLogger().warn("\033[33m[装备同步] 序列化装备 NBT 数据失败: {}\033[0m", e.what());
                }
            }
            equipmentItems.push_back(item);
        }

        // 保存胸甲（槽位 37）
        if (torsoItem && !torsoItem->isNull()) {
            PlayerEquipmentItem item;
            item.slot = 37;
            if (torsoItem->mItem) {
                item.itemType = torsoItem->mItem->getSerializedName();
                getSelf().getLogger().info("\033[33m[装备同步] 保存装备: {} 在槽位 {}\033[0m", item.itemType, 37);
            }
            item.count = static_cast<int>(torsoItem->mCount);
            item.damage = torsoItem->mAuxValue;
            if (torsoItem->mUserData) {
                try {
                    item.nbt = torsoItem->mUserData->toSnbt();
                } catch (const std::exception& e) {
                    getSelf().getLogger().warn("\033[33m[装备同步] 序列化装备 NBT 数据失败: {}\033[0m", e.what());
                }
            }
            equipmentItems.push_back(item);
        }

        // 保存护腿（槽位 38）
        if (legsItem && !legsItem->isNull()) {
            PlayerEquipmentItem item;
            item.slot = 38;
            if (legsItem->mItem) {
                item.itemType = legsItem->mItem->getSerializedName();
                getSelf().getLogger().info("\033[33m[装备同步] 保存装备: {} 在槽位 {}\033[0m", item.itemType, 38);
            }
            item.count = static_cast<int>(legsItem->mCount);
            item.damage = legsItem->mAuxValue;
            if (legsItem->mUserData) {
                try {
                    item.nbt = legsItem->mUserData->toSnbt();
                } catch (const std::exception& e) {
                    getSelf().getLogger().warn("\033[33m[装备同步] 序列化装备 NBT 数据失败: {}\033[0m", e.what());
                }
            }
            equipmentItems.push_back(item);
        }

        // 保存靴子（槽位 39）
        if (feetItem && !feetItem->isNull()) {
            PlayerEquipmentItem item;
            item.slot = 39;
            if (feetItem->mItem) {
                item.itemType = feetItem->mItem->getSerializedName();
                getSelf().getLogger().info("\033[33m[装备同步] 保存装备: {} 在槽位 {}\033[0m", item.itemType, 39);
            }
            item.count = static_cast<int>(feetItem->mCount);
            item.damage = feetItem->mAuxValue;
            if (feetItem->mUserData) {
                try {
                    item.nbt = feetItem->mUserData->toSnbt();
                } catch (const std::exception& e) {
                    getSelf().getLogger().warn("\033[33m[装备同步] 序列化装备 NBT 数据失败: {}\033[0m", e.what());
                }
            }
            equipmentItems.push_back(item);
        }

        // 保存副手（槽位 40）
        if (offhandItem && !offhandItem->isNull()) {
            PlayerEquipmentItem item;
            item.slot = 40;
            if (offhandItem->mItem) {
                item.itemType = offhandItem->mItem->getSerializedName();
                getSelf().getLogger().info("\033[33m[装备同步] 保存装备: {} 在槽位 {}\033[0m", item.itemType, 40);
            }
            item.count = static_cast<int>(offhandItem->mCount);
            item.damage = offhandItem->mAuxValue;
            if (offhandItem->mUserData) {
                try {
                    item.nbt = offhandItem->mUserData->toSnbt();
                } catch (const std::exception& e) {
                    getSelf().getLogger().warn("\033[33m[装备同步] 序列化装备 NBT 数据失败: {}\033[0m", e.what());
                }
            }
            equipmentItems.push_back(item);
        }

        if (Database::getInstance().savePlayerEquipment(uuid, serverName, equipmentItems)) {
            getSelf().getLogger().info("\033[32m[装备同步] 已保存玩家 {} 的 {} 个装备\033[0m", name, equipmentItems.size());
        }

        getSelf().getLogger().info("\033[32m[数据同步] 已保存玩家 {} 的背包和装备数据到数据库\033[0m", name);
    } else {
        // ===== 玩家有数据库数据：直接加载数据库数据覆盖玩家数据 =====
        getSelf().getLogger().info("\033[33m[数据同步] 玩家 {} 有背包/装备数据，正在加载\033[0m", name);

        // 先加载数据库数据
        std::vector<PlayerBackpackItem> backpackItems;
        std::vector<PlayerEquipmentItem> equipmentItems;
        
        Database::getInstance().loadPlayerBackpack(uuid, serverName, backpackItems);
        Database::getInstance().loadPlayerEquipment(uuid, serverName, equipmentItems);

        getSelf().getLogger().info("\033[33m[背包同步] 已加载 {} 个背包物品\033[0m", backpackItems.size());
        getSelf().getLogger().info("\033[33m[装备同步] 已加载 {} 个装备物品\033[0m", equipmentItems.size());

        ItemStack emptyStack;

        // 加载背包数据（槽位 0-35），直接覆盖
        auto& playerInv = player.getInventory();
        int backpackCount = 0;
        
        for (const auto& item : backpackItems) {
            ItemStack stack;
            
            // 如果物品类型为空字符串，说明是空槽位
            if (item.itemType.empty()) {
                stack = emptyStack;
            } else {
                stack = ItemStack(item.itemType, item.count, item.damage);

                // 应用 NBT 数据（包括附魔）
                if (!item.nbt.empty()) {
                    try {
                        auto nbtResult = CompoundTag::fromSnbt(item.nbt);
                        if (nbtResult) {
                            stack.mUserData = std::make_unique<CompoundTag>(std::move(*nbtResult));
                        }
                    } catch (const std::exception& e) {
                        getSelf().getLogger().warn("\033[33m[背包同步] 应用物品 {} (槽位 {}) 的 NBT 数据失败: {}\033[0m", item.itemType, item.slot, e.what());
                    }
                }
                backpackCount++;
            }

            // 设置背包物品
            playerInv.setItem(item.slot, stack);
        }

        getSelf().getLogger().info("\033[32m[背包同步] 已应用 {} 个背包物品\033[0m", backpackCount);

        // 加载装备数据（槽位 36-40），直接覆盖
        int armorCount = 0;
        std::bitset<5> armorSlotsToSync;

        for (const auto& item : equipmentItems) {
            ItemStack stack;
            
            // 如果物品类型为空字符串，说明是空槽位
            if (item.itemType.empty()) {
                stack = emptyStack;
            } else {
                stack = ItemStack(item.itemType, item.count, item.damage);

                // 应用 NBT 数据（包括附魔）
                if (!item.nbt.empty()) {
                    try {
                        auto nbtResult = CompoundTag::fromSnbt(item.nbt);
                        if (nbtResult) {
                            stack.mUserData = std::make_unique<CompoundTag>(std::move(*nbtResult));
                            getSelf().getLogger().info("\033[33m[装备同步] 已应用物品 {} (槽位 {}) 的 NBT 数据\033[0m", item.itemType, item.slot);
                        }
                    } catch (const std::exception& e) {
                        getSelf().getLogger().warn("\033[33m[装备同步] 应用物品 {} (槽位 {}) 的 NBT 数据失败: {}\033[0m", item.itemType, item.slot, e.what());
                    }
                }
            }

            // 设置装备和副手（36-40）
            if (item.slot == 36) {
                // 头盔
                serverPlayer.setArmor(SharedTypes::Legacy::ArmorSlot::Head, stack);
                armorSlotsToSync.set(0);
                if (!item.itemType.empty()) armorCount++;
                getSelf().getLogger().info("\033[33m[装备同步] 已设置头盔槽位: {}\033[0m", item.itemType.empty() ? "(空)" : item.itemType);
            } else if (item.slot == 37) {
                // 胸甲
                serverPlayer.setArmor(SharedTypes::Legacy::ArmorSlot::Torso, stack);
                armorSlotsToSync.set(1);
                if (!item.itemType.empty()) armorCount++;
                getSelf().getLogger().info("\033[33m[装备同步] 已设置胸甲槽位: {}\033[0m", item.itemType.empty() ? "(空)" : item.itemType);
            } else if (item.slot == 38) {
                // 护腿
                serverPlayer.setArmor(SharedTypes::Legacy::ArmorSlot::Legs, stack);
                armorSlotsToSync.set(2);
                if (!item.itemType.empty()) armorCount++;
                getSelf().getLogger().info("\033[33m[装备同步] 已设置护腿槽位: {}\033[0m", item.itemType.empty() ? "(空)" : item.itemType);
            } else if (item.slot == 39) {
                // 靴子
                serverPlayer.setArmor(SharedTypes::Legacy::ArmorSlot::Feet, stack);
                armorSlotsToSync.set(3);
                if (!item.itemType.empty()) armorCount++;
                getSelf().getLogger().info("\033[33m[装备同步] 已设置靴子槽位: {}\033[0m", item.itemType.empty() ? "(空)" : item.itemType);
            } else if (item.slot == 40) {
                // 副手
                serverPlayer.setOffhandSlot(stack);
                if (!item.itemType.empty()) armorCount++;
                getSelf().getLogger().info("\033[33m[装备同步] 已设置副手槽位: {}\033[0m", item.itemType.empty() ? "(空)" : item.itemType);
            }
        }

        // 同步装备到客户端
        if (armorSlotsToSync.any()) {
            serverPlayer.sendArmor(armorSlotsToSync);
        }
        serverPlayer.sendInventory(false);

        getSelf().getLogger().info("\033[32m[装备同步] 已应用 {} 个装备\033[0m", armorCount);
        getSelf().getLogger().info("\033[32m[数据同步] 已加载玩家 {} 的背包和装备数据\033[0m", name);
    }

    // 更新玩家基础数据
    PlayerData data;
    data.uuid     = uuid;
    data.name     = name;
    data.xuid     = xuid;
    data.playTime = 0;
    data.isOnline = true;

    if (Database::getInstance().isPlayerExists(uuid)) {
        Database::getInstance().loadPlayerData(uuid, data);
        data.isOnline = true;
        Database::getInstance().updatePlayerData(data);
        getSelf().getLogger().info("\033[32m[玩家] 已更新玩家 {} 的数据\033[0m", name);
    } else {
        // 新玩家：创建记录
        Database::getInstance().savePlayerData(data);
        getSelf().getLogger().info("\033[32m[玩家] 已保存新玩家 {} 的数据\033[0m", name);
    }
}

void MyMod::onPlayerLeft(Player& player) {
    std::string uuid = player.getUuid().asString();
    std::string name = player.getRealName();

    auto it = mPlayerJoinTimes.find(uuid);
    if (it == mPlayerJoinTimes.end()) {
        getSelf().getLogger().warn("\033[33m[玩家] 未找到玩家 {} 的加入时间\033[0m", name);
        return;
    }

    auto joinTime  = it->second;
    auto leaveTime = std::chrono::system_clock::now();
    auto duration  = std::chrono::duration_cast<std::chrono::seconds>(leaveTime - joinTime).count();

    mPlayerJoinTimes.erase(it);

    getSelf().getLogger().info("\033[32m[玩家] 玩家 {} 离开了服务器 (本次游玩时间: {}秒)\033[0m", name, duration);

    // 更新玩家数据
    PlayerData data;
    if (Database::getInstance().loadPlayerData(uuid, data)) {
        data.playTime += static_cast<int>(duration);
        data.isOnline = false;
        Database::getInstance().updatePlayerData(data);
        getSelf().getLogger().info("\033[32m[玩家] 已更新玩家 {} 的数据 (总游玩时间: {}秒)\033[0m", name, data.playTime);
    }

    // 保存玩家属性数据到数据库（包括生命值、饱食度、经验等）
    getSelf().getLogger().info("\033[33m[数据同步] 正在保存玩家 {} 的属性数据...\033[0m", name);

    std::string serverName = Config::getInstance().getDatabaseConfig().serverName;

    // 收集玩家属性数据
    PlayerSyncData syncData;
    syncData.uuid = uuid;
    syncData.serverName = serverName;

    // 获取游戏模式
    syncData.gamemode = static_cast<int>(player.getPlayerGameType());

    // 获取生命值
    try {
        auto healthAttr = player.getAttribute(SharedAttributes::HEALTH());
        syncData.health = static_cast<int>(healthAttr.mCurrentValue);
        syncData.maxHealth = static_cast<int>(healthAttr.mCurrentMaxValue);
        getSelf().getLogger().info("\033[33m[数据同步] 获取生命值 - 当前: {}/{}\033[0m", syncData.health, syncData.maxHealth);
    } catch (const std::exception& e) {
        getSelf().getLogger().warn("\033[33m[数据同步] 获取生命值失败: {}\033[0m", e.what());
        syncData.health = 20;
        syncData.maxHealth = 20;
    }

    // 获取饱食度
    try {
        auto hungerAttr = player.getAttribute(Player::HUNGER());
        syncData.food = static_cast<int>(hungerAttr.mCurrentValue);
        getSelf().getLogger().info("\033[33m[数据同步] 获取饱食度 - 当前: {}\033[0m", syncData.food);
    } catch (const std::exception& e) {
        getSelf().getLogger().warn("\033[33m[数据同步] 获取饱食度失败: " + std::string(e.what()) + "\033[0m");
        syncData.food = 20;
    }

    // 获取饱和度
    try {
        auto saturationAttr = player.getAttribute(Player::SATURATION());
        syncData.foodSaturation = static_cast<float>(saturationAttr.mCurrentValue);
        getSelf().getLogger().info("\033[33m[数据同步] 获取饱和度 - 当前: " + std::to_string(syncData.foodSaturation) + "\033[0m");
    } catch (const std::exception& e) {
        getSelf().getLogger().warn("\033[33m[数据同步] 获取饱和度失败: " + std::string(e.what()) + "\033[0m");
        syncData.foodSaturation = 20.0f;
    }

    // 获取经验值和等级
    try {
        auto xpAttr = player.getAttribute(Player::EXPERIENCE());
        syncData.expLevel = static_cast<int>(xpAttr.mCurrentMaxValue);
        float xpProgress = xpAttr.mCurrentValue;
        syncData.expPoints = static_cast<int>(xpProgress * 100);
        getSelf().getLogger().info("\033[33m[数据同步] 获取经验 - 等级: {}, 进度: {} ({}%)\033[0m", 
            syncData.expLevel, xpProgress, syncData.expPoints);
    } catch (const std::exception& e) {
        getSelf().getLogger().warn("\033[33m[数据同步] 获取经验值失败: {}\033[0m", e.what());
        syncData.expLevel = 0;
        syncData.expPoints = 0;
    }
    
    getSelf().getLogger().info("\033[33m[数据同步] 属性数据 - 生命值: " + std::to_string(syncData.health) + "/" + std::to_string(syncData.maxHealth) + 
        ", 饱食度: " + std::to_string(syncData.food) + 
        ", 饱和度: " + std::to_string(syncData.foodSaturation) + 
        ", 经验等级: " + std::to_string(syncData.expLevel) + 
        ", 经验点数: " + std::to_string(syncData.expPoints) + "\033[0m");

    // 保存属性数据
    if (Database::getInstance().savePlayerSyncData(syncData)) {
        getSelf().getLogger().info("\033[32m[数据同步] 已保存玩家 {} 的属性数据\033[0m", name);
    } else {
        getSelf().getLogger().error("\033[31m[数据同步] 保存玩家 {} 的属性数据失败\033[0m", name);
    }

    // ===== 保存背包数据（槽位 0-35） =====
    getSelf().getLogger().info("\033[33m[背包同步] 正在保存玩家 {} 的背包数据（槽位 0-35）...\033[0m", name);
    
    std::vector<PlayerBackpackItem> backpackItems;
    try {
        auto& playerInv = player.getInventory();
        int containerSize = playerInv.getContainerSize();
        
        int inventorySlots = std::min(containerSize, 36);
        for (int i = 0; i < inventorySlots; i++) {
            auto& itemStack = playerInv.getItem(i);
            
            PlayerBackpackItem item;
            item.slot = i;
            item.itemType = "";  // 空槽位的类型为空字符串
            item.count = 0;
            item.damage = 0;
            item.nbt = "";
            
            if (!itemStack.isNull()) {
                if (itemStack.mItem) {
                    item.itemType = itemStack.mItem->getSerializedName();
                }
                item.count = static_cast<int>(itemStack.mCount);
                item.damage = itemStack.mAuxValue;

                if (itemStack.mUserData) {
                    try {
                        item.nbt = itemStack.mUserData->toSnbt();
                    } catch (const std::exception& e) {
                        getSelf().getLogger().warn("\033[33m[背包同步] 序列化背包物品 (槽位 {}) 的 NBT 数据失败: {}\033[0m", i, e.what());
                    }
                }
            }
            
            backpackItems.push_back(item);
        }

        if (Database::getInstance().savePlayerBackpack(uuid, serverName, backpackItems)) {
            getSelf().getLogger().info("\033[32m[背包同步] 已保存 {} 个背包槽位\033[0m", backpackItems.size());
        } else {
            getSelf().getLogger().error("\033[31m[背包同步] 保存背包数据失败\033[0m");
        }
    } catch (const std::exception& e) {
        getSelf().getLogger().error("\033[31m[背包同步] 保存背包数据失败: {}\033[0m", e.what());
    }

    // ===== 保存装备数据（槽位 36-40） =====
    getSelf().getLogger().info("\033[33m[装备同步] 正在保存玩家 {} 的装备数据（槽位 36-40）...\033[0m", name);
    
    std::vector<PlayerEquipmentItem> equipmentItems;
    try {
        // 使用 ActorInventoryUtils 获取装备数据
        auto* headItem = ActorInventoryUtils::getItem(player, SharedTypes::Legacy::EquipmentSlot::Head, 0);
        auto* torsoItem = ActorInventoryUtils::getItem(player, SharedTypes::Legacy::EquipmentSlot::Torso, 0);
        auto* legsItem = ActorInventoryUtils::getItem(player, SharedTypes::Legacy::EquipmentSlot::Legs, 0);
        auto* feetItem = ActorInventoryUtils::getItem(player, SharedTypes::Legacy::EquipmentSlot::Feet, 0);
        auto* offhandItem = ActorInventoryUtils::getItem(player, SharedTypes::Legacy::EquipmentSlot::Offhand, 0);
        
        // 保存所有装备槽位（包括空槽位）
        std::array<const ItemStack*, 5> equipmentSlots = {headItem, torsoItem, legsItem, feetItem, offhandItem};
        
        for (int i = 0; i < 5; i++) {
            int slot = 36 + i;  // 36-40
            const ItemStack* itemPtr = equipmentSlots[i];
            
            PlayerEquipmentItem item;
            item.slot = slot;
            item.itemType = "";  // 空槽位的类型为空字符串
            item.count = 0;
            item.damage = 0;
            item.nbt = "";
            
            if (itemPtr && !itemPtr->isNull()) {
                if (itemPtr->mItem) {
                    item.itemType = itemPtr->mItem->getSerializedName();
                    getSelf().getLogger().info("\033[33m[装备同步] 找到装备: {} 在槽位 {}\033[0m", item.itemType, slot);
                }
                item.count = static_cast<int>(itemPtr->mCount);
                item.damage = itemPtr->mAuxValue;

                if (itemPtr->mUserData) {
                    try {
                        item.nbt = itemPtr->mUserData->toSnbt();
                    } catch (const std::exception& e) {
                        getSelf().getLogger().warn("\033[33m[装备同步] 序列化装备 (槽位 {}) 的 NBT 数据失败: {}\033[0m", slot, e.what());
                    }
                }
            }

            equipmentItems.push_back(item);
        }

        if (Database::getInstance().savePlayerEquipment(uuid, serverName, equipmentItems)) {
            getSelf().getLogger().info("\033[32m[装备同步] 已保存 {} 个装备物品\033[0m", equipmentItems.size());
        } else {
            getSelf().getLogger().error("\033[31m[装备同步] 保存装备数据失败\033[0m");
        }
    } catch (const std::exception& e) {
        getSelf().getLogger().error("\033[31m[装备同步] 保存装备数据失败: {}\033[0m", e.what());
    }
}

void MyMod::onServerStopping() {
    // 遍历所有在线玩家，保存他们的数据
    int savedCount = 0;
    for (const auto& [uuid, joinTime] : mPlayerJoinTimes) {
        PlayerData data;
        if (Database::getInstance().loadPlayerData(uuid, data)) {
            auto leaveTime = std::chrono::system_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(leaveTime - joinTime).count();
            data.playTime += static_cast<int>(duration);
            data.isOnline = false;
            Database::getInstance().updatePlayerData(data);
            savedCount++;
        }
    }

    // 清空在线玩家列表
    mPlayerJoinTimes.clear();

    getSelf().getLogger().info("\033[32m[BDSmysql] 已保存 {} 个玩家的数据\033[0m", savedCount);
}

void MyMod::registerCommands() {
    using ll::command::CommandRegistrar;

    auto& cmd = CommandRegistrar::getInstance().getOrCreateCommand(
        "tpserver",
        "跳转到指定的服务器",
        CommandPermissionLevel::Any);

    // 无参数：显示服务器选择 UI
    cmd.overload()
        .execute([](CommandOrigin const& origin, CommandOutput& output) {
            Player* player = nullptr;
            if (origin.getEntity() && origin.getEntity()->isType(ActorType::Player)) {
                player = static_cast<Player*>(origin.getEntity());
            }

            if (!player) {
                output.error("\033[31m此命令只能由玩家执行\033[0m");
                return;
            }

            MyMod::getInstance().showServerListForm(*player);
        });

    // 有参数：直接传送到指定服务器
    cmd.overload<ServerName>()
        .required("name")
        .execute([](CommandOrigin const& origin, CommandOutput& output, ServerName const& serverName) {
            // 获取目标服务器配置
            auto& servers = ServerConfigManager::getInstance().getServers();
            auto targetIt = std::find_if(servers.begin(), servers.end(),
                [&serverName](const ServerConfig& server) { return server.name == serverName.name; });

            if (targetIt == servers.end()) {
                output.error("\033[31m未找到服务器: {}\033[0m", serverName.name);
                return;
            }

            auto& targetServer = *targetIt;

            // 获取命令执行者
            Player* player = nullptr;
            if (origin.getEntity() && origin.getEntity()->isType(ActorType::Player)) {
                player = static_cast<Player*>(origin.getEntity());
            }

            if (!player) {
                output.error("\033[31m此命令只能由玩家执行\033[0m");
                return;
            }

            // 获取当前玩家
            std::string uuid = player->getUuid().asString();
            std::string name = player->getRealName();

            auto& mod = MyMod::getInstance();

            mod.getSelf().getLogger().info("\033[33m[传送] 玩家 {} ({}) 请求传送到服务器: {}\033[0m", name, uuid, targetServer.name);

            // 保存当前玩家数据
            try {
                auto& playerInv = player->getInventory();
                int containerSize = playerInv.getContainerSize();

                // 收集玩家当前状态
                PlayerSyncData syncData;
                syncData.uuid = uuid;
                syncData.serverName = Config::getInstance().getDatabaseConfig().serverName;

                // 获取游戏模式
                syncData.gamemode = static_cast<int>(player->getPlayerGameType());

                // 获取生命值
                auto healthAttr = player->getAttribute(SharedAttributes::HEALTH());
                syncData.health = static_cast<int>(healthAttr.mCurrentValue);
                syncData.maxHealth = static_cast<int>(healthAttr.mCurrentMaxValue);

                // 获取饱食度
                auto hungerAttr = player->getAttribute(Player::HUNGER());
                syncData.food = static_cast<int>(hungerAttr.mCurrentValue);

                // 获取饱和度
                auto saturationAttr = player->getAttribute(Player::SATURATION());
                syncData.foodSaturation = static_cast<float>(saturationAttr.mCurrentValue);  // 修复：改为float类型

                // 获取经验
                try {
                    auto xpAttr = player->getAttribute(Player::EXPERIENCE());
                    syncData.expLevel = static_cast<int>(xpAttr.mCurrentMaxValue);
                    float xpProgress = xpAttr.mCurrentValue;
                    syncData.expPoints = static_cast<int>(xpProgress * 100);
                } catch (const std::exception& e) {
                    syncData.expLevel = 0;
                    syncData.expPoints = 0;
                }

                // 保存属性数据
                Database::getInstance().savePlayerSyncData(syncData);

                // 保存背包数据
                std::vector<PlayerInventoryItem> inventory;

                // 背包槽位（0-35）
                int inventorySlots = std::min(containerSize, 36);
                for (int i = 0; i < inventorySlots; i++) {
                    auto& itemStack = playerInv.getItem(i);
                    if (!itemStack.isNull()) {
                        PlayerInventoryItem item;
                        item.slot = i;
                        if (itemStack.mItem) {
                            item.itemType = itemStack.mItem->getSerializedName();
                        }
                        item.count = static_cast<int>(itemStack.mCount);
                        item.damage = itemStack.mAuxValue;

                        if (itemStack.mUserData) {
                            try {
                                item.nbt = itemStack.mUserData->toSnbt();
                            } catch (...) {}
                        }

                        inventory.push_back(item);
                    }
                }

                // 装备栏（36-39）
                auto* headItem = ActorInventoryUtils::getItem(*player, SharedTypes::Legacy::EquipmentSlot::Head, 0);
                auto* torsoItem = ActorInventoryUtils::getItem(*player, SharedTypes::Legacy::EquipmentSlot::Torso, 0);
                auto* legsItem = ActorInventoryUtils::getItem(*player, SharedTypes::Legacy::EquipmentSlot::Legs, 0);
                auto* feetItem = ActorInventoryUtils::getItem(*player, SharedTypes::Legacy::EquipmentSlot::Feet, 0);
                
                if (headItem && !headItem->isNull()) {
                    PlayerInventoryItem item;
                    item.slot = 36;
                    if (headItem->mItem) {
                        item.itemType = headItem->mItem->getSerializedName();
                    }
                    item.count = static_cast<int>(headItem->mCount);
                    item.damage = headItem->mAuxValue;

                    if (headItem->mUserData) {
                        try {
                            item.nbt = headItem->mUserData->toSnbt();
                        } catch (...) {}
                    }

                    inventory.push_back(item);
                }
                
                if (torsoItem && !torsoItem->isNull()) {
                    PlayerInventoryItem item;
                    item.slot = 37;
                    if (torsoItem->mItem) {
                        item.itemType = torsoItem->mItem->getSerializedName();
                    }
                    item.count = static_cast<int>(torsoItem->mCount);
                    item.damage = torsoItem->mAuxValue;

                    if (torsoItem->mUserData) {
                        try {
                            item.nbt = torsoItem->mUserData->toSnbt();
                        } catch (...) {}
                    }

                    inventory.push_back(item);
                }
                
                if (legsItem && !legsItem->isNull()) {
                    PlayerInventoryItem item;
                    item.slot = 38;
                    if (legsItem->mItem) {
                        item.itemType = legsItem->mItem->getSerializedName();
                    }
                    item.count = static_cast<int>(legsItem->mCount);
                    item.damage = legsItem->mAuxValue;

                    if (legsItem->mUserData) {
                        try {
                            item.nbt = legsItem->mUserData->toSnbt();
                        } catch (...) {}
                    }

                    inventory.push_back(item);
                }
                
                if (feetItem && !feetItem->isNull()) {
                    PlayerInventoryItem item;
                    item.slot = 39;
                    if (feetItem->mItem) {
                        item.itemType = feetItem->mItem->getSerializedName();
                    }
                    item.count = static_cast<int>(feetItem->mCount);
                    item.damage = feetItem->mAuxValue;

                    if (feetItem->mUserData) {
                        try {
                            item.nbt = feetItem->mUserData->toSnbt();
                        } catch (...) {}
                    }

                    inventory.push_back(item);
                }

                // 副手（40）
                auto* offhandItem = ActorInventoryUtils::getItem(*player, SharedTypes::Legacy::EquipmentSlot::Offhand, 0);
                if (offhandItem && !offhandItem->isNull()) {
                    PlayerInventoryItem item;
                    item.slot = 40;
                    if (offhandItem->mItem) {
                        item.itemType = offhandItem->mItem->getSerializedName();
                    }
                    item.count = static_cast<int>(offhandItem->mCount);
                    item.damage = offhandItem->mAuxValue;

                    if (offhandItem->mUserData) {
                        try {
                            item.nbt = offhandItem->mUserData->toSnbt();
                        } catch (...) {}
                    }

                    inventory.push_back(item);
                }

                Database::getInstance().savePlayerInventory(uuid, Config::getInstance().getDatabaseConfig().serverName, inventory);
                mod.getSelf().getLogger().info("\033[32m[传送] 已保存玩家 {} 的数据\033[0m", name);
            } catch (const std::exception& e) {
                mod.getSelf().getLogger().error("\033[31m[传送] 保存玩家数据失败: {}\033[0m", e.what());
                output.error("\033[31m传送失败：保存数据时出错\033[0m");
                return;
            }

            // 发送传送数据包
            try {
                TransferPacket packet(targetServer.address, targetServer.port);
                player->sendNetworkPacket(packet);
                mod.getSelf().getLogger().info("\033[32m[传送] 传送数据包已发送\033[0m");
            } catch (const std::exception& e) {
                mod.getSelf().getLogger().error("\033[31m[传送] 发送传送数据包失败: {}\033[0m", e.what());
                output.error("\033[31m传送失败：{}\033[0m", e.what());
                return;
            }

            output.success("\033[32m正在传送到服务器：{} ({}:{})\033[0m", targetServer.name, targetServer.address, targetServer.port);
        });
}

void MyMod::showServerListForm(Player& player) {
    auto& servers = ServerConfigManager::getInstance().getServers();

    if (servers.empty()) {
        player.sendMessage("§c没有可用的服务器，请联系管理员配置服务器列表");
        return;
    }

    // 创建服务器选择表单
    ll::form::SimpleForm form("§l§6§n服务器传送", "§d§l请选择要传送的服务器：\n\n");

    // 添加服务器按钮
    for (const auto& server : servers) {
        std::string buttonText = "§c§l" + server.name;
        form.appendButton(buttonText);
    }

    // 添加关闭按钮
    form.appendButton("§4§l关闭");

    // 发送表单给玩家
    form.sendTo(player, [&servers](Player& p, int index, ll::form::FormCancelReason reason) {
        // 如果有 reason，说明玩家关闭了表单或表单被取消
        if (reason.has_value()) {
            return;
        }

        if (index < 0 || static_cast<size_t>(index) >= servers.size()) {
            // 点击了关闭按钮或无效索引
            return;
        }

        const auto& targetServer = servers[index];
        std::string uuid = p.getUuid().asString();
        std::string name = p.getRealName();

        auto& mod = MyMod::getInstance();
        mod.getSelf().getLogger().info("\033[33m[传送] 玩家 {} ({}) 通过 UI 请求传送到服务器: {}\033[0m", name, uuid, targetServer.name);

        // 保存玩家数据
        try {
            auto& playerInv = p.getInventory();
            int containerSize = playerInv.getContainerSize();

            PlayerSyncData syncData;
            syncData.uuid = uuid;
            syncData.serverName = Config::getInstance().getDatabaseConfig().serverName;
            syncData.gamemode = static_cast<int>(p.getPlayerGameType());

            auto healthAttr = p.getAttribute(SharedAttributes::HEALTH());
            syncData.health = static_cast<int>(healthAttr.mCurrentValue);
            syncData.maxHealth = static_cast<int>(healthAttr.mCurrentMaxValue);

            auto hungerAttr = p.getAttribute(Player::HUNGER());
            syncData.food = static_cast<int>(hungerAttr.mCurrentValue);

            auto saturationAttr = p.getAttribute(Player::SATURATION());
            syncData.foodSaturation = static_cast<float>(saturationAttr.mCurrentValue);  // 修复：改为float类型

            try {
                auto xpAttr = p.getAttribute(Player::EXPERIENCE());
                syncData.expLevel = static_cast<int>(xpAttr.mCurrentMaxValue);
                float xpProgress = xpAttr.mCurrentValue;
                syncData.expPoints = static_cast<int>(xpProgress * 100);
            } catch (...) {
                syncData.expLevel = 0;
                syncData.expPoints = 0;
            }

            Database::getInstance().savePlayerSyncData(syncData);

            std::vector<PlayerInventoryItem> inventory;

            int inventorySlots = std::min(containerSize, 36);
            for (int j = 0; j < inventorySlots; j++) {
                auto& itemStack = playerInv.getItem(j);
                if (!itemStack.isNull()) {
                    PlayerInventoryItem item;
                    item.slot = j;
                    if (itemStack.mItem) {
                        item.itemType = itemStack.mItem->getSerializedName();
                    }
                    item.count = static_cast<int>(itemStack.mCount);
                    item.damage = itemStack.mAuxValue;

                    if (itemStack.mUserData) {
                        try {
                            item.nbt = itemStack.mUserData->toSnbt();
                        } catch (...) {}
                    }

                    inventory.push_back(item);
                }
            }

            auto* headItem = ActorInventoryUtils::getItem(p, SharedTypes::Legacy::EquipmentSlot::Head, 0);
            auto* torsoItem = ActorInventoryUtils::getItem(p, SharedTypes::Legacy::EquipmentSlot::Torso, 0);
            auto* legsItem = ActorInventoryUtils::getItem(p, SharedTypes::Legacy::EquipmentSlot::Legs, 0);
            auto* feetItem = ActorInventoryUtils::getItem(p, SharedTypes::Legacy::EquipmentSlot::Feet, 0);
            
            if (headItem && !headItem->isNull()) {
                PlayerInventoryItem item;
                item.slot = 36;
                if (headItem->mItem) {
                    item.itemType = headItem->mItem->getSerializedName();
                }
                item.count = static_cast<int>(headItem->mCount);
                item.damage = headItem->mAuxValue;

                if (headItem->mUserData) {
                    try {
                        item.nbt = headItem->mUserData->toSnbt();
                    } catch (...) {}
                }

                inventory.push_back(item);
            }
            
            if (torsoItem && !torsoItem->isNull()) {
                PlayerInventoryItem item;
                item.slot = 37;
                if (torsoItem->mItem) {
                    item.itemType = torsoItem->mItem->getSerializedName();
                }
                item.count = static_cast<int>(torsoItem->mCount);
                item.damage = torsoItem->mAuxValue;

                if (torsoItem->mUserData) {
                    try {
                        item.nbt = torsoItem->mUserData->toSnbt();
                    } catch (...) {}
                }

                inventory.push_back(item);
            }
            
            if (legsItem && !legsItem->isNull()) {
                PlayerInventoryItem item;
                item.slot = 38;
                if (legsItem->mItem) {
                    item.itemType = legsItem->mItem->getSerializedName();
                }
                item.count = static_cast<int>(legsItem->mCount);
                item.damage = legsItem->mAuxValue;

                if (legsItem->mUserData) {
                    try {
                        item.nbt = legsItem->mUserData->toSnbt();
                    } catch (...) {}
                }

                inventory.push_back(item);
            }
            
            if (feetItem && !feetItem->isNull()) {
                PlayerInventoryItem item;
                item.slot = 39;
                if (feetItem->mItem) {
                    item.itemType = feetItem->mItem->getSerializedName();
                }
                item.count = static_cast<int>(feetItem->mCount);
                item.damage = feetItem->mAuxValue;

                if (feetItem->mUserData) {
                    try {
                        item.nbt = feetItem->mUserData->toSnbt();
                    } catch (...) {}
                }

                inventory.push_back(item);
            }

            auto* offhandItem = ActorInventoryUtils::getItem(p, SharedTypes::Legacy::EquipmentSlot::Offhand, 0);
            if (offhandItem && !offhandItem->isNull()) {
                PlayerInventoryItem item;
                item.slot = 40;
                if (offhandItem->mItem) {
                    item.itemType = offhandItem->mItem->getSerializedName();
                }
                item.count = static_cast<int>(offhandItem->mCount);
                item.damage = offhandItem->mAuxValue;

                if (offhandItem->mUserData) {
                    try {
                        item.nbt = offhandItem->mUserData->toSnbt();
                    } catch (...) {}
                }

                inventory.push_back(item);
            }

            Database::getInstance().savePlayerInventory(uuid, Config::getInstance().getDatabaseConfig().serverName, inventory);
            mod.getSelf().getLogger().info("\033[32m[传送] 已保存玩家 {} 的数据\033[0m", name);
        } catch (const std::exception& e) {
            mod.getSelf().getLogger().error("\033[31m[传送] 保存玩家数据失败: {}\033[0m", e.what());
            p.sendMessage("§c传送失败：保存数据时出错");
            return;
        }

        // 发送传送数据包
        try {
            TransferPacket packet(targetServer.address, targetServer.port);
            p.sendNetworkPacket(packet);
            mod.getSelf().getLogger().info("\033[32m[传送] 传送数据包已发送\033[0m");
        } catch (const std::exception& e) {
            mod.getSelf().getLogger().error("\033[31m[传送] 发送传送数据包失败: {}\033[0m", e.what());
            p.sendMessage("§c传送失败：" + std::string(e.what()));
            return;
        }

        p.sendMessage("§a正在传送到服务器：§b" + targetServer.name + "§r §7(" + targetServer.address + ":" + std::to_string(targetServer.port) + ")");
    });
}

void MyMod::setPlayerAttributesDelayed(Player& player, const PlayerSyncData& syncData) {
    std::string playerName = player.getRealName();
    getSelf().getLogger().info("\033[33m[数据同步] [{}] 开始设置玩家属性\033[0m", playerName);
    getSelf().getLogger().info("\033[33m[数据同步] [{}] 目标值 - 生命值: {}/{}, 饱食度: {}, 饱和度: {}, 经验等级: {}, 经验点数: {}\033[0m", 
        playerName, syncData.health, syncData.maxHealth, syncData.food, syncData.foodSaturation, 
        syncData.expLevel, syncData.expPoints);
    
    // 获取属性并记录当前值
    auto healthAttr = player.getAttribute(SharedAttributes::HEALTH());
    auto hungerAttr = player.getAttribute(Player::HUNGER());
    auto saturationAttr = player.getAttribute(Player::SATURATION());
    auto xpAttr = player.getAttribute(Player::EXPERIENCE());
    
    getSelf().getLogger().info("\033[33m[数据同步] [{}] 设置前 - 生命值: {:.1f}/{:.1f}, 饱食度: {:.1f}, 饱和度: {:.1f}, 经验等级: {:.0f}, 经验进度: {:.1f}%\033[0m",
        playerName, 
        healthAttr.mCurrentValue, healthAttr.mCurrentMaxValue,
        hungerAttr.mCurrentValue,
        saturationAttr.mCurrentValue,
        xpAttr.mCurrentMaxValue,
        xpAttr.mCurrentValue * 100.0f);
    
    // 设置生命值
    healthAttr.mCurrentMaxValue = static_cast<float>(syncData.maxHealth);
    healthAttr.mCurrentValue = static_cast<float>(syncData.health);
    
    // 设置饱食度
    hungerAttr.mCurrentValue = static_cast<float>(syncData.food);
    
    // 设置饱和度
    saturationAttr.mCurrentValue = syncData.foodSaturation;
    
    // 设置经验等级和进度
    xpAttr.mCurrentMaxValue = static_cast<float>(syncData.expLevel);
    xpAttr.mCurrentValue = static_cast<float>(syncData.expPoints) / 100.0f;
    
    getSelf().getLogger().info("\033[33m[数据同步] [{}] 设置后 - 生命值: {:.1f}/{:.1f}, 饱食度: {:.1f}, 饱和度: {:.1f}, 经验等级: {:.0f}, 经验进度: {:.1f}%\033[0m",
        playerName, 
        healthAttr.mCurrentValue, healthAttr.mCurrentMaxValue,
        hungerAttr.mCurrentValue,
        saturationAttr.mCurrentValue,
        xpAttr.mCurrentMaxValue,
        xpAttr.mCurrentValue * 100.0f);
    
    // 延迟一小段时间，确保属性值已设置
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // 验证属性值是否保持
    getSelf().getLogger().info("\033[33m[数据同步] [{}] 验证 - 生命值: {:.1f}/{:.1f}, 饱食度: {:.1f}, 饱和度: {:.1f}, 经验等级: {:.0f}, 经验进度: {:.1f}%\033[0m",
        playerName, 
        healthAttr.mCurrentValue, healthAttr.mCurrentMaxValue,
        hungerAttr.mCurrentValue,
        saturationAttr.mCurrentValue,
        xpAttr.mCurrentMaxValue,
        xpAttr.mCurrentValue * 100.0f);
    
    // 尝试同步到客户端
    try {
        auto& serverPlayer = static_cast<ServerPlayer&>(player);
        serverPlayer.sendInventory(false);
        getSelf().getLogger().info("\033[32m[数据同步] [{}] 已调用 sendInventory() 同步到客户端\033[0m", playerName);
    } catch (const std::exception& e) {
        getSelf().getLogger().warn("\033[33m[数据同步] [{}] sendInventory() 调用失败: {}\033[0m", playerName, e.what());
    }
    
    getSelf().getLogger().info("\033[32m[数据同步] [{}] 属性设置和同步完成\033[0m", playerName);
}

} // namespace bdsmysql

LL_REGISTER_MOD(bdsmysql::MyMod, bdsmysql::MyMod::getInstance());
