
---


# EasyLoRa 

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform](https://img.shields.io/badge/platform-STM32%20%7C%20ESP32%20%7C%20Any%20MCU-green.svg)
![Language](https://img.shields.io/badge/language-C99-orange.svg)

**EasyLoRa** 是一个专为嵌入式系统设计的、**轻量级**、**高可靠**、**硬件无关** 的 UART LoRa 链路层中间件。

它致力于解决廉价 UART LoRa 模组（如 ATK-LORA-01, Ebyte E32 等）在实际工程应用中的痛点，通过软件定义的方式，将一条“不可靠的物理串口线”升级为一条“可靠的逻辑数据链路”。

> **⚠️ 命名说明 (Naming Convention)**:
> 本项目仓库名为 **EasyLoRa**，但在源代码内部，API 和文件前缀仍保留为 **`LoRaPlat`** (e.g., `LoRa_Service_Init`)。这不会影响功能使用。

**注意，本项目包含人工设计架构及 AI 辅助生成的代码。**

---

## 1. 核心特性 (Key Features)

*   **🛡️ 可靠传输**: 内置 **Stop-and-Wait ARQ** (停等协议) 与 **ACK 确认机制**，支持超时重传，确保关键数据必达。
*   **🧩 极致解耦**: 采用 **OSAL (操作系统抽象层)** 设计，一套代码无缝运行于 **裸机 (Bare-Metal)** 与 **RTOS** 环境。
*   **⚡ 异步并发**: 全非阻塞 API 设计，内置发送队列与环形缓冲区，自动处理半双工通信时序。
*   **🔧 远程运维**: 支持 **OTA 参数配置** (CMD 指令集)，支持参数掉电保存与系统自愈。
*   **🔒 安全管理**: 支持逻辑 ID (NetID) 过滤、组播 (GroupID) 及 Payload 加密钩子。

---

## 2. 适用场景 (Use Cases)

*   **智能家居/楼宇**: 需要可靠控制（如开关灯必达）的场景。
*   **工业遥测**: 环境恶劣，干扰大，需要数据完整性校验的场景。
*   **低成本组网**: 使用廉价模组快速构建星型网络。

---

## 3. 依赖说明 (Dependencies)

EasyLoRa 采用分层设计，移植时需满足以下软件接口与硬件模组要求。

### 3.1 软件接口依赖 (Software Interfaces)

移植 EasyLoRa 仅需适配以下三层接口，无需修改核心代码：

*   **OSAL 依赖 (底层基石)**
    *   **必须实现**: `GetTick` (毫秒级时基), `DelayMs` (阻塞延时), `Enter/ExitCritical` (临界区保护)。
    *   **可选实现**: `Log` (调试日志), `CompensateTick` (低功耗休眠补偿)。
    *   *适配文件: `lora_osal.c`*

*   **Port 层依赖 (硬件抽象)**
    *   **接口定义**: `lora_port.h` 定义了标准硬件操作接口。
    *   **用户实现**: 需提供 `lora_port.c`，实现 UART 字节收发 (推荐 DMA/中断环形缓冲)、GPIO 控制 (AUX/MD0/RST)。
    *   *适配文件: `lora_port.c`*

*   **Service 层依赖 (业务回调)**
    *   **机制**: 通过 `LoRa_Callback_t` 结构体向业务层索要功能支持。
    *   **内容**:
        *   `OnRecvData`: 接收数据回调。
        *   `OnEvent`: 系统事件通知 (如发送完成、初始化成功)。
        *   `Save/LoadConfig`: NVS/Flash 读写接口 (用于参数掉电保存)。
        *   `SystemReset`: 系统复位接口 (用于 OTA 重启或错误自愈)。

👉 **更详细的接口定义、函数原型及实现规范，请参阅**: [📄 移植与依赖详解 (Porting Guide)](./docs/porting_guide.md)        

### 3.2 LoRa 模组硬性要求 (Module Requirements)

EasyLoRa 是基于 **UART AT 指令** 的中间件，所选用的 LoRa 模组 **必须** 具备以下特性：

| 特性 | 要求等级 | 说明 |
| :--- | :--- | :--- |
| **UART 接口** | **必须** | 支持标准串口通信 (波特率通常为 9600/115200)。 |
| **透传模式** | **必须** | 支持透明传输 (Transparent Mode)，即“发什么收什么”，不自动添加私有协议头。 |
| **定点模式** | **推荐** | 支持 **Fixed Transmission (定点传输)**。即发送数据前先发送 3 字节 (高位地址+低位地址+信道)，模组根据地址自动过滤空中数据。**若不支持，将无法使用低功耗唤醒和硬件级地址过滤功能。** （1）|
| **AUX 引脚** | **推荐** | 模组需提供 Busy/Ready 状态引脚 (如 Ebyte 的 AUX)。**若不支持，将无法准确判断发送完成时机，只能依赖延时估算。** |
| **参数配置** | **必须** | 支持通过 AT 指令或特定时序修改：**信道 (Channel)**、**空速 (Air Rate)**、**功率 (Power)**。 |
| **休眠控制** | **可选** | 支持通过引脚 (如 MD0/M1) 控制模组进入休眠/配置模式。 |

> ✅ **已验证模组**: 正点原子 ATK-LORA-01 (SX1278)。

（1）该定点模式的封包逻辑仅针对正点原子 ATK-LORA-01。

---


## 4. 快速开始 (Quick Start)

### 4.1 获取工程 (Get Project)

我们提供了两种方式获取代码：**直接下载工程包** (推荐新手) 或 **克隆源码**。

#### 方式一：下载开箱即用的工程包 (推荐)
以下压缩包已包含所有依赖库及核心代码，解压后即可直接编译运行，无需配置 Git 环境。

| 平台 | 开发环境 | 下载链接 | 说明 |
| :--- | :--- | :--- | :--- |
| **STM32F103** | Keil MDK 5 | [📥 **点击下载 ZIP**](https://github.com/user-attachments/files/25483284/LoRaPlatForSTM32.zip) | 基于标准库，已配置 DMA 与 Flash 模拟 |
| **ESP32-S3** | VS Code (IDF) | [📥 **点击下载 ZIP**](https://github.com/user-attachments/files/25483283/LoRaPlatForESP32S3.zip) | 基于 FreeRTOS，已配置 NVS 分区 |

#### 方式二：克隆源码集成
如果您希望将 EasyLoRa 集成到现有项目中，请克隆本仓库：
```bash
git clone https://github.com/LooongJH2004/EasyLoRa.git
```

**注意，这也会额外下载代码仓库中的示例工程文件。**

---

### 4.2 硬件连接 (Hardware Connection)

为了确保示例工程正常运行，请按照以下引脚定义连接您的 MCU 与 LoRa 模组 (以 ATK-LORA-01 为例)。

> ⚠️ **注意**: LoRa 模组的 TXD 需接 MCU 的 RX，RXD 接 MCU 的 TX (交叉连接)。

#### 🔌 STM32F103C8T6 接线表
*更详细的引脚定义可以参考 `examples/LoRaPlatForSTM32`下的pin_config.txt*

| LoRa 模组引脚 | STM32 引脚 | 功能说明 |
| :--- | :--- | :--- |
| **RXD** | **PB10** (UART3_TX) | 数据发送 (MCU -> LoRa) |
| **TXD** | **PB11** (UART3_RX) | 数据接收 (LoRa -> MCU) |
| **MD0** | **PA4** | 模式控制 (高=配置, 低=通信) |
| **AUX** | **PA5** | 状态指示 (高=忙, 低=闲) |
| **VCC** | 3.3V | **严禁接 5V** |
| **GND** | GND | 共地 |

#### 🔌 ESP32-S3 接线表
*更详细的引脚定义可以参考 `examples/LoRaPlatForESP32S3`下的pin_config.txt*

| LoRa 模组引脚 | ESP32 引脚 | 功能说明 |
| :--- | :--- | :--- |
| **RXD** | **GPIO 17** (UART1_TX) | 数据发送 (MCU -> LoRa) |
| **TXD** | **GPIO 18** (UART1_RX) | 数据接收 (LoRa -> MCU) |
| **MD0** | **GPIO 16** | 模式控制 |
| **AUX** | **GPIO 15** | 状态指示 |
| **VCC** | 3.3V | 电源 |
| **GND** | GND | 共地 |

---

### 4.3 运行验证

1.  **连接硬件**：按上表连接 MCU 与 LoRa 模组，并确保 USB-TTL 连接到 MCU 的调试串口（STM32 为 PA9/PA10，ESP32 为 USB 串口）。
2.  **编译烧录**：打开对应的示例工程，编译并下载。
3.  **观察日志**：打开串口调试助手（波特率 115200），复位开发板。
    *   若看到 `[EVT] LoRa Stack Ready`，说明初始化成功。
    *   若看到 `[DRV] Handshake Fail`，请检查接线或电源。
---

## 5. 软件架构 (Architecture)

EasyLoRa 采用严格的分层架构设计，确保层与层之间依赖清晰：

*   **Service Layer**: 业务逻辑、配置管理、OTA、状态监控。
*   **Manager Layer**: 协议栈核心 (FSM 状态机, ARQ 重传, 队列管理)。
*   **Driver Layer**: 模组 AT 指令驱动 (硬件 MAC 抽象)。
*   **Port Layer**: 硬件抽象层 (BSP)。
*   **OSAL**: 操作系统抽象层。

👉 **深入了解架构设计**: [架构设计文档](./docs/architecture.md)

---

## 6. 资源占用 (Resource Usage)

在 STM32F103 上，默认配置下的资源占用情况：

| 资源类型 | 占用量 (约) | 说明 |
| :--- | :--- | :--- |
| **Flash (Code)** | ~8 KB | 取决于优化等级和启用的功能 (如 OTA, Log) |
| **RAM (Static)** | ~2.5 KB | 包含收发缓冲区 (各 512B) 及去重表 |
| **Stack** | < 512 Bytes | 深度优化，无递归调用 |

👉 **性能分析与裁剪指南**: [性能文档](./docs/performance.md)

---

## 7. API 参考 (API Reference)

核心 API 列表：

*   `LoRa_Service_Init`: 初始化协议栈。
*   `LoRa_Service_Run`: 主循环轮询 (Tick 驱动)。
*   `LoRa_Service_Send`: 发送数据 (支持 Confirmed/Unconfirmed)。
*   `LoRa_Service_CanSleep`: 低功耗休眠判断。

👉 **完整 API 手册**: [API 参考文档](./docs/api_reference.md)

---

## 8. 数据流分析 (Data Flow)

了解数据如何在各层之间流转，有助于你理解 ACK 机制和重传逻辑。

👉 **查看数据流向图**: [数据流分析](./docs/data_flow.md)

---

### 9. 长线更新计划 (Roadmap)

EasyLoRa 将沿着 **"更通用 -> 更实用 -> 更强大"** 的路径持续演进。

#### Phase 1: 驱动抽象与低功耗 (v4.0)
*目标：彻底解耦特定模组依赖，实现真正的硬件无关。*
- [ ] **驱动层重构**: 定义 `lora_driver_ops_t` 接口，将 ATK-LORA-01 的实现剥离为独立驱动文件。
- [ ] **适配新模组**: 增加对 **Ebyte E32 (SX1278)** 和 **E22 (SX1262)** 系列模组的支持。
- [ ] **物理 MAC 支持**: 引入 **定点传输模式 (Fixed Mode)**，利用模组硬件过滤地址，大幅降低 MCU 唤醒频率。

#### Phase 2: 简易网关与物联网 (v4.1)
*目标：打通 LoRa 到互联网的“最后一公里”。*
- [ ] **ESP32 网关例程**: 实现 LoRa <-> MQTT (EMQX/Aliyun) 的双向透传。
- [ ] **下行控制**: 实现云端 JSON 指令解析，控制 LoRa 终端节点。
- [ ] **性能基准测试**: 在 50+ 节点环境下测试网关的并发吞吐能力。

#### Phase 3: 多实例与虚拟化 (v5.0)
*目标：支持复杂组网与大规模压力测试。*
- [ ] **多实例重构**: 将全局变量封装为 `LoRa_Handle_t` 上下文，支持单 MCU 驱动多个 LoRa 模组（如双频段网关）。
- [ ] **PC 端模拟器**: 编写 Linux/Windows 虚拟驱动，通过 Socket 模拟 UART，实现 100+ 虚拟节点的自动化压力测试。

---

> **致敬开源精神**
> 本项目的诞生离不开大语言模型（LLM）的辅助，而 AI 的智慧源于全球开发者无私贡献的开源代码。
> 秉持“取之开源，回馈开源”的理念，本项目采用 **MIT 协议** 完全开放。希望这份代码能成为后来者学习 LoRa 技术的阶梯，正如前辈们的代码曾指引我一样。

-

> **🕊️ A Tribute to Open Source**
> This project was built with the assistance of Large Language Models (LLMs), whose intelligence stands on the shoulders of the global open-source community.
> In the spirit of **"From the community, for the community,"** this project is released under the **MIT License**. I hope this codebase serves as a stepping stone for others learning LoRa, just as open-source code has guided me.

---
**License**: MIT License
**Author**: [ljh]