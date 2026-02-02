# BDSmysql

一个基于 LeviLamina 的 Minecraft 基岩版 MySQL 数据库插件，用于记录玩家数据到 MySQL 数据库。

## 功能特性

- ✅ 自动连接 MySQL 数据库
- ✅ 自动创建不存在的数据库和数据表
- ✅ 记录玩家加入/离开服务器
- ✅ 记录玩家在线时长
- ✅ 中文化日志提示
- ✅ 彩色控制台输出
- ✅ 自动更新玩家数据

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
3. 重启服务器

## 配置说明

在 `plugins/BDSmysql/config.json` 中配置数据库连接信息：

```json
{
    "host": "localhost",
    "port": 3306,
    "database": "minecraft",
    "username": "root",
    "password": "你的密码",
    "charset": "utf8mb4"
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

## 数据库表结构

插件会自动创建 `player_data` 表，结构如下：

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

## 使用说明

### 启动插件

插件会在服务器启动时自动连接数据库，如果数据库不存在会自动创建。

### 日志说明

插件使用彩色日志输出：
- **绿色**：成功消息、玩家加入/离开信息
- **黄色**：警告信息
- **红色**：错误信息

示例日志：
```
\033[32m[数据库] ========== MySQL 数据库连接成功！ ==========\033[0m
\033[32m[数据库] 数据库: minecraft @ localhost:3306\033[0m
\033[32m[玩家] 玩家 Steve (550e8400-e29b-41d4-a716-446655440000) 加入了服务器\033[0m
```

## 常见问题

### 插件无法连接数据库

1. 检查 MySQL 服务是否运行
2. 确认配置文件中的用户名和密码正确
3. 确认 MySQL 服务器允许远程连接
4. 检查防火墙设置

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

## 贡献

欢迎提交 Issue 和 Pull Request！

## 许可证

CC0-1.0 © LeviMC(LiteLDev)
