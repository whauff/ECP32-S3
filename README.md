# ECP32-S3

## 项目说明

这是一个基于 `Waveshare ESP32-S3-RLCD-4.2` 开发板的本地工程仓库，当前以 `ESP-IDF` 为主线进行板级 bring-up、屏幕驱动、首页 UI 和基础外设联调。

当前仓库已经完成：

- `ESP-IDF` 本地环境接入
- `RLCD` 屏幕初始化与测试显示
- `LVGL` 最小首页
- `RTC`、电池电压、电量估算、`SHTC3` 温湿度接入
- `Wi‑Fi + NTP + RTC 回写` 主链路接入
- 基于网页配网的 `AP` 兜底配置流程

## 目录结构

```text
ECP32-S3/
  board-bringup/   当前主工程
  docs/            设计与计划文档
  scripts/         环境脚本
```

补充说明：

- 当前主工程位于 [board-bringup/README.md](/Users/yang/Code/ECP32-S3/board-bringup/README.md)
- 本仓库不再内置完整 `ESP-IDF` 支持目录
- 当前默认支持目录位于 `~/Dev/ECP32-S3-support`

## 环境准备

每次打开新终端后先执行：

```bash
. /Users/yang/Code/ECP32-S3/scripts/idf-env.sh
```

默认会加载：

- `IDF_PATH=/Users/yang/Dev/ECP32-S3-support/esp-idf-v5.5.1`
- `IDF_TOOLS_PATH=/Users/yang/Dev/ECP32-S3-support/esp-idf-v5.5.1/.espressif`

如果你把支持目录放到别处，可以先设置：

```bash
export ECP32_S3_SUPPORT_ROOT=/你的支持目录
. /Users/yang/Code/ECP32-S3/scripts/idf-env.sh
```

## 快速开始

编译：

```bash
. /Users/yang/Code/ECP32-S3/scripts/idf-env.sh
cd /Users/yang/Code/ECP32-S3/board-bringup
IDF_COMPONENT_MANAGER=0 idf.py build
```

烧录并查看串口：

```bash
. /Users/yang/Code/ECP32-S3/scripts/idf-env.sh
cd /Users/yang/Code/ECP32-S3/board-bringup
IDF_COMPONENT_MANAGER=0 idf.py -p /dev/cu.usbmodem1101 flash monitor
```

## 当前状态

当前首页已经可以显示真实板级数据：

- 日期与时间
- 电池百分比
- 电池电压
- 温度
- 湿度
- `RTC / Wi‑Fi / NTP` 状态

如果设备没有可用的 `Wi‑Fi` 配置，会自动启动：

- 热点名：`ECP32-S3-Config`
- 配网页面地址：连接热点后访问设备默认网关

## 后续建议

当前更适合继续推进：

- 完整验证 `Wi‑Fi + NTP + RTC` 实机闭环
- 首页排版进一步产品化
- 低功耗策略稳定性验证
- 音频、SD 卡、更多板载能力接入
