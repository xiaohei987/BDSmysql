#include "mod/Database.h"
#include "ll/api/io/Logger.h"
#include "ll/api/mod/NativeMod.h"
#include <sstream>
#include <format>

namespace bdsmysql {

Database& Database::getInstance() {
    static Database instance;
    return instance;
}

bool Database::connect() {
    if (mConnected) {
        return true;
    }

    mConnection = mysql_init(nullptr);
    if (!mConnection) {
        auto mod = ll::mod::NativeMod::current();
        mod->getLogger().error("Failed to initialize MySQL connection");
        return false;
    }

    bool reconnect = true;
    mysql_options(mConnection, MYSQL_OPT_RECONNECT, &reconnect);

    auto mod = ll::mod::NativeMod::current();
    
    // 先尝试连接到 MySQL 服务器（不指定数据库）
    if (!mysql_real_connect(
            mConnection,
            mConfig.host.c_str(),
            mConfig.username.c_str(),
            mConfig.password.c_str(),
            nullptr,
            mConfig.port,
            nullptr,
            0
        )) {
        mod->getLogger().error("\033[31m[数据库] MySQL 服务器连接失败！错误: {}\033[0m", mysql_error(mConnection));
        mysql_close(mConnection);
        mConnection = nullptr;
        return false;
    }

    // 检查数据库是否存在，不存在则创建
    std::string checkQuery = "SELECT SCHEMA_NAME FROM INFORMATION_SCHEMA.SCHEMATA WHERE SCHEMA_NAME = '" + mConfig.database + "'";
    if (mysql_query(mConnection, checkQuery.c_str())) {
        mod->getLogger().error("\033[31m[数据库] 检查数据库失败！错误: {}\033[0m", mysql_error(mConnection));
        mysql_close(mConnection);
        mConnection = nullptr;
        return false;
    }

    MYSQL_RES* result = mysql_store_result(mConnection);
    bool dbExists = (result && mysql_num_rows(result) > 0);
    if (result) mysql_free_result(result);

    if (!dbExists) {
        mod->getLogger().info("\033[33m[数据库] 数据库 '{}' 不存在，正在创建...\033[0m", mConfig.database);
        std::string createQuery = "CREATE DATABASE `" + mConfig.database + "` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci";
        if (mysql_query(mConnection, createQuery.c_str())) {
            mod->getLogger().error("\033[31m[数据库] 创建数据库失败！错误: {}\033[0m", mysql_error(mConnection));
            mysql_close(mConnection);
            mConnection = nullptr;
            return false;
        }
        mod->getLogger().info("\033[32m[数据库] 数据库 '{}' 创建成功！\033[0m", mConfig.database);
    } else {
        mod->getLogger().info("\033[32m[数据库] 数据库 '{}' 已存在\033[0m", mConfig.database);
    }

    // 关闭连接，然后重新连接到指定数据库
    mysql_close(mConnection);
    mConnection = mysql_init(nullptr);
    if (!mConnection) {
        mod->getLogger().error("\033[31m[数据库] 重新初始化 MySQL 连接失败！\033[0m");
        return false;
    }

    if (!mysql_real_connect(
            mConnection,
            mConfig.host.c_str(),
            mConfig.username.c_str(),
            mConfig.password.c_str(),
            mConfig.database.c_str(),
            mConfig.port,
            nullptr,
            CLIENT_MULTI_STATEMENTS
        )) {
        mod->getLogger().error("\033[31m[数据库] 连接数据库失败！错误: {}\033[0m", mysql_error(mConnection));
        mysql_close(mConnection);
        mConnection = nullptr;
        return false;
    }

    if (mysql_set_character_set(mConnection, mConfig.charset.c_str())) {
        mod->getLogger().warn("\033[33m[数据库] 设置字符集失败：{}\033[0m", mysql_error(mConnection));
    }

    mConnected = true;
    mod->getLogger().info("\033[1;32m[数据库] ========== MySQL 数据库连接成功！ ==========\033[0m");
    mod->getLogger().info("\033[1;32m[数据库] 数据库: {} @ {}:{}\033[0m", mConfig.database, mConfig.host, mConfig.port);
    return true;
}

void Database::disconnect() {
    if (mConnection) {
        mysql_close(mConnection);
        mConnection = nullptr;
        mConnected  = false;
        auto mod    = ll::mod::NativeMod::current();
        mod->getLogger().info("\033[33m[数据库] 已断开 MySQL 数据库连接\033[0m");
    }
}

bool Database::initTables() {
    if (!mConnected) {
        return false;
    }

    const char* createTableSQL = R"(
        CREATE TABLE IF NOT EXISTS `player_data` (
            `id` INT AUTO_INCREMENT PRIMARY KEY,
            `uuid` VARCHAR(36) NOT NULL UNIQUE,
            `name` VARCHAR(16) NOT NULL,
            `xuid` VARCHAR(32) DEFAULT NULL,
            `join_date` DATETIME NOT NULL,
            `last_seen` DATETIME NOT NULL,
            `play_time` INT DEFAULT 0,
            `is_online` TINYINT(1) DEFAULT 0,
            INDEX `idx_uuid` (`uuid`),
            INDEX `idx_name` (`name`)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
    )";

    if (mysql_query(mConnection, createTableSQL)) {
        auto mod = ll::mod::NativeMod::current();
        mod->getLogger().error("\033[31m[数据库] 创建 player_data 表失败！错误: {}\033[0m", mysql_error(mConnection));
        return false;
    }

    // 创建玩家同步数据表（共享数据：所有服务器共享同一份数据）
    const char* createSyncTableSQL = R"(
        CREATE TABLE IF NOT EXISTS `player_sync_data` (
            `id` INT AUTO_INCREMENT PRIMARY KEY,
            `uuid` VARCHAR(36) NOT NULL UNIQUE,
            `server_name` VARCHAR(32) DEFAULT NULL,
            `health` INT DEFAULT 20,
            `max_health` INT DEFAULT 20,
            `food` INT DEFAULT 20,
            `food_saturation` FLOAT DEFAULT 20.0,
            `exp_level` INT DEFAULT 0,
            `exp_points` INT DEFAULT 0,
            `gamemode` INT DEFAULT 0,
            `x` FLOAT DEFAULT 0,
            `y` FLOAT DEFAULT 64,
            `z` FLOAT DEFAULT 0,
            `dimension` INT DEFAULT 0,
            `last_sync_time` DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
            INDEX `idx_uuid` (`uuid`)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
    )";

    if (mysql_query(mConnection, createSyncTableSQL)) {
        auto mod = ll::mod::NativeMod::current();
        mod->getLogger().error("\033[31m[数据库] 创建 player_sync_data 表失败！错误: {}\033[0m", mysql_error(mConnection));
        return false;
    }

    // 创建玩家背包数据表（共享数据：所有服务器共享同一份数据）
    const char* createInventoryTableSQL = R"(
        CREATE TABLE IF NOT EXISTS `player_inventory` (
            `id` INT AUTO_INCREMENT PRIMARY KEY,
            `uuid` VARCHAR(36) NOT NULL,
            `server_name` VARCHAR(32) DEFAULT NULL,
            `slot` INT NOT NULL,
            `item_type` VARCHAR(64) DEFAULT NULL,
            `count` INT DEFAULT 0,
            `damage` INT DEFAULT 0,
            `nbt` TEXT DEFAULT NULL,
            UNIQUE KEY `unique_slot` (`uuid`, `slot`),
            INDEX `idx_uuid` (`uuid`)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
    )";

    if (mysql_query(mConnection, createInventoryTableSQL)) {
        auto mod = ll::mod::NativeMod::current();
        mod->getLogger().error("\033[31m[数据库] 创建 player_inventory 表失败！错误: {}\033[0m", mysql_error(mConnection));
        return false;
    }

    auto mod = ll::mod::NativeMod::current();
    mod->getLogger().info("\033[32m[数据库] 数据表初始化成功！\033[0m");
    return true;
}

bool Database::savePlayerData(const PlayerData& data) {
    if (!mConnected) {
        return false;
    }

    std::ostringstream query;
    query << "INSERT INTO `player_data` (`uuid`, `name`, `xuid`, `join_date`, `last_seen`, `play_time`, `is_online`) "
          << "VALUES ('" << data.uuid << "', '" << data.name << "', '" << data.xuid << "', NOW(), NOW(), "
          << data.playTime << ", " << (data.isOnline ? 1 : 0) << ") "
          << "ON DUPLICATE KEY UPDATE "
          << "`name` = VALUES(`name`), `xuid` = VALUES(`xuid`)";

    if (mysql_query(mConnection, query.str().c_str())) {
        auto mod = ll::mod::NativeMod::current();
        mod->getLogger().error("\033[31m[数据库] 保存玩家数据失败！错误: {}\033[0m", mysql_error(mConnection));
        return false;
    }

    return true;
}

bool Database::updatePlayerData(const PlayerData& data) {
    if (!mConnected) {
        return false;
    }

    std::ostringstream query;
    query << "UPDATE `player_data` SET "
          << "`name` = '" << data.name << "', "
          << "`last_seen` = NOW(), "
          << "`play_time` = " << data.playTime << ", "
          << "`is_online` = " << (data.isOnline ? 1 : 0) << " "
          << "WHERE `uuid` = '" << data.uuid << "'";

    if (mysql_query(mConnection, query.str().c_str())) {
        auto mod = ll::mod::NativeMod::current();
        mod->getLogger().error("\033[31m[数据库] 更新玩家数据失败！错误: {}\033[0m", mysql_error(mConnection));
        return false;
    }

    return true;
}

bool Database::loadPlayerData(const std::string& uuid, PlayerData& data) {
    if (!mConnected) {
        return false;
    }

    std::string query = "SELECT * FROM `player_data` WHERE `uuid` = '" + uuid + "'";

    if (mysql_query(mConnection, query.c_str())) {
        auto mod = ll::mod::NativeMod::current();
        mod->getLogger().error("\033[31m[数据库] 加载玩家数据失败！错误: {}\033[0m", mysql_error(mConnection));
        return false;
    }

    MYSQL_RES* result = mysql_store_result(mConnection);
    if (!result) {
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    if (!row) {
        mysql_free_result(result);
        return false;
    }

    data.id       = std::stoi(row[0]);
    data.uuid     = row[1];
    data.name     = row[2];
    data.xuid     = row[3] ? row[3] : "";
    data.joinDate = row[4];
    data.lastSeen = row[5];
    data.playTime = std::stoi(row[6]);
    data.isOnline = std::stoi(row[7]) != 0;

    mysql_free_result(result);
    return true;
}

bool Database::isPlayerExists(const std::string& uuid) {
    if (!mConnected) {
        return false;
    }

    std::string query = "SELECT COUNT(*) FROM `player_data` WHERE `uuid` = '" + uuid + "'";

    if (mysql_query(mConnection, query.c_str())) {
        auto mod = ll::mod::NativeMod::current();
        mod->getLogger().error("\033[31m[数据库] 检查玩家是否存在失败！错误: {}\033[0m", mysql_error(mConnection));
        return false;
    }

    MYSQL_RES* result = mysql_store_result(mConnection);
    if (!result) {
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    int      count = row ? std::stoi(row[0]) : 0;

    mysql_free_result(result);
    return count > 0;
}

// 保存玩家同步数据
bool Database::savePlayerSyncData(const PlayerSyncData& data) {
    if (!mConnected) {
        auto mod = ll::mod::NativeMod::current();
        mod->getLogger().error("\033[31m[数据库] 数据库未连接，无法保存同步数据\033[0m");
        return false;
    }

    auto mod = ll::mod::NativeMod::current();
    mod->getLogger().info("\033[33m[数据库] 准备保存玩家同步数据: UUID={}, 服务器={}\033[0m", data.uuid, data.serverName);

    std::ostringstream query;
    query << "INSERT INTO `player_sync_data` (`uuid`, `server_name`, `health`, `max_health`, `food`, `food_saturation`, "
          << "`exp_level`, `exp_points`, `gamemode`, `x`, `y`, `z`, `dimension`) "
          << "VALUES ('" << data.uuid << "', '" << data.serverName << "', " << data.health << ", " << data.maxHealth << ", "
          << data.food << ", " << data.foodSaturation << ", " << data.expLevel << ", " << data.expPoints << ", "
          << data.gamemode << ", " << data.x << ", " << data.y << ", " << data.z << ", " << data.dimension << ") "
          << "ON DUPLICATE KEY UPDATE "
          << "`server_name` = VALUES(`server_name`), "
          << "`health` = VALUES(`health`), `max_health` = VALUES(`max_health`), "
          << "`food` = VALUES(`food`), `food_saturation` = VALUES(`food_saturation`), "
          << "`exp_level` = VALUES(`exp_level`), `exp_points` = VALUES(`exp_points`), "
          << "`gamemode` = VALUES(`gamemode`), `x` = VALUES(`x`), `y` = VALUES(`y`), "
          << "`z` = VALUES(`z`), `dimension` = VALUES(`dimension`)";

    mod->getLogger().info("\033[33m[数据库] SQL查询: {}\033[0m", query.str());

    if (mysql_query(mConnection, query.str().c_str())) {
        auto mod = ll::mod::NativeMod::current();
        mod->getLogger().error("\033[31m[数据库] 保存玩家同步数据失败！错误: {}\033[0m", mysql_error(mConnection));
        return false;
    }

    mod->getLogger().info("\033[32m[数据库] 玩家同步数据保存成功\033[0m");
    return true;
}

// 加载玩家同步数据（共享数据：不区分服务器）
bool Database::loadPlayerSyncData(const std::string& uuid, const std::string& serverName, PlayerSyncData& data) {
    if (!mConnected) {
        return false;
    }

    // 不再根据 server_name 过滤，所有服务器共享同一份数据
    std::string query = "SELECT * FROM `player_sync_data` WHERE `uuid` = '" + uuid + "'";

    if (mysql_query(mConnection, query.c_str())) {
        auto mod = ll::mod::NativeMod::current();
        mod->getLogger().error("\033[31m[数据库] 加载玩家同步数据失败！错误: {}\033[0m", mysql_error(mConnection));
        return false;
    }

    MYSQL_RES* result = mysql_store_result(mConnection);
    if (!result) {
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    if (!row) {
        mysql_free_result(result);
        return false;
    }

    data.id             = std::stoi(row[0]);
    data.uuid           = row[1];
    data.serverName     = row[2];  // 保留服务器名称，但不用于过滤
    data.health         = std::stoi(row[3]);
    data.maxHealth      = std::stoi(row[4]);
    data.food           = std::stoi(row[5]);
    data.foodSaturation = std::stof(row[6]);
    data.expLevel       = std::stoi(row[7]);
    data.expPoints      = std::stoi(row[8]);
    data.gamemode       = std::stoi(row[9]);
    data.x              = std::stof(row[10]);
    data.y              = std::stof(row[11]);
    data.z              = std::stof(row[12]);
    data.dimension      = std::stoi(row[13]);
    data.lastSyncTime   = row[14];

    mysql_free_result(result);
    return true;
}

// 更新玩家同步数据（共享数据：不区分服务器）
bool Database::updatePlayerSyncData(const PlayerSyncData& data) {
    if (!mConnected) {
        return false;
    }

    std::ostringstream query;
    query << "UPDATE `player_sync_data` SET "
          << "`server_name` = '" << data.serverName << "', "
          << "`health` = " << data.health << ", "
          << "`max_health` = " << data.maxHealth << ", "
          << "`food` = " << data.food << ", "
          << "`food_saturation` = " << data.foodSaturation << ", "
          << "`exp_level` = " << data.expLevel << ", "
          << "`exp_points` = " << data.expPoints << ", "
          << "`gamemode` = " << data.gamemode << ", "
          << "`x` = " << data.x << ", "
          << "`y` = " << data.y << ", "
          << "`z` = " << data.z << ", "
          << "`dimension` = " << data.dimension << " "
          << "WHERE `uuid` = '" << data.uuid << "'";

    if (mysql_query(mConnection, query.str().c_str())) {
        auto mod = ll::mod::NativeMod::current();
        mod->getLogger().error("\033[31m[数据库] 更新玩家同步数据失败！错误: {}\033[0m", mysql_error(mConnection));
        return false;
    }

    return true;
}

// 保存玩家背包数据（共享数据：所有服务器共享同一份数据）
bool Database::savePlayerInventory(const std::string& uuid, const std::string& serverName, const std::vector<PlayerInventoryItem>& items) {
    if (!mConnected) {
        return false;
    }

    // 先删除该玩家的旧背包数据（不区分服务器）
    std::string deleteQuery = "DELETE FROM `player_inventory` WHERE `uuid` = '" + uuid + "'";
    if (mysql_query(mConnection, deleteQuery.c_str())) {
        auto mod = ll::mod::NativeMod::current();
        mod->getLogger().error("\033[31m[数据库] 删除旧背包数据失败！错误: {}\033[0m", mysql_error(mConnection));
        return false;
    }

    // 插入新的背包数据
    for (const auto& item : items) {
        std::ostringstream query;
        query << "INSERT INTO `player_inventory` (`uuid`, `server_name`, `slot`, `item_type`, `count`, `damage`, `nbt`) "
              << "VALUES ('" << uuid << "', '" << serverName << "', " << item.slot << ", "
              << "'" << item.itemType << "', " << item.count << ", " << item.damage << ", "
              << (item.nbt.empty() ? "NULL" : "'" + item.nbt + "'") << ")";

        if (mysql_query(mConnection, query.str().c_str())) {
            auto mod = ll::mod::NativeMod::current();
            mod->getLogger().error("\033[31m[数据库] 保存背包物品失败！错误: {}\033[0m", mysql_error(mConnection));
            return false;
        }
    }

    return true;
}

// 加载玩家背包数据（共享数据：不区分服务器）
bool Database::loadPlayerInventory(const std::string& uuid, const std::string& serverName, std::vector<PlayerInventoryItem>& items) {
    if (!mConnected) {
        return false;
    }

    // 不再根据 server_name 过滤，所有服务器共享同一份数据
    std::string query = "SELECT `slot`, `item_type`, `count`, `damage`, `nbt` FROM `player_inventory` "
                       "WHERE `uuid` = '" + uuid + "' ORDER BY `slot`";

    if (mysql_query(mConnection, query.c_str())) {
        auto mod = ll::mod::NativeMod::current();
        mod->getLogger().error("\033[31m[数据库] 加载玩家背包数据失败！错误: {}\033[0m", mysql_error(mConnection));
        return false;
    }

    MYSQL_RES* result = mysql_store_result(mConnection);
    if (!result) {
        return false;
    }

    items.clear();
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result))) {
        PlayerInventoryItem item;
        item.slot     = std::stoi(row[0]);
        item.itemType = row[1] ? row[1] : "";
        item.count    = std::stoi(row[2]);
        item.damage   = std::stoi(row[3]);
        item.nbt      = row[4] ? row[4] : "";
        items.push_back(item);
    }

    mysql_free_result(result);
    return true;
}

} // namespace bdsmysql