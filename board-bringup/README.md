# board-bringup

## 工程说明

这是 `Waveshare ESP32-S3-RLCD-4.2` 的板级 bring-up 主工程。

当前已经接入并验证过的能力包括：

- `ESP-IDF` 本地构建环境
- `ESP32-S3` 启动链路与串口日志
- `RLCD` 屏幕底层初始化
- `LVGL` 最小首页
- `RTC` 时间读取与回写
- 电池电压采样与电量估算
- `SHTC3` 温湿度读取
- `PSRAM framebuffer`
- `Wi‑Fi + NTP + RTC 回写`
- 失败兜底的 `AP + 网页配网`

当前依赖策略：

- `LVGL` 通过 `ESP-IDF Component Manager` 管理
- 首次构建时会自动下载到 `managed_components/`
- `managed_components/` 不再作为仓库源码提交

## 当前行为

### 1. 启动后的联网策略

- 如果 `NVS` 里已经保存过 `Wi‑Fi` 配置，系统优先使用已保存配置联网
- 如果 `NVS` 中没有保存配置，但 [sdkconfig.defaults](/Users/yang/Code/ECP32-S3/board-bringup/sdkconfig.defaults) 里填了静态 `SSID` 和密码，就使用静态配置联网
- 如果两者都没有，系统会直接启动 `AP` 配网页面
- 如果多次重连失败，也会自动退回 `AP` 配网模式

### 2. 配网模式

进入配网模式后：

- 热点名：`ECP32-S3-Config`
- 安全方式：开放网络
- 连接热点后，可访问设备默认网关进入网页配网
- 提交 `SSID` 和密码后，设备会保存到 `NVS` 并自动重启

### 3. 校时行为

- `Wi‑Fi` 联网成功后会启动 `SNTP`
- `NTP` 校时成功后会将当前本地时间回写到板载 `RTC`
- 校时稳定一段时间后，当前代码会尝试进入更低功耗运行状态

## 首页说明

当前首页会显示：

- 顶部设备标题与日期
- 中间大号时间
- 右上角电量百分比
- 中部状态卡片：
  - 温度
  - 湿度
  - 电池电压
- 底部状态行：
  - 常规模式下显示 `RTC / WIFI / NTP`
  - 配网模式下显示 `WIFI: CFG | AP: ECP32-S3-Config`

## 环境初始化

每次打开新终端后，先执行：

```bash
. /Users/yang/Code/ECP32-S3/scripts/idf-env.sh
```

默认支持目录位于：

```text
/Users/yang/Dev/ECP32-S3-support/esp-idf-v5.5.1
```

如需改到其他路径，可在执行前设置：

```bash
export ECP32_S3_SUPPORT_ROOT=/你的支持目录
. /Users/yang/Code/ECP32-S3/scripts/idf-env.sh
```

## 关键配置

默认配置位于 [sdkconfig.defaults](/Users/yang/Code/ECP32-S3/board-bringup/sdkconfig.defaults)。

当前关键点包括：

- `16MB Flash`
- 大单应用分区
- `8MB PSRAM`
- `USB Serial/JTAG` 串口输出
- `Light Sleep` 下的 `USB Serial/JTAG` 与 `SPIRAM` 支持
- 默认 `Wi‑Fi` 账号密码留空，由运行时配网补齐

## 编译与烧录

编译：

```bash
. /Users/yang/Code/ECP32-S3/scripts/idf-env.sh
cd /Users/yang/Code/ECP32-S3/board-bringup
idf.py build
```

烧录并查看串口：

```bash
. /Users/yang/Code/ECP32-S3/scripts/idf-env.sh
cd /Users/yang/Code/ECP32-S3/board-bringup
idf.py -p /dev/cu.usbmodem1101 flash monitor
```

彻底清理后重编：

```bash
. /Users/yang/Code/ECP32-S3/scripts/idf-env.sh
cd /Users/yang/Code/ECP32-S3/board-bringup
idf.py fullclean
idf.py build
```

首次构建时如果本地还没有拉过依赖：

- `idf.py build` 会自动下载 `LVGL`
- 下载结果位于 `board-bringup/managed_components/`
- 这是本地依赖缓存，不会提交到 Git

## 已完成验证

已经确认过的结果：

- `RLCD` 测试页可正常显示
- `LVGL` 首页可正常显示
- `RTC`、电量、温湿度可驱动页面刷新
- `PSRAM framebuffer` 可成功分配
- 构建目标为 `ESP32-S3`
- `Flash Size` 已修正为 `16MB`

## 当前适合继续推进的方向

- 实机完整验证 `Wi‑Fi + NTP + RTC` 闭环
- 配网流程体验打磨
- 首页样式产品化整理
- 低功耗与唤醒策略稳定性验证
