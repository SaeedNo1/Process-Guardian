# Process Guardian 进程守护者

## 📖 项目简介

**Process Guardian（进程守护者）** 是一个 Windows 平台下的进程管理工具，提供进程保护、自动重启、持续终止、实时监控等功能。支持三种权限级别运行（普通/管理员/SYSTEM），可实现开机自启和后台守护。

---

## 🎯 核心功能

### 1. 进程保护（Protect Process）
- 监控指定进程，当进程退出时自动重启
- 适用于需要持续运行的关键程序

### 2. 重复终止（Repeated Termination）
- 持续终止指定的进程，阻止其运行
- 支持单进程终止或进程树终止
- 适用于阻止恶意软件或不需要的程序（或者一些流氓软件）

### 3. 实时监控（Real-time Monitoring）
- 通过 `observer.exe` 实时监控进程的：
  - **CPU 使用率**（绿色曲线）
  - **内存使用**（蓝色曲线）
  - **缓存使用**（黄色曲线）
- 支持鼠标悬停查看历史数据点

### 4. 多权限模式（有时可能无法正常安装服务，可能无法提权到SYSTEM）
| 模式 | 说明 | 适用场景 |
|------|------|----------|
| 普通权限 | 以当前用户权限运行 | 管理普通进程 |
| 管理员权限 | 请求 UAC 提权 | 管理系统进程 |
| SYSTEM 权限 | 以系统服务方式运行 | 管理核心系统进程 |

### 5. 开机自启
- 自动注册到 Windows 启动项
- 支持隐藏模式后台运行

---

## 📁 项目结构

```
process-guardian/
├── guardian.exe          # 主程序（GUI 界面）
├── guardiand.exe         # 后台守护进程
├── observer.exe          # 实时监控工具
├── service_wrapper.exe   # Windows 服务包装器
├── settings.exe          # 设置程序
├── install_service.exe   # 服务安装工具
├── setup_service.bat     # 服务安装脚本
├── build.bat             # 编译脚本
├── build_settings.bat    # 编译设置程序脚本
├── data/
│   ├── config.dat        # 配置文件（二进制）
│   ├── settings.ini      # 设置文件
│   ├── process_guardian.log  # 日志文件
│   └── reload.signal     # 配置重载信号文件
├── src/
│   ├── core/             # 核心模块
│   │   ├── process_monitor.c/h    # 进程监控
│   │   └── process_protector.c/h  # 进程保护
│   ├── gui/              # GUI 界面
│   │   ├── gui.c/h
│   │   └── main.c               # 主入口
│   └── utils/            # 工具模块
│       └── logger.c/h           # 日志系统
└── build/                # 源代码（编译目录）
    ├── main.c
    ├── daemon.c
    ├── observer.c
    ├── service_wrapper.c
    ├── settings.c
    └── Makefile
```

---

## 🚀 使用方法

### 启动主程序

```bash
# 双击运行 guardian.exe
# 或命令行启动
guardian.exe

# 隐藏模式启动（后台运行）
guardian.exe --hidden
```

### 界面操作

#### 左侧：正在运行的进程列表
- 显示当前系统中所有运行进程的：
  - 进程名称
  - PID（进程 ID）
  - 内存占用（KB）

#### 右侧：已保护的进程列表
- 显示已配置的进程及其状态：
  - 进程名称
  - PID
  - 类型（保护/重复结束/重复结束树）

### 右键菜单功能

#### 对运行中的进程右键：

| 选项 | 功能 |
|------|------|
| **结束进程** | 终止选中的进程 |
| **结束进程树** | 终止进程及其所有子进程 |
| **重复结束进程** | 持续终止该进程（阻止运行） |
| **重复结束进程树** | 持续终止进程及其子进程 |
| **保护进程** | 保护进程，退出时自动重启 |
| **打开进程位置** | 在资源管理器中打开进程所在目录 |
| **实时监控** | 打开 observer 窗口监控该进程 |
| **安装 SYSTEM 服务** | 以 SYSTEM 权限安装服务 （有时可能用不了！！！有时可能用不了！！！有时可能用不了！！！）|

#### 对已保护进程右键：

| 选项 | 功能 |
|------|------|
| **取消重复终止** | 移除重复终止规则 |
| **取消保护** | 移除保护规则 |

### 启动守护进程

点击 **"启动守护"** 按钮，可选择：

1. **普通权限** - 以当前用户身份运行
2. **管理员权限** - 请求 UAC 提权后运行
3. **SYSTEM 权限** - 安装为 Windows 服务运行

### 实时监控

```bash
# 监控指定 PID 的进程
observer.exe --pid <进程 ID>

# 示例：监控 PID 为 1234 的进程
observer.exe --pid 1234
```

监控窗口显示：
- 30 分钟历史数据曲线
- 鼠标悬停可查看任意时间点的具体数值
- 初始 2 秒刷新，收集 10 个点后改为 20 秒刷新

---

## ⚙️ 配置文件

### settings.ini

位于 `data/settings.ini`：

```ini
[Settings]
CheckInterval=100      # 检查间隔（毫秒），默认 100ms
ReloadInterval=3000    # 配置重载间隔（毫秒），默认 3000ms
```

**参数说明：**
- `CheckInterval`: 守护进程检查进程状态的频率
  - 范围：100 - 60000 毫秒
  - 值越小响应越快，但 CPU 占用越高
- `ReloadInterval`: 自动重新加载配置的频率
  - 范围：1000 - 3600000 毫秒
  - 可通过创建 `reload.signal` 文件手动触发重载

### config.dat

二进制配置文件，存储保护的进程列表：
- 进程名称
- PID
- 是否为进程树模式
- 是否为重复终止模式

---

## 📝 日志文件

日志位于 `data/process_guardian.log`：

```
[2026-06-13 11:30:00] Guardian started
[2026-06-13 11:30:01] Guardian Daemon started (hidden=1)
[2026-06-13 11:30:02] 已加载 3 个保护进程
[2026-06-13 11:30:05] 保护进程重启：notepad.exe
[2026-06-13 11:30:10] 重复终止：malware.exe
[2026-06-13 11:30:15] 已注册开机自启
```

---

## 🔧 高级功能

### 以服务方式运行（SYSTEM 权限）

1. 点击 **"启动守护"** → 选择 **"SYSTEM 权限"**
2. 程序会自动：
   - 检查 `GuardianDaemon` 服务是否存在
   - 如不存在，请求管理员权限创建服务
   - 启动服务

**手动安装服务：**
```bash
# 使用 sc 命令
sc create GuardianDaemon binPath= "完整路径\service_wrapper.exe" start= auto

# 启动服务
sc start GuardianDaemon

# 停止服务
sc stop GuardianDaemon

# 删除服务
sc delete GuardianDaemon
```

### 配置热重载

守护进程会定期检查配置变更，也可手动触发：

```bash
# 创建信号文件触发立即重载
type nul > data\reload.signal
```

### 开机自启

程序会自动在以下注册表项添加启动项：
```
HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run
```
键名：`GuardianDaemon`

---

## 🛡️ 安全说明

### 权限要求

- **普通进程管理**: 当前用户权限即可
- **系统进程管理**: 需要管理员权限
- **核心进程管理**: 需要 SYSTEM 权限（通过服务实现）

### SeDebugPrivilege

程序会自动获取 `SeDebugPrivilege` 权限以终止受保护的进程。

### 单实例保护

守护进程使用互斥量 `Global\GuardianDaemonMutex` 确保单实例运行。

---

## 🐛 故障排除

### 问题：无法终止某些进程
**解决**: 使用管理员或 SYSTEM 权限重新启动守护进程

### 问题：服务启动失败
**解决**: 
1. 确保以管理员身份运行主程序
2. 检查服务是否已存在：`sc query GuardianDaemon`
3. 删除旧服务后重新安装

### 问题：配置不生效
**解决**:
1. 检查 `data/settings.ini` 格式是否正确
2. 创建 `data\reload.signal` 触发重载
3. 重启守护进程

### 问题：日志文件过大
**解决**: 定期清理 `data/process_guardian.log`

### 问题：千万不要误操作
**解决**：这是整个REMIND.md里面最重要的环节了，我曾经圈了电脑中运行的所有的进程（除了守护者进程）并点-击了“重复终止”。然后...电脑显示黑屏，重启后出现了一行字”开机遇到问题，无法修复“...这事实在是太离（丢）奇（脸）了。
**蓝屏怎么办**：可以把U盘插入另一台电脑，在U盘里面安装PE（这会格式化U盘，务必备份！！推荐安装wePE），然后把U盘插在蓝屏的电脑里面，重启电脑然后狂按F2，进入设置页面之后选择你的PE U盘，进入的桌面就是PE，然后删除此软件就可以。

---

## 📋 编译说明

### 环境要求
- Windows SDK
- GCC (MinGW) 或 MSVC
- Make

### 编译主程序
```bash
cd build
make
```

### 编译设置程序
'''bash
build_settings.bat

### 手动编译（GCC）
```bash
gcc -o guardian.exe -mwindows build/main.c -lcomctl32 -lpsapi
gcc -o guardiand.exe -mwindows build/daemon.c -lpsapi
gcc -o observer.exe -mwindows build/observer.c -lpsapi
gcc -o service_wrapper.exe build/service_wrapper.c


## 📄 许可证

本项目为个人/内部使用工具。


## 📞 技术支持

查看日志文件 `data/process_guardian.log` 获取详细运行信息。

---

*最后更新：2026-06-13*
