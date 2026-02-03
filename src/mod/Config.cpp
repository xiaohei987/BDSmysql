#include "mod/Config.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/io/FileUtils.h"
#include "ll/api/io/Logger.h"
#include <fstream>
#include <filesystem>

namespace bdsmysql {

Config& Config::getInstance() {
    static Config instance;
    return instance;
}

bool Config::load() {
    auto mod = ll::mod::NativeMod::current();
    
    // 使用 config/config.json
    auto modDir = mod->getModDir() / "config" / "config.json";
    
    if (std::filesystem::exists(modDir)) {
        mConfigPath = modDir.string();
        mod->getLogger().info("Loading config from: {}", mConfigPath);
    } else {
        // 不存在，创建默认配置
        mConfigPath = modDir.string();
        createDefaultConfig();
        save();
        mod->getLogger().info("Created default config file at: {}", mConfigPath);
        mod->getLogger().info("\033[33m[配置] 默认服务器名称: {}\033[0m", mDatabaseConfig.serverName);
        return true;
    }

    try {
        std::ifstream file(mConfigPath);
        nlohmann::json j;
        file >> j;
        mDatabaseConfig = j.get<DatabaseConfig>();
        mod->getLogger().info("Config loaded successfully");
        mod->getLogger().info("\033[33m[配置] 服务器名称: {}\033[0m", mDatabaseConfig.serverName);
        mod->getLogger().info("\033[33m[配置] 数据库: {} @ {}:{}\033[0m", mDatabaseConfig.database, mDatabaseConfig.host, mDatabaseConfig.port);
        return true;
    } catch (const std::exception& e) {
        mod->getLogger().error("Failed to load config: {}", e.what());
        return false;
    }
}

bool Config::save() {
    try {
        // 确保目录存在
        std::filesystem::path path(mConfigPath);
        std::filesystem::create_directories(path.parent_path());
        
        nlohmann::json j = mDatabaseConfig;
        std::ofstream  file(mConfigPath);
        file << j.dump(4);
        return true;
    } catch (const std::exception& e) {
        auto mod = ll::mod::NativeMod::current();
        mod->getLogger().error("Failed to save config: {}", e.what());
        return false;
    }
}

void Config::createDefaultConfig() {
    mDatabaseConfig.host        = "localhost";
    mDatabaseConfig.port        = 3306;
    mDatabaseConfig.database    = "minecraft";
    mDatabaseConfig.username    = "root";
    mDatabaseConfig.password    = "password";
    mDatabaseConfig.charset     = "utf8mb4";
    mDatabaseConfig.serverName  = "main";  // 默认服务器名称
}

} // namespace bdsmysql