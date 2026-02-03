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
#include "mod/ServerConfig.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

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

    // 先识别玩家是否有数据库数据
    bool hasSyncData = false;
    PlayerSyncData syncData;
    if (Database::getInstance().isPlayerExists(uuid) && Database::getInstance().loadPlayerSyncData(uuid, serverName, syncData)) {
        hasSyncData = true;
        getSelf().getLogger().info("\033[33m[数据互通] 玩家 {} 有数据库数据，将加载装备\033[0m", name);
    } else {
        getSelf().getLogger().info("\033[33m[数据互通] 玩家 {} 没有数据库数据，将保存当前装备\033[0m", name);
    }

    if (hasSyncData) {
        // 玩家有数据：先清除装备和背包，然后加载数据库中的装备和背包
        ItemStack emptyStack;
        auto& playerInv = player.getInventory();
        
        // 清空背包槽位（0-35）
        for (int i = 0; i < 36; i++) {
            playerInv.setItem(i, emptyStack);
        }
        
        // 清空装备槽位（36-39）
        serverPlayer.setArmor(SharedTypes::Legacy::ArmorSlot::Head, emptyStack);
        serverPlayer.setArmor(SharedTypes::Legacy::ArmorSlot::Torso, emptyStack);
        serverPlayer.setArmor(SharedTypes::Legacy::ArmorSlot::Legs, emptyStack);
        serverPlayer.setArmor(SharedTypes::Legacy::ArmorSlot::Feet, emptyStack);
        
        // 清空副手槽位（40）
        serverPlayer.setOffhandSlot(emptyStack);
        
        // 同步清空后的状态到客户端
        serverPlayer.sendInventory(false);
        serverPlayer.sendArmor(std::bitset<5>(0b1111));
        
        getSelf().getLogger().info("\033[33m[数据互通] 已清空玩家 {} 的所有槽位（0-40）\033[0m", name);
        
        // 加载背包和装备数据
        std::vector<PlayerInventoryItem> inventory;
        if (Database::getInstance().loadPlayerInventory(uuid, serverName, inventory)) {
            getSelf().getLogger().info("\033[33m[数据互通] 已加载 {} 个物品，正在应用...\033[0m", inventory.size());

            int inventoryCount = 0;
            int armorCount = 0;
            std::bitset<5> armorSlotsToSync;

            for (const auto& item : inventory) {
                ItemStack stack(item.itemType, item.count, item.damage);

                // 应用 NBT 数据（包括附魔）
                if (!item.nbt.empty()) {
                    try {
                        auto nbtResult = CompoundTag::fromSnbt(item.nbt);
                        if (nbtResult) {
                            stack.mUserData = std::make_unique<CompoundTag>(std::move(*nbtResult));
                            getSelf().getLogger().info("\033[33m[数据互通] 已应用物品 {} (槽位 {}) 的 NBT 数据\033[0m", item.itemType, item.slot);
                        }
                    } catch (const std::exception& e) {
                        getSelf().getLogger().warn("\033[33m[数据互通] 应用物品 {} (槽位 {}) 的 NBT 数据失败: {}\033[0m", item.itemType, item.slot, e.what());
                    }
                }

                // 根据槽位类型设置物品
                if (item.slot >= 0 && item.slot < 36) {
                    // 背包槽位（0-35）
                    playerInv.setItem(item.slot, stack);
                    inventoryCount++;
                } else if (item.slot == 36) {
                    // 头盔
                    serverPlayer.setArmor(SharedTypes::Legacy::ArmorSlot::Head, stack);
                    armorSlotsToSync.set(0);
                    armorCount++;
                    getSelf().getLogger().info("\033[33m[数据互通] 已设置头盔: {}\033[0m", item.itemType);
                } else if (item.slot == 37) {
                    // 胸甲
                    serverPlayer.setArmor(SharedTypes::Legacy::ArmorSlot::Torso, stack);
                    armorSlotsToSync.set(1);
                    armorCount++;
                    getSelf().getLogger().info("\033[33m[数据互通] 已设置胸甲: {}\033[0m", item.itemType);
                } else if (item.slot == 38) {
                    // 护腿
                    serverPlayer.setArmor(SharedTypes::Legacy::ArmorSlot::Legs, stack);
                    armorSlotsToSync.set(2);
                    armorCount++;
                    getSelf().getLogger().info("\033[33m[数据互通] 已设置护腿: {}\033[0m", item.itemType);
                } else if (item.slot == 39) {
                    // 靴子
                    serverPlayer.setArmor(SharedTypes::Legacy::ArmorSlot::Feet, stack);
                    armorSlotsToSync.set(3);
                    armorCount++;
                    getSelf().getLogger().info("\033[33m[数据互通] 已设置靴子: {}\033[0m", item.itemType);
                } else if (item.slot == 40) {
                    // 副手
                    serverPlayer.setOffhandSlot(stack);
                    getSelf().getLogger().info("\033[33m[数据互通] 已设置副手: {}\033[0m", item.itemType);
                }
            }
            
            // 同步装备到客户端
            serverPlayer.sendInventory(false);
            if (armorSlotsToSync.any()) {
                serverPlayer.sendArmor(armorSlotsToSync);
            }
            
            getSelf().getLogger().info("\033[32m[数据互通] 已应用 {} 个背包物品，{} 个装备\033[0m", inventoryCount, armorCount);
        }
    } else {
        // 玩家没有数据：保存当前装备
        getSelf().getLogger().info("\033[33m[数据互通] 正在保存玩家 {} 的当前装备...\033[0m", name);
        
        // 收集玩家当前装备
        std::vector<PlayerInventoryItem> inventory;
        
        // 使用 ActorInventoryUtils 获取装备数据
        auto* headItem = ActorInventoryUtils::getItem(player, SharedTypes::Legacy::EquipmentSlot::Head, 0);
        auto* torsoItem = ActorInventoryUtils::getItem(player, SharedTypes::Legacy::EquipmentSlot::Torso, 0);
        auto* legsItem = ActorInventoryUtils::getItem(player, SharedTypes::Legacy::EquipmentSlot::Legs, 0);
        auto* feetItem = ActorInventoryUtils::getItem(player, SharedTypes::Legacy::EquipmentSlot::Feet, 0);
        auto* offhandItem = ActorInventoryUtils::getItem(player, SharedTypes::Legacy::EquipmentSlot::Offhand, 0);
        
        // 保存头盔（槽位 36）
        if (headItem && !headItem->isNull()) {
            PlayerInventoryItem item;
            item.slot = 36;
            if (headItem->mItem) {
                item.itemType = headItem->mItem->getSerializedName();
                getSelf().getLogger().info("\033[33m[数据互通] 保存装备: {} 在槽位 {}\033[0m", item.itemType, 36);
            }
            item.count = static_cast<int>(headItem->mCount);
            item.damage = headItem->mAuxValue;
            if (headItem->mUserData) {
                try {
                    item.nbt = headItem->mUserData->toSnbt();
                } catch (const std::exception& e) {
                    getSelf().getLogger().warn("\033[33m[数据互通] 序列化装备 NBT 数据失败: {}\033[0m", e.what());
                }
            }
            inventory.push_back(item);
        }
        
        // 保存胸甲（槽位 37）
        if (torsoItem && !torsoItem->isNull()) {
            PlayerInventoryItem item;
            item.slot = 37;
            if (torsoItem->mItem) {
                item.itemType = torsoItem->mItem->getSerializedName();
                getSelf().getLogger().info("\033[33m[数据互通] 保存装备: {} 在槽位 {}\033[0m", item.itemType, 37);
            }
            item.count = static_cast<int>(torsoItem->mCount);
            item.damage = torsoItem->mAuxValue;
            if (torsoItem->mUserData) {
                try {
                    item.nbt = torsoItem->mUserData->toSnbt();
                } catch (const std::exception& e) {
                    getSelf().getLogger().warn("\033[33m[数据互通] 序列化装备 NBT 数据失败: {}\033[0m", e.what());
                }
            }
            inventory.push_back(item);
        }
        
        // 保存护腿（槽位 38）
        if (legsItem && !legsItem->isNull()) {
            PlayerInventoryItem item;
            item.slot = 38;
            if (legsItem->mItem) {
                item.itemType = legsItem->mItem->getSerializedName();
                getSelf().getLogger().info("\033[33m[数据互通] 保存装备: {} 在槽位 {}\033[0m", item.itemType, 38);
            }
            item.count = static_cast<int>(legsItem->mCount);
            item.damage = legsItem->mAuxValue;
            if (legsItem->mUserData) {
                try {
                    item.nbt = legsItem->mUserData->toSnbt();
                } catch (const std::exception& e) {
                    getSelf().getLogger().warn("\033[33m[数据互通] 序列化装备 NBT 数据失败: {}\033[0m", e.what());
                }
            }
            inventory.push_back(item);
        }
        
        // 保存靴子（槽位 39）
        if (feetItem && !feetItem->isNull()) {
            PlayerInventoryItem item;
            item.slot = 39;
            if (feetItem->mItem) {
                item.itemType = feetItem->mItem->getSerializedName();
                getSelf().getLogger().info("\033[33m[数据互通] 保存装备: {} 在槽位 {}\033[0m", item.itemType, 39);
            }
            item.count = static_cast<int>(feetItem->mCount);
            item.damage = feetItem->mAuxValue;
            if (feetItem->mUserData) {
                try {
                    item.nbt = feetItem->mUserData->toSnbt();
                } catch (const std::exception& e) {
                    getSelf().getLogger().warn("\033[33m[数据互通] 序列化装备 NBT 数据失败: {}\033[0m", e.what());
                }
            }
            inventory.push_back(item);
        }
        
        // 保存副手（槽位 40）
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
                } catch (const std::exception& e) {
                    getSelf().getLogger().warn("\033[33m[数据互通] 序列化副手 NBT 数据失败: {}\033[0m", e.what());
                }
            }
            inventory.push_back(item);
        }
        
        // 保存装备数据到数据库
        Database::getInstance().savePlayerInventory(uuid, serverName, inventory);
        getSelf().getLogger().info("\033[32m[数据互通] 已保存玩家 {} 的 {} 个装备\033[0m", name, inventory.size());
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

    // 保存玩家当前状态到数据库
    getSelf().getLogger().info("\033[33m[数据互通] 正在保存玩家 {} 的当前状态...\033[0m", name);

    std::string serverName = Config::getInstance().getDatabaseConfig().serverName;

    // 收集玩家当前状态
    PlayerSyncData syncData;
    syncData.uuid = uuid;
    syncData.serverName = serverName;

    // 获取玩家游戏模式
    syncData.gamemode = static_cast<int>(player.getPlayerGameType());

    // 获取玩家生命值
    try {
        auto healthAttr = player.getAttribute(SharedAttributes::HEALTH());
        syncData.health = static_cast<int>(healthAttr.mCurrentValue);
        syncData.maxHealth = static_cast<int>(healthAttr.mCurrentMaxValue);
        getSelf().getLogger().info("\033[33m[数据互通] 生命值: {}/{}\033[0m", syncData.health, syncData.maxHealth);
    } catch (const std::exception& e) {
        getSelf().getLogger().warn("\033[33m[数据互通] 获取生命值失败: {}\033[0m", e.what());
        syncData.health = 20;
        syncData.maxHealth = 20;
    }

    // 获取玩家饥饿值
    try {
        auto hungerAttr = player.getAttribute(Player::HUNGER());
        syncData.food = static_cast<int>(hungerAttr.mCurrentValue);
        getSelf().getLogger().info("\033[33m[数据互通] 饥饿值: {}\033[0m", syncData.food);
    } catch (const std::exception& e) {
        getSelf().getLogger().warn("\033[33m[数据互通] 获取饥饿值失败: {}\033[0m", e.what());
        syncData.food = 20;
    }

    // 获取玩家饱和度
    try {
        auto saturationAttr = player.getAttribute(Player::SATURATION());
        syncData.foodSaturation = static_cast<float>(saturationAttr.mCurrentValue);
        getSelf().getLogger().info("\033[33m[数据互通] 饱和度: {}\033[0m", syncData.foodSaturation);
    } catch (const std::exception& e) {
        getSelf().getLogger().warn("\033[33m[数据互通] 获取饱和度失败: {}\033[0m", e.what());
        syncData.foodSaturation = 0.0f;
    }

    // 获取玩家经验值和等级
    syncData.expLevel = 0;  // 暂时设为0，因为 Level 类型无法直接转换
    try {
        auto xpAttr = player.getAttribute(Player::EXPERIENCE());
        float xpProgress = xpAttr.mCurrentValue;
        syncData.expPoints = static_cast<int>(xpProgress * 100);
    } catch (const std::exception& e) {
        syncData.expPoints = 0;
    }
    getSelf().getLogger().info("\033[33m[数据互通] 经验: 等级 {}, 经验点数 {}\033[0m", syncData.expLevel, syncData.expPoints);

    // 保存属性数据
    if (Database::getInstance().savePlayerSyncData(syncData)) {
        getSelf().getLogger().info("\033[32m[数据互通] 已保存玩家 {} 的属性数据\033[0m", name);
    } else {
        getSelf().getLogger().error("\033[31m[数据互通] 保存玩家 {} 的属性数据失败\033[0m", name);
    }

    // 保存背包和装备数据
    std::vector<PlayerInventoryItem> inventory;
    try {
        auto& playerInv = player.getInventory();
        int containerSize = playerInv.getContainerSize();
        getSelf().getLogger().info("\033[33m[数据互通] 背包大小: {}\033[0m", containerSize);

        // 遍历背包槽位（0-35：快捷栏和背包主栏）
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

                // 序列化 NBT 数据（包括附魔）
                if (itemStack.mUserData) {
                    try {
                        item.nbt = itemStack.mUserData->toSnbt();
                        getSelf().getLogger().info("\033[33m[数据互通] 物品 {} (槽位 {}) 已序列化 NBT 数据\033[0m", item.itemType, i);
                    } catch (const std::exception& e) {
                        getSelf().getLogger().warn("\033[33m[数据互通] 序列化物品 {} (槽位 {}) 的 NBT 数据失败: {}\033[0m", item.itemType, i, e.what());
                    }
                }

                inventory.push_back(item);
            }
        }

        // 保存装备栏数据（36-39：头盔、胸甲、护腿、靴子）
        getSelf().getLogger().info("\033[33m[数据互通] 正在保存装备栏数据...\033[0m");
        
        // 使用 ActorInventoryUtils 获取装备数据
        auto* headItem = ActorInventoryUtils::getItem(player, SharedTypes::Legacy::EquipmentSlot::Head, 0);
        auto* torsoItem = ActorInventoryUtils::getItem(player, SharedTypes::Legacy::EquipmentSlot::Torso, 0);
        auto* legsItem = ActorInventoryUtils::getItem(player, SharedTypes::Legacy::EquipmentSlot::Legs, 0);
        auto* feetItem = ActorInventoryUtils::getItem(player, SharedTypes::Legacy::EquipmentSlot::Feet, 0);
        
        // 保存头盔（槽位 36）
        if (headItem && !headItem->isNull()) {
            PlayerInventoryItem item;
            item.slot = 36;
            if (headItem->mItem) {
                item.itemType = headItem->mItem->getSerializedName();
                getSelf().getLogger().info("\033[33m[数据互通] 找到装备: {} 在槽位 {}\033[0m", item.itemType, 36);
            }
            item.count = static_cast<int>(headItem->mCount);
            item.damage = headItem->mAuxValue;

            // 序列化 NBT 数据（包括附魔）
            if (headItem->mUserData) {
                try {
                    item.nbt = headItem->mUserData->toSnbt();
                    getSelf().getLogger().info("\033[33m[数据互通] 装备 {} (槽位 36) 已序列化 NBT 数据\033[0m", item.itemType);
                } catch (const std::exception& e) {
                    getSelf().getLogger().warn("\033[33m[数据互通] 序列化装备 {} (槽位 36) 的 NBT 数据失败: {}\033[0m", item.itemType, e.what());
                }
            }

            inventory.push_back(item);
        }
        
        // 保存胸甲（槽位 37）
        if (torsoItem && !torsoItem->isNull()) {
            PlayerInventoryItem item;
            item.slot = 37;
            if (torsoItem->mItem) {
                item.itemType = torsoItem->mItem->getSerializedName();
                getSelf().getLogger().info("\033[33m[数据互通] 找到装备: {} 在槽位 {}\033[0m", item.itemType, 37);
            }
            item.count = static_cast<int>(torsoItem->mCount);
            item.damage = torsoItem->mAuxValue;

            // 序列化 NBT 数据（包括附魔）
            if (torsoItem->mUserData) {
                try {
                    item.nbt = torsoItem->mUserData->toSnbt();
                    getSelf().getLogger().info("\033[33m[数据互通] 装备 {} (槽位 37) 已序列化 NBT 数据\033[0m", item.itemType);
                } catch (const std::exception& e) {
                    getSelf().getLogger().warn("\033[33m[数据互通] 序列化装备 {} (槽位 37) 的 NBT 数据失败: {}\033[0m", item.itemType, e.what());
                }
            }

            inventory.push_back(item);
        }
        
        // 保存护腿（槽位 38）
        if (legsItem && !legsItem->isNull()) {
            PlayerInventoryItem item;
            item.slot = 38;
            if (legsItem->mItem) {
                item.itemType = legsItem->mItem->getSerializedName();
                getSelf().getLogger().info("\033[33m[数据互通] 找到装备: {} 在槽位 {}\033[0m", item.itemType, 38);
            }
            item.count = static_cast<int>(legsItem->mCount);
            item.damage = legsItem->mAuxValue;

            // 序列化 NBT 数据（包括附魔）
            if (legsItem->mUserData) {
                try {
                    item.nbt = legsItem->mUserData->toSnbt();
                    getSelf().getLogger().info("\033[33m[数据互通] 装备 {} (槽位 38) 已序列化 NBT 数据\033[0m", item.itemType);
                } catch (const std::exception& e) {
                    getSelf().getLogger().warn("\033[33m[数据互通] 序列化装备 {} (槽位 38) 的 NBT 数据失败: {}\033[0m", item.itemType, e.what());
                }
            }

            inventory.push_back(item);
        }
        
        // 保存靴子（槽位 39）
        if (feetItem && !feetItem->isNull()) {
            PlayerInventoryItem item;
            item.slot = 39;
            if (feetItem->mItem) {
                item.itemType = feetItem->mItem->getSerializedName();
                getSelf().getLogger().info("\033[33m[数据互通] 找到装备: {} 在槽位 {}\033[0m", item.itemType, 39);
            }
            item.count = static_cast<int>(feetItem->mCount);
            item.damage = feetItem->mAuxValue;

            // 序列化 NBT 数据（包括附魔）
            if (feetItem->mUserData) {
                try {
                    item.nbt = feetItem->mUserData->toSnbt();
                    getSelf().getLogger().info("\033[33m[数据互通] 装备 {} (槽位 39) 已序列化 NBT 数据\033[0m", item.itemType);
                } catch (const std::exception& e) {
                    getSelf().getLogger().warn("\033[33m[数据互通] 序列化装备 {} (槽位 39) 的 NBT 数据失败: {}\033[0m", item.itemType, e.what());
                }
            }

            inventory.push_back(item);
        }

        // 保存副手槽位（40）
        auto* offhandItem = ActorInventoryUtils::getItem(player, SharedTypes::Legacy::EquipmentSlot::Offhand, 0);
        if (offhandItem && !offhandItem->isNull()) {
            PlayerInventoryItem item;
            item.slot = 40;
            if (offhandItem->mItem) {
                item.itemType = offhandItem->mItem->getSerializedName();
            }
            item.count = static_cast<int>(offhandItem->mCount);
            item.damage = offhandItem->mAuxValue;

            // 序列化 NBT 数据（包括附魔）
            if (offhandItem->mUserData) {
                try {
                    item.nbt = offhandItem->mUserData->toSnbt();
                    getSelf().getLogger().info("\033[33m[数据互通] 副手物品 (槽位 40) 已序列化 NBT 数据\033[0m");
                } catch (const std::exception& e) {
                    getSelf().getLogger().warn("\033[33m[数据互通] 序列化副手物品的 NBT 数据失败: {}\033[0m", e.what());
                }
            }

            inventory.push_back(item);
        }

        getSelf().getLogger().info("\033[33m[数据互通] 收集到 {} 个背包物品（包括装备）\033[0m", inventory.size());

        // 保存背包数据到数据库
        Database::getInstance().savePlayerInventory(uuid, serverName, inventory);
        getSelf().getLogger().info("\033[32m[数据互通] 已保存玩家 {} 的背包数据\033[0m", name);
    } catch (const std::exception& e) {
        getSelf().getLogger().warn("\033[33m[数据互通] 保存背包数据失败: {}\033[0m", e.what());
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
                syncData.foodSaturation = static_cast<int>(saturationAttr.mCurrentValue);

                // 获取经验
                auto xpAttr = player->getAttribute(Player::EXPERIENCE());
                float xpProgress = xpAttr.mCurrentValue;
                syncData.expPoints = static_cast<int>(xpProgress * 100);

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

} // namespace bdsmysql

LL_REGISTER_MOD(bdsmysql::MyMod, bdsmysql::MyMod::getInstance());
