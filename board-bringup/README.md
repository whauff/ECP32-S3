# board-bringup

## 说明

这是 `ESP32-S3-RLCD-4.2` 的板级 bring-up 工程。

当前阶段已完成以下能力：

- `ESP-IDF` 环境已正确初始化
- `esp32s3` 目标工程入口已建立
- 基础启动日志与 `heartbeat` 日志稳定输出
- `RLCD` 底层初始化已接入
- `LVGL` 最小首页已接入
- `RTC`、电池电压、电量估算、`SHTC3` 温湿度已接入首页
- `PSRAM framebuffer` 已启用
- `Wi‑Fi + NTP + RTC 回写` 代码已接入

当前联网校时逻辑说明：

- 如果未配置 `Wi‑Fi SSID`，系统会跳过联网校时，首页会显示 `WIFI 未配 | NTP 未校时`
- 如果配置了 `Wi‑Fi`，系统启动后会自动联网
- 联网拿到 IP 后会启动 `NTP`
- 一旦 `NTP` 校时成功，会把当前本地时间回写到板载 `RTC`

## 初始化环境

每次打开新终端后，先执行：

```bash
. /Users/yang/Code/ECP32-S3/scripts/idf-env.sh
```

编译、烧录与串口监视已完成首次验证，后续如调整板级配置，应重新执行对应验证。

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
  - `RTC 正常/回退`
  - `WIFI 在线/等待/未配`
  - `NTP 已校时/未校时`

## 联网配置

默认配置位于 [sdkconfig.defaults](/Users/yang/Code/ECP32-S3/board-bringup/sdkconfig.defaults)：

```ini
CONFIG_BOARD_WIFI_SSID=""
CONFIG_BOARD_WIFI_PASSWORD=""
CONFIG_BOARD_NTP_SERVER="ntp.aliyun.com"
CONFIG_BOARD_TIMEZONE="CST-8"
```

如需启用自动校时，把 `SSID` 和 `密码` 改成你的网络信息后重新编译烧录即可。

## 编译验证

已在本机完成当前工程编译验证，执行命令如下：

```bash
. /Users/yang/Code/ECP32-S3/scripts/idf-env.sh
cd /Users/yang/Code/ECP32-S3/board-bringup
IDF_COMPONENT_MANAGER=0 idf.py set-target esp32s3
IDF_COMPONENT_MANAGER=0 idf.py build
```

验证结果：

- `idf.py set-target esp32s3` 成功
- `idf.py build` 成功生成 `build/board_bringup.bin`
- 已切换为更大的单应用分区，适配当前 `LVGL + Wi‑Fi` 固件体积
- 当前应用二进制大小约为 `0x1471a0`

补充说明：

- 工程已按开发板实际规格修正为 `16MB Flash`
- 当前构建输出中的烧录参数已显示 `--flash_size 16MB`
- 当前最小应用分区大小约为 `0x177000`

## 首次实机验证

已完成早期版本的首次烧录与显示验证，执行命令如下：

```bash
. /Users/yang/Code/ECP32-S3/scripts/idf-env.sh
cd /Users/yang/Code/ECP32-S3/board-bringup
IDF_COMPONENT_MANAGER=0 idf.py -p /dev/cu.usbmodem1101 flash monitor
```

验证结果：

- 烧录成功，`esptool.py` 已将 `bootloader.bin`、`board_bringup.bin` 和 `partition-table.bin` 写入开发板
- 已确认 `RLCD` 纯色测试页可显示
- 已确认 `LVGL` 首页可显示
- 已确认 `RTC`、电量、温湿度数据链路可以驱动页面刷新
- 启动日志已确认 `SPI Flash Size : 16MB`

## 当前推荐验证命令

```bash
. /Users/yang/Code/ECP32-S3/scripts/idf-env.sh
cd /Users/yang/Code/ECP32-S3/board-bringup
IDF_COMPONENT_MANAGER=0 idf.py build
IDF_COMPONENT_MANAGER=0 idf.py -p /dev/cu.usbmodem1101 flash monitor
```
