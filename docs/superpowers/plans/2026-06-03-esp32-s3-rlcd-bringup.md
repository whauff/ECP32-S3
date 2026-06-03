# ESP32-S3-RLCD-4.2 Bring-up Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 `/Users/yang/Code/ECP32-S3` 内建立一套自包含的 `ESP-IDF` 开发环境，并完成 `ESP32-S3-RLCD-4.2` 的首次编译、烧录和串口启动验证。

**Architecture:** 采用“项目目录内本地框架 + 最小 bring-up 工程”的方式组织工作区。第一阶段只验证工具链、目标芯片、烧录链路和启动日志，不接入屏幕、`LVGL`、音频或其他板载外设，避免 bring-up 阶段问题耦合。

**Tech Stack:** `ESP-IDF v5.5.1`、`idf.py`、`cmake`、`ninja`、`esptool.py`、`ESP32-S3`、macOS 串口设备 `/dev/cu.usbmodem1101`

---

## 文件结构

### 计划内新增目录与文件

- Create: `/Users/yang/Code/ECP32-S3/esp-idf`
- Create: `/Users/yang/Code/ECP32-S3/board-bringup/CMakeLists.txt`
- Create: `/Users/yang/Code/ECP32-S3/board-bringup/sdkconfig.defaults`
- Create: `/Users/yang/Code/ECP32-S3/board-bringup/main/CMakeLists.txt`
- Create: `/Users/yang/Code/ECP32-S3/board-bringup/main/main.c`
- Create: `/Users/yang/Code/ECP32-S3/board-bringup/README.md`

### 各文件职责

- `/Users/yang/Code/ECP32-S3/esp-idf`
  存放当前项目专用的本地 `ESP-IDF` 框架与工具安装入口。
- `/Users/yang/Code/ECP32-S3/board-bringup/CMakeLists.txt`
  声明 `ESP-IDF` 项目入口。
- `/Users/yang/Code/ECP32-S3/board-bringup/sdkconfig.defaults`
  固化初始默认配置，例如目标芯片和串口监视相关设置。
- `/Users/yang/Code/ECP32-S3/board-bringup/main/CMakeLists.txt`
  注册 `main` 组件。
- `/Users/yang/Code/ECP32-S3/board-bringup/main/main.c`
  提供最小启动应用，打印启动日志并周期性输出心跳日志，作为板级 bring-up 验证点。
- `/Users/yang/Code/ECP32-S3/board-bringup/README.md`
  记录本地环境初始化、编译、烧录、串口监视命令，默认用中文说明。

## 说明

本阶段主要是环境搭建和硬件连通性验证，不属于适合单元测试驱动的业务逻辑开发。这里不强行编写脱离硬件的伪测试，而是以“命令执行结果 + 实机串口输出”作为验证标准。进入屏幕、传感器、业务逻辑阶段后，再按 `TDD` 为可测试逻辑补充红绿循环。

### Task 1: 在项目目录内安装本地 ESP-IDF

**Files:**
- Create: `/Users/yang/Code/ECP32-S3/esp-idf`

- [ ] **Step 1: 下载 `ESP-IDF v5.5.1` 到项目目录**

Run:

```bash
git clone -b v5.5.1 --recursive https://github.com/espressif/esp-idf.git /Users/yang/Code/ECP32-S3/esp-idf
```

Expected:

```text
Cloning into '/Users/yang/Code/ECP32-S3/esp-idf'...
```

- [ ] **Step 2: 若子模块未完整下载，补齐全部子模块**

Run:

```bash
git -C /Users/yang/Code/ECP32-S3/esp-idf submodule update --init --recursive
```

Expected:

```text
Submodule path '...': checked out '...'
```

- [ ] **Step 3: 安装 `esp32s3` 所需工具链**

Run:

```bash
/Users/yang/Code/ECP32-S3/esp-idf/install.sh esp32s3
```

Expected:

```text
All done! You can now run:
  . ./export.sh
```

- [ ] **Step 4: 导出本地环境并确认 `idf.py` 可用**

Run:

```bash
. /Users/yang/Code/ECP32-S3/esp-idf/export.sh && idf.py --version
```

Expected:

```text
ESP-IDF v5.5.1
```

- [ ] **Step 5: 提交当前进度**

```bash
git add /Users/yang/Code/ECP32-S3/esp-idf
git commit -m "初始化项目内 ESP-IDF 环境"
```

### Task 2: 创建最小 board-bringup 工程

**Files:**
- Create: `/Users/yang/Code/ECP32-S3/board-bringup/CMakeLists.txt`
- Create: `/Users/yang/Code/ECP32-S3/board-bringup/sdkconfig.defaults`
- Create: `/Users/yang/Code/ECP32-S3/board-bringup/main/CMakeLists.txt`
- Create: `/Users/yang/Code/ECP32-S3/board-bringup/main/main.c`
- Create: `/Users/yang/Code/ECP32-S3/board-bringup/README.md`

- [ ] **Step 1: 创建项目入口 `CMakeLists.txt`**

Write:

```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(board_bringup)
```

- [ ] **Step 2: 创建 `sdkconfig.defaults` 固定基础目标配置**

Write:

```text
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESP_CONSOLE_UART_DEFAULT=y
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
```

- [ ] **Step 3: 创建 `main` 组件声明**

Write:

```cmake
idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS ".")
```

- [ ] **Step 4: 先写最小启动程序**

Write:

```c
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"

static const char *TAG = "board_bringup";

void app_main(void)
{
    esp_chip_info_t chip_info = {0};
    uint32_t flash_size = 0;

    esp_chip_info(&chip_info);
    esp_flash_get_size(NULL, &flash_size);

    ESP_LOGI(TAG, "ESP32-S3-RLCD-4.2 bring-up start");
    ESP_LOGI(TAG, "cores=%d, revision=%d, flash=%" PRIu32 "MB",
             chip_info.cores,
             chip_info.revision,
             flash_size / (1024 * 1024));

    while (1) {
        ESP_LOGI(TAG, "heartbeat");
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
```

- [ ] **Step 5: 创建中文 README 记录使用方法**

Write:

```md
# board-bringup

## 目标

这是 `ESP32-S3-RLCD-4.2` 的第一阶段 bring-up 工程，只用于验证：

- `ESP-IDF` 环境可用
- 工程可编译
- 工程可烧录
- 串口日志正常

## 初始化环境

```bash
. /Users/yang/Code/ECP32-S3/esp-idf/export.sh
```

## 编译

```bash
cd /Users/yang/Code/ECP32-S3/board-bringup
idf.py set-target esp32s3
idf.py build
```

## 烧录

```bash
idf.py -p /dev/cu.usbmodem1101 flash
```

## 监视串口

```bash
idf.py -p /dev/cu.usbmodem1101 monitor
```
```

- [ ] **Step 6: 提交工程骨架**

```bash
git add /Users/yang/Code/ECP32-S3/board-bringup
git commit -m "创建最小 bring-up 工程"
```

### Task 3: 编译并验证最小工程

**Files:**
- Modify: `/Users/yang/Code/ECP32-S3/board-bringup/sdkconfig.defaults`
- Modify: `/Users/yang/Code/ECP32-S3/board-bringup/main/main.c`
- Modify: `/Users/yang/Code/ECP32-S3/board-bringup/README.md`

- [ ] **Step 1: 导出环境并设置目标芯片**

Run:

```bash
. /Users/yang/Code/ECP32-S3/esp-idf/export.sh && cd /Users/yang/Code/ECP32-S3/board-bringup && idf.py set-target esp32s3
```

Expected:

```text
Set Target to: esp32s3
```

- [ ] **Step 2: 编译工程并确认通过**

Run:

```bash
. /Users/yang/Code/ECP32-S3/esp-idf/export.sh && cd /Users/yang/Code/ECP32-S3/board-bringup && idf.py build
```

Expected:

```text
Project build complete.
```

- [ ] **Step 3: 若编译失败，仅做最小修正**

Possible fix in `/Users/yang/Code/ECP32-S3/board-bringup/main/main.c`:

```c
#include <inttypes.h>
```

Expected:

```text
Rebuild after the include fix succeeds.
```

- [ ] **Step 4: 将最终可编译状态写回文档**

Update `/Users/yang/Code/ECP32-S3/board-bringup/README.md` with:

```md
## 当前状态

已验证：

- 可设置 `esp32s3` 目标
- 可本地编译通过

未验证：

- 屏幕
- 音频
- 传感器
```

- [ ] **Step 5: 提交编译通过状态**

```bash
git add /Users/yang/Code/ECP32-S3/board-bringup
git commit -m "验证 bring-up 工程可编译"
```

### Task 4: 烧录开发板并验证串口日志

**Files:**
- Modify: `/Users/yang/Code/ECP32-S3/board-bringup/README.md`

- [ ] **Step 1: 烧录到开发板串口 `/dev/cu.usbmodem1101`**

Run:

```bash
. /Users/yang/Code/ECP32-S3/esp-idf/export.sh && cd /Users/yang/Code/ECP32-S3/board-bringup && idf.py -p /dev/cu.usbmodem1101 flash
```

Expected:

```text
Hard resetting via RTS pin...
Done
```

- [ ] **Step 2: 打开串口监视器并读取启动日志**

Run:

```bash
. /Users/yang/Code/ECP32-S3/esp-idf/export.sh && cd /Users/yang/Code/ECP32-S3/board-bringup && idf.py -p /dev/cu.usbmodem1101 monitor
```

Expected:

```text
I (...) board_bringup: ESP32-S3-RLCD-4.2 bring-up start
I (...) board_bringup: heartbeat
```

- [ ] **Step 3: 若串口日志异常，仅记录最小排查信息**

Update `/Users/yang/Code/ECP32-S3/board-bringup/README.md` with:

```md
## 串口异常排查

- 检查端口是否仍为 `/dev/cu.usbmodem1101`
- 必要时按住 `BOOT` 后重新上电
- 确认使用的是可传数据的 Type-C 线
```

- [ ] **Step 4: 将首次实机验证结果写入文档**

Update `/Users/yang/Code/ECP32-S3/board-bringup/README.md` with:

```md
## 首次实机验证记录

- 烧录端口：`/dev/cu.usbmodem1101`
- 验证内容：烧录、启动、串口日志
- 结果：以实际执行结果为准，成功后补充时间与现象
```

- [ ] **Step 5: 提交首次烧录验证结果**

```bash
git add /Users/yang/Code/ECP32-S3/board-bringup/README.md
git commit -m "记录 bring-up 首次烧录验证"
```
