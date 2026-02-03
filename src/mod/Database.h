#pragma once

#include <mysql.h>
#include <string>
#include <memory>
#include "mod/Config.h"

namespace bdsmysql {

struct PlayerData {
    int         id;
    std::string uuid;
    std::string name;
    std::string xuid;
    std::string joinDate;
    std::string lastSeen;
    int         playTime;
    bool        isOnline;
};

// 玩家同步数据（用于服务器间数据互通）
struct PlayerSyncData {
    int         id;
    std::string uuid;
    std::string serverName;      // 服务器名称
    int         health;          // 生命值
    int         maxHealth;       // 最大生命值
    int         food;            // 饱食度
    int         foodSaturation;  // 饱食等级
    int         expLevel;        // 经验等级
    int         expPoints;       // 经验点数
    int         gamemode;        // 游戏模式 (0=生存, 1=创造, 2=冒险, 3=旁观)
    float       x;               // X坐标
    float       y;               // Y坐标
    float       z;               // Z坐标
    int         dimension;       // 维度 (0=主世界, 1=下界, 2=末地)
    std::string lastSyncTime;    // 最后同步时间
};

// 玩家背包物品数据
struct PlayerInventoryItem {
    int    slot;        // 槽位
    std::string itemType;  // 物品类型
    int    count;       // 数量
    int    damage;      // 损坏值
    std::string nbt;        // NBT数据
};

class Database {
public:
    static Database& getInstance();

    bool connect();
    void disconnect();
    bool isConnected() const { return mConnected; }

    bool initTables();
    bool savePlayerData(const PlayerData& data);
    bool updatePlayerData(const PlayerData& data);
    bool loadPlayerData(const std::string& uuid, PlayerData& data);
    bool isPlayerExists(const std::string& uuid);

    // 数据互通相关功能
    bool savePlayerSyncData(const PlayerSyncData& data);
    bool loadPlayerSyncData(const std::string& uuid, const std::string& serverName, PlayerSyncData& data);
    bool updatePlayerSyncData(const PlayerSyncData& data);
    bool savePlayerInventory(const std::string& uuid, const std::string& serverName, const std::vector<PlayerInventoryItem>& items);
    bool loadPlayerInventory(const std::string& uuid, const std::string& serverName, std::vector<PlayerInventoryItem>& items);

private:
    Database()  = default;
    ~Database() = default;

    Database(const Database&)            = delete;
    Database& operator=(const Database&) = delete;

    MYSQL*                 mConnection = nullptr;
    bool                   mConnected  = false;
    const DatabaseConfig&  mConfig     = Config::getInstance().getDatabaseConfig();
};

} // namespace bdsmysql