#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace bdsmysql {

struct ServerConfig {
    std::string name;
    std::string address;
    int         port;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ServerConfig, name, address, port)
};

class ServerConfigManager {
public:
    static ServerConfigManager& getInstance();

    bool load();
    bool save();
    void createDefaultConfig();

    const std::vector<ServerConfig>& getServers() const { return mServers; }

private:
    ServerConfigManager()  = default;
    ~ServerConfigManager() = default;

    ServerConfigManager(const ServerConfigManager&)            = delete;
    ServerConfigManager& operator=(const ServerConfigManager&) = delete;

    std::vector<ServerConfig> mServers;
    std::string              mConfigPath;
};

} // namespace bdsmysql