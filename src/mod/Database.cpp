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
        mod->getLogger().error("\033[31m[数据库] 创建数据表失败！错误: {}\033[0m", mysql_error(mConnection));
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

} // namespace bdsmysql