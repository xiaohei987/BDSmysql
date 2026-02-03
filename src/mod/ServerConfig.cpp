#include "mod/ServerConfig.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/io/FileUtils.h"
#include "ll/api/io/Logger.h"
#include <fstream>
#include <filesystem>

namespace bdsmysql {

ServerConfigManager& ServerConfigManager::getInstance() {
    static ServerConfigManager instance;
    return instance;
}

bool ServerConfigManager::load() {
    auto mod = ll::mod::NativeMod::current();
    
    // 使用 config/serverconfig.json
    auto modDir = mod->getModDir() / "config" / "serverconfig.json";
    
    if (std::filesystem::exists(modDir)) {
        mConfigPath = modDir.string();
        mod->getLogger().info("Loading server config from: {}", mConfigPath);
    } else {
        // 不存在，创建默认配置
        mConfigPath = modDir.string();
        createDefaultConfig();
        save();
        mod->getLogger().info("Created default server config file at: {}", mConfigPath);
        return true;
    }

    try {
        std::ifstream file(mConfigPath);
        nlohmann::json j;
        file >> j;
        
        // 加载服务器列表
        if (j.contains("servers")) {
            mServers = j["servers"].get<std::vector<ServerConfig>>();
            mod->getLogger().info("Loaded {} servers from config", mServers.size());
        }
        
        mod->getLogger().info("Server config loaded successfully");
        return true;
    } catch (const std::exception& e) {
        mod->getLogger().error("Failed to load server config: {}", e.what());
        return false;
    }
}

bool ServerConfigManager::save() {
    try {
        // 确保目录存在
        std::filesystem::path path(mConfigPath);
        std::filesystem::create_directories(path.parent_path());
        
        nlohmann::json j;
        j["servers"] = mServers;
        std::ofstream  file(mConfigPath);
        file << j.dump(4);
        return true;
    } catch (const std::exception& e) {
        auto mod = ll::mod::NativeMod::current();
        mod->getLogger().error("Failed to save server config: {}", e.what());
        return false;
    }
}

void ServerConfigManager::createDefaultConfig() {
    // 添加默认服务器配置示例
    ServerConfig server;
    server.name = "示例服务器";
    server.address = "example.com";
    server.port = 19132;
    mServers.push_back(server);
    
    // 立即保存到文件
    try {
        std::filesystem::path path(mConfigPath);
        std::filesystem::create_directories(path.parent_path());
        
        nlohmann::json j;
        j["servers"] = mServers;
        std::ofstream  file(mConfigPath);
        file << j.dump(4);
    } catch (const std::exception& e) {
        auto mod = ll::mod::NativeMod::current();
        mod->getLogger().error("Failed to create default server config file: {}", e.what());
    }
}

} // namespace bdsmysql