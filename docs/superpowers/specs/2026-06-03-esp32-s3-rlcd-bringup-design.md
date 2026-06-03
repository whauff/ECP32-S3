# ESP32-S3-RLCD-4.2 Bring-up 设计说明

## 目标

在 `/Users/yang/Code/ECP32-S3` 下为微雪 `ESP32-S3-RLCD-4.2` 开发板建立一套自包含的开发工作区。

第一阶段只聚焦板级 bring-up：

- 本地 `ESP-IDF` 放在项目目录内
- 创建一个最小可运行的 `board-bringup` 工程，目标芯片为 `esp32s3`
- 工程可以成功编译
- 工程可以通过 `/dev/cu.usbmodem1101` 成功烧录
- 设备启动后可以稳定输出串口日志

## 目录结构

```text
/Users/yang/Code/ECP32-S3/
  esp-idf/
  board-bringup/
  docs/
```

## 方案

开发基线采用 `ESP-IDF + VS Code`，并将框架放在当前项目目录内，因为这个工作区只服务这一块开发板。

首个工程保持尽量小，只验证工具链、目标芯片选择、烧录流程、串口监视和应用启动链路。屏幕、`LVGL`、音频、`RTC`、`SD`、传感器等功能暂不放入第一步，避免 bring-up 阶段问题相互耦合。

## 第一阶段暂不包含

以下内容明确延后到 bring-up 完成之后：

- `RLCD` 屏幕初始化
- `LVGL` 界面
- 音频输入输出
- `SHTC3`、`RTC`、`SD` 卡、电池电量监测
- 上层业务逻辑

## 完成标准

满足以下条件即视为第一阶段完成：

1. `/Users/yang/Code/ECP32-S3/esp-idf` 下具备可用的 `ESP-IDF`
2. `board-bringup` 能以 `esp32s3` 目标成功编译
3. 开发板可通过 `/dev/cu.usbmodem1101` 被访问
4. 烧录流程成功
5. 串口日志显示正常启动信息和应用启动输出

## 文档约定

除代码标识、命令、路径、官方专有名词等必须保留原文的内容外，本项目中的 `Markdown` 文档默认优先使用中文。
