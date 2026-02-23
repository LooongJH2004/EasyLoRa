#施工中
---


# EasyLoRa 

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform](https://img.shields.io/badge/platform-STM32%20%7C%20ESP32%20%7C%20Any%20MCU-green.svg)
![Language](https://img.shields.io/badge/language-C99-orange.svg)

**EasyLoRa** 是一个专为嵌入式系统设计的、**轻量级**、**高可靠**、**硬件无关** 的 UART LoRa 链路层中间件。

它致力于解决廉价 UART LoRa 模组（如 ATK-LORA-01, Ebyte E32 等）在实际工程应用中的痛点，通过软件定义的方式，将一条“不可靠的物理串口线”升级为一条“可靠的逻辑数据链路”。

> **⚠️ 命名说明 (Naming Convention)**:
> 本项目仓库名为 **EasyLoRa**，但在源代码内部，API 和文件前缀仍保留为 **`LoRaPlat`** (e.g., `LoRa_Service_Init`)。这不会影响功能使用。

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

### 第一步：获取代码
```bash
git clone https://github.com/LooongJH2004/EasyLoRa
```

### 第二步：适配接口 (Porting)
你需要实现 `lora_port.c` (硬件接口) 和 `lora_osal.c` (系统接口)。

```c
// 示例：在 main.c 中初始化
#include "lora_service.h"

// 1. 定义回调
const LoRa_Callback_t my_cb = {
    .OnRecvData = My_OnRecv, // 接收回调
    .OnEvent    = My_OnEvent // 事件回调
};

int main(void) {
    // 2. 初始化硬件与 OSAL
    BSP_Init();
    LoRa_OSAL_Init(&my_osal_impl);

    // 3. 启动协议栈
    LoRa_Service_Init(&my_cb, 0x0001); // 本机 ID: 1

    while(1) {
        // 4. 周期性轮询
        LoRa_Service_Run();
    }
}
```

### 第三步：发送数据
```c
// 发送 "Hello" 给 ID 为 2 的设备，要求 ACK 确认
LoRa_Service_Send("Hello", 5, 0x0002, LORA_OPT_CONFIRMED);
```

👉 **平台移植教程**:
*   [STM32F103 裸机移植指南](./docs/porting_stm32.md)
*   [ESP32-S3 FreeRTOS 移植指南](./docs/porting_esp32.md)

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

## 9. 更新计划 (Roadmap)

*   [x] **v3.0**: 基础架构重构，引入 OSAL 和 Port 层。
*   [x] **v3.4**: 增加 ACK 重传机制与发送队列。
*   [x] **v3.9**: 支持 OTA 远程配置与系统自愈。
*   [ ] **v4.0 (Plan)**: 支持多模组并发 (Multi-Instance)。
*   [ ] **v4.1 (Plan)**: 简易网关模式 (Gateway Mode)。

---

---

### 三、 长线更新计划批判与优化

你的新计划比之前的更加**务实**和**聚焦**。以下是具体的优化建议：

#### 1. 阶段划分优化

**Phase 1: 单实例基石 (Foundation)**
*   **目标**：打造一个完美的单节点协议栈。
*   **核心任务**：
    *   **Driver 层抽象**：定义 `lora_driver_ops_t` 接口，将 ATK-LORA-01 的实现剥离为 `driver_atk_lora01.c`。
    *   **物理 MAC 支持**：正式引入定点传输模式，利用硬件过滤地址，降低 MCU 功耗。
    *   **兼容性验证**：尝试适配第二款模组 (如 Ebyte E32)，验证 Driver 抽象层的通用性。

**Phase 2: 单实例网关与联网 (Gateway MVP)**
*   **目标**：验证 LoRa + WiFi/4G 的数据透传能力。
*   **核心任务**：
    *   **ESP32-S3 网关**：移植 LoRaPlat 到 ESP32，实现 LoRa -> MQTT (EMQX) 的上行链路。
    *   **下行控制**：实现 MQTT -> LoRa 的指令下发。
    *   **性能评估**：测试单模组网关在 10/50/100 节点下的并发处理能力和丢包率。
    *   **决策点**：如果单模组网关能满足 50 节点 1min/包 的需求，则无需急于进入多实例阶段。

**Phase 3: 多实例架构重构 (Multi-Instance)**
*   **目标**：突破单模组的物理带宽限制。
*   **核心任务**：
    *   **上下文封装**：将全局变量封装进 `LoRa_Handle_t`，实现 `LoRa_Init(&hLoRa1)`。
    *   **并发调度**：在 Manager 层实现简单的轮询或优先级调度，防止多模组同时发送导致的电流冲击或干扰。
    *   **虚拟化测试**：在 PC 端 (Linux/Windows) 编写模拟器，通过 socket 模拟 UART，实现 100+ 虚拟节点的并发压力测试。

**Phase 4: 生态与云 (Ecosystem)**
*   **目标**：提供端到端的解决方案。
*   **核心任务**：
    *   **虚拟测试单元**：完善 PC 端模拟器，支持脚本化测试场景 (如模拟 50% 丢包率环境)。
    *   **云端面板**：提供简单的 Grafana 模板或微信小程序 demo。


**License**: MIT License
**Author**: [Your Name / LoRaPlat Team]