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