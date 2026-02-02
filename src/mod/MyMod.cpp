#include "mod/MyMod.h"

#include "ll/api/mod/RegisterHelper.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
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

    PlayerData data;
    if (Database::getInstance().loadPlayerData(uuid, data)) {
        data.playTime += static_cast<int>(duration);
        data.isOnline = false;
        Database::getInstance().updatePlayerData(data);
        getSelf().getLogger().info("\033[32m[玩家] 已更新玩家 {} 的数据 (总游玩时间: {}秒)\033[0m", name, data.playTime);
    }
}

} // namespace bdsmysql

LL_REGISTER_MOD(bdsmysql::MyMod, bdsmysql::MyMod::getInstance());
