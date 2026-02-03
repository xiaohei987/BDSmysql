# BDSmysql

一个基于 LeviLamina 1.7.7 的 Minecraft 基岩版 MySQL 数据库插件，实现多服务器玩家数据互通和跨服传送功能。

## 功能特性

### 数据记录
- ✅ 自动连接 MySQL 数据库
- ✅ 自动创建不存在的数据库和数据表
- ✅ 记录玩家加入/离开服务器
- ✅ 记录玩家在线时长
- ✅ 记录玩家 UUID、名称、XUID

### 数据互通
- ✅ 玩家装备同步（头盔、胸甲、护腿、靴子）
- ✅ 玩家副手物品同步
- ✅ 玩家属性同步（生命值、饱食度、饱和度）
- ✅ 玩家经验值同步
- ✅ 玩家游戏模式同步
- ✅ 跨服数据互通

### 跨服传送
- ✅ `/tpserver` 命令传送到其他服务器
- ✅ 传送前自动保存玩家数据
- ✅ 支持多服务器配置

### 其他特性
- ✅ 中文化日志提示
- ✅ 彩色控制台输出
- ✅ 自动更新玩家数据
- ✅ 服务器停止时自动保存所有在线玩家数据

## 环境要求

- Windows 10 或更高版本
- [xmake](https://xmake.io/) 构建系统
- MySQL 8.0 或更高版本
- Visual Studio 2022
- [LeviLamina](https://github.com/LiteLDev/LeviLamina) 1.7.7

## 安装方法

### 从源码构建

1. 克隆本仓库
```bash
git clone https://github.com/xiaohei987/BDSmysql.git
cd BDSmysql
```

2. 配置构建环境
```bash
xmake f -y -p windows -a x64 -m release
```

3. 编译插件
```bash
xmake
```

4. 编译成功后，在 `bin/BDSmysql/` 目录下找到编译好的插件文件

### 安装到服务器

1. 将 `bin/BDSmysql/` 目录下的所有文件复制到服务器的 `plugins/BDSmysql/` 目录
2. 编辑 `plugins/BDSmysql/config.json` 文件，配置数据库连接信息
3. 编辑 `plugins/BDSmysql/server_config.json` 文件，配置服务器列表
4. 重启服务器

## 配置说明

### 数据库配置

在 `plugins/BDSmysql/config.json` 中配置数据库连接信息：

```json
{
    "host": "localhost",
    "port": 3306,
    "database": "minecraft",
    "username": "root",
    "password": "你的密码",
    "charset": "utf8mb4",
    "serverName": "服务器1"
}
```

| 配置项 | 说明 | 默认值 |
|--------|------|--------|
| host | MySQL 服务器地址 | localhost |
| port | MySQL 服务器端口 | 3306 |
| database | 数据库名称 | minecraft |
| username | 数据库用户名 | root |
| password | 数据库密码 | password |
| charset | 字符集 | utf8mb4 |
| serverName | 当前服务器名称（用于数据互通） | 服务器1 |

### 服务器配置

在 `plugins/BDSmysql/server_config.json` 中配置服务器列表：

```json
{
    "servers": [
        {
            "name": "服务器1",
            "address": "server1.example.com",
            "port": 19132
        },
        {
            "name": "服务器2",
            "address": "server2.example.com",
            "port": 19132
        }
    ]
}
```

| 配置项 | 说明 |
|--------|------|
| name | 服务器显示名称 |
| address | 服务器地址 |
| port | 服务器端口 |

## 数据库表结构

插件会自动创建以下表：

### player_data 表

```sql
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
```

| 字段 | 类型 | 说明 |
|------|------|------|
| id | INT | 主键 ID |
| uuid | VARCHAR(36) | 玩家 UUID（唯一） |
| name | VARCHAR(16) | 玩家名称 |
| xuid | VARCHAR(32) | 玩家 XUID |
| join_date | DATETIME | 首次加入时间 |
| last_seen | DATETIME | 最后在线时间 |
| play_time | INT | 总游玩时长（秒） |
| is_online | TINYINT(1) | 在线状态（0=离线，1=在线） |

### player_sync_data 表

```sql
CREATE TABLE IF NOT EXISTS `player_sync_data` (
    `id` INT AUTO_INCREMENT PRIMARY KEY,
    `uuid` VARCHAR(36) NOT NULL,
    `server_name` VARCHAR(64) NOT NULL,
    `health` INT DEFAULT 20,
    `max_health` INT DEFAULT 20,
    `food` INT DEFAULT 20,
    `food_saturation` INT DEFAULT 5,
    `exp_level` INT DEFAULT 0,
    `exp_points` INT DEFAULT 0,
    `gamemode` INT DEFAULT 0,
    `x` FLOAT DEFAULT 0,
    `y` FLOAT DEFAULT 0,
    `z` FLOAT DEFAULT 0,
    `dimension` INT DEFAULT 0,
    `last_sync_time` DATETIME DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY `unique_player_server` (`uuid`, `server_name`),
    INDEX `idx_uuid` (`uuid`),
    INDEX `idx_server` (`server_name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
```

| 字段 | 类型 | 说明 |
|------|------|------|
| id | INT | 主键 ID |
| uuid | VARCHAR(36) | 玩家 UUID |
| server_name | VARCHAR(64) | 服务器名称 |
| health | INT | 生命值 |
| max_health | INT | 最大生命值 |
| food | INT | 饱食度 |
| food_saturation | INT | 饱和度 |
| exp_level | INT | 经验等级 |
| exp_points | INT | 经验点数 |
| gamemode | INT | 游戏模式（0=生存，1=创造，2=冒险，3=旁观） |
| x | FLOAT | X 坐标 |
| y | FLOAT | Y 坐标 |
| z | FLOAT | Z 坐标 |
| dimension | INT | 维度（0=主世界，1=下界，2=末地） |
| last_sync_time | DATETIME | 最后同步时间 |

### player_inventory 表

```sql
CREATE TABLE IF NOT EXISTS `player_inventory` (
    `id` INT AUTO_INCREMENT PRIMARY KEY,
    `uuid` VARCHAR(36) NOT NULL,
    `server_name` VARCHAR(64) NOT NULL,
    `slot` INT NOT NULL,
    `item_type` VARCHAR(256) NOT NULL,
    `count` INT DEFAULT 1,
    `damage` INT DEFAULT 0,
    `nbt` TEXT,
    INDEX `idx_uuid_server` (`uuid`, `server_name`),
    INDEX `idx_slot` (`slot`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
```

| 字段 | 类型 | 说明 |
|------|------|------|
| id | INT | 主键 ID |
| uuid | VARCHAR(36) | 玩家 UUID |
| server_name | VARCHAR(64) | 服务器名称 |
| slot | INT | 槽位索引（0-35=背包，36-39=装备，40=副手） |
| item_type | VARCHAR(256) | 物品类型 |
| count | INT | 物品数量 |
| damage | INT | 损坏值 |
| nbt | TEXT | NBT 数据（SNBT 格式，包含附魔等） |

## 使用说明

### 跨服传送

使用 `/tpserver` 命令传送到其他服务器：

```
/tpserver 服务器1
/tpserver 服务器2
```

传送前会自动保存当前玩家的所有数据到数据库。

### 数据同步逻辑

#### 玩家加入服务器时

1. 识别玩家是否有数据库数据
2. 如果有数据：
   - 清空玩家装备和副手
   - 从数据库加载装备、属性、经验等数据
   - 应用数据到玩家
3. 如果没有数据：
   - 保存玩家当前装备到数据库
   - 不清除装备

#### 玩家离开服务器时

1. 保存玩家当前属性（生命值、饱食度、经验等）
2. 使用 `ActorInventoryUtils` 正确获取装备数据
3. 保存装备和副手数据到数据库
4. 更新玩家在线时长

#### 服务器停止时

1. 遍历所有在线玩家
2. 保存每个玩家的数据到数据库
3. 更新在线状态为离线

### 日志说明

插件使用彩色日志输出：
- **绿色**：成功消息、玩家加入/离开信息
- **黄色**：警告信息、数据互通信息
- **红色**：错误信息

示例日志：
```
\033[32m[数据库] ========== MySQL 数据库连接成功！ ==========\033[0m
\033[32m[玩家] 玩家 Steve (550e8400-e29b-41d4-a716-446655440000) 加入了服务器\033[0m
\033[33m[数据互通] 玩家 Steve 没有数据库数据，将保存当前装备\033[0m
\033[33m[数据互通] 保存装备: minecraft:diamond_helmet 在槽位 36\033[0m
\033[32m[数据互通] 已保存玩家 Steve 的 4 个装备\033[0m
```

## 常见问题

### 插件无法连接数据库

1. 检查 MySQL 服务是否运行
2. 确认配置文件中的用户名和密码正确
3. 确认 MySQL 服务器允许远程连接
4. 检查防火墙设置

### 装备无法同步

1. 确认使用的是 LeviLamina 1.7.7
2. 检查数据库中 `player_inventory` 表是否有数据
3. 查看服务器日志中的 `[数据互通]` 相关信息

### 跨服传送失败

1. 检查 `server_config.json` 中的服务器配置是否正确
2. 确认目标服务器是否在线
3. 检查网络连接

### 数据库不存在

插件会自动创建配置文件中指定的数据库，无需手动创建。

### 查看详细错误

查看服务器日志文件（通常在 `logs/latest.log`）以获取详细的错误信息。

## 开发

### 依赖项

- [LeviLamina](https://github.com/LiteLDev/LeviLamina) 1.7.7
- [MySQL Connector/C](https://dev.mysql.com/downloads/connector/c/) 8.0
- [fmt](https://github.com/fmtlib/fmt) 11.2.0

### 构建

```bash
# 配置构建环境
xmake f -y -p windows -a x64 -m release

# 编译
xmake
```

编译后的插件文件位于 `bin/BDSmysql/` 目录。

### 技术实现

- 使用 `ActorInventoryUtils::getItem()` 正确获取装备数据
- 使用 `ServerPlayer::setArmor()` 和 `setOffhandSlot()` 正确设置装备
- 使用 `ServerPlayer::sendArmor()` 和 `sendInventory()` 同步装备到客户端
- 使用 `CompoundTag` 序列化和反序列化 NBT 数据（附魔等）

## 贡献

欢迎提交 Issue 和 Pull Request！

## 许可证

CC0-1.0 © LeviMC(LiteLDev)

## 更新日志

### v1.1.0
- ✅ 添加跨服传送功能
- ✅ 实现装备数据同步
- ✅ 实现属性数据同步
- ✅ 修复装备获取问题
- ✅ 优化网络同步顺序

### v1.0.0
- ✅ 基础玩家数据记录功能
- ✅ 数据库自动创建
- ✅ 在线时长统计