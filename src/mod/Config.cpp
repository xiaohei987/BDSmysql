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
    
    // 优先使用插件根目录下的 config.json
    auto modDir = mod->getModDir() / "config.json";
    auto dataDir = mod->getDataDir() / "config.json";
    
    // 检查根目录配置文件是否存在
    if (std::filesystem::exists(modDir)) {
        mConfigPath = modDir.string();
        mod->getLogger().info("Loading config from: {}", mConfigPath);
    } else if (std::filesystem::exists(dataDir)) {
        mConfigPath = dataDir.string();
        mod->getLogger().info("Loading config from: {}", mConfigPath);
    } else {
        // 都不存在，创建默认配置到根目录
        mConfigPath = modDir.string();
        createDefaultConfig();
        save();
        mod->getLogger().info("Created default config file at: {}", mConfigPath);
        return true;
    }

    try {
        std::ifstream file(mConfigPath);
        nlohmann::json j;
        file >> j;
        mDatabaseConfig = j.get<DatabaseConfig>();
        mod->getLogger().info("Config loaded successfully");
        return true;
    } catch (const std::exception& e) {
        mod->getLogger().error("Failed to load config: {}", e.what());
        return false;
    }
}

bool Config::save() {
    try {
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
    mDatabaseConfig.host     = "localhost";
    mDatabaseConfig.port     = 3306;
    mDatabaseConfig.database = "minecraft";
    mDatabaseConfig.username = "root";
    mDatabaseConfig.password = "password";
    mDatabaseConfig.charset  = "utf8mb4";
}

} // namespace bdsmysql