# RLCD Test Pattern Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在现有 `board-bringup` 工程中接入最小 `RLCD` 驱动，并显示一张稳定黑白测试页。

**Architecture:** 参考官方 `11_U8G2_Test` 的底层 `RLCD` 初始化实现，但在当前项目中只保留最小 SPI + 帧缓冲 + 刷新链路，不引入 `U8G2` 或 `LVGL`。`main` 负责调用 `rlcd_bsp` 完成屏幕初始化与测试图案绘制。

**Tech Stack:** `ESP-IDF v5.5.1`、`esp_lcd`、`spi_master`、`ESP32-S3`、`RLCD` 1-bit framebuffer

---

## 文件结构

- Create: `/Users/yang/Code/ECP32-S3/board-bringup/components/rlcd_bsp/CMakeLists.txt`
- Create: `/Users/yang/Code/ECP32-S3/board-bringup/components/rlcd_bsp/rlcd_bsp.h`
- Create: `/Users/yang/Code/ECP32-S3/board-bringup/components/rlcd_bsp/rlcd_bsp.c`
- Modify: `/Users/yang/Code/ECP32-S3/board-bringup/main/main.c`
- Modify: `/Users/yang/Code/ECP32-S3/board-bringup/README.md`

## 说明

本阶段主要是硬件点屏与实机验证，不适合用脱离硬件的单元测试驱动。验证标准以构建结果、烧录结果与实机串口/肉眼观察为准。
