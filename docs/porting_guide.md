# 移植与依赖详解 (Porting Guide)

本文档详细描述了将 **EasyLoRa** 移植到新硬件平台（如 STM32, ESP32, MSP430 等）所需的接口规范与实现细节。

移植工作主要包含三个部分：
1.  **OSAL**: 适配操作系统或裸机环境。
2.  **Port**: 适配 UART、GPIO 等硬件外设。
3.  **Service**: 实现业务层回调（存储、复位等）。

---

## 1. 接口全景表 (Interface Overview)

### 1.1 OSAL 层 (Operating System Abstraction Layer)
*文件位置: `src/0_OSAL/lora_osal.h`*

| 接口名称 | 类型 | 描述 | 关联功能 |
| :--- | :--- | :--- | :--- |
| `GetTick` | **必须** | 获取系统运行时间 (单位: ms) | 状态机超时、重传计时(1) |
| `DelayMs` | **必须** | 阻塞式毫秒延时 | 硬件初始化时序控制 |
| `EnterCritical` | **必须** | 进入临界区 (关中断/加锁) | 队列操作原子性保护 |
| `ExitCritical` | **必须** | 退出临界区 (开中断/解锁) | 队列操作原子性保护 |
| `Log` | 可选 | 格式化日志输出 (printf) | `LORA_DEBUG_PRINT` 宏开启时生效 |
| `CompensateTick`| 可选 | 补偿系统休眠期间丢失的 Tick | 低功耗 Tickless 模式 |

(1)可以基于SystemTick，也可以基于RTC（实时时钟）。基于RTC时可以选择不实现CompensateTick。

### 1.2 Port 层 (Hardware Interface)
*文件位置: `src/1_Port/lora_port.h`*

| 接口名称 | 类型 | 描述 | 关联功能 |
| :--- | :--- | :--- | :--- |
| `Init` | **必须** | 初始化 UART, GPIO, DMA | 协议栈启动 |
| `TransmitData` | **必须** | **非阻塞**发送数据 (启动 DMA) | 数据包发送 |
| `ReceiveData` | **必须** | 从硬件缓冲区读取数据 | 数据包接收 |
| `IsTxBusy` | **必须** | 查询发送硬件是否忙碌 | 发送流控 |
| `SetMD0` | **必须** | 控制模组模式引脚 (M0/M1) | 模式切换 (配置/通信) |
| `GetAUX` | **必须** | 读取模组状态引脚 | 空闲检测 (CSMA/CA) |
| `SetRST` | 可选 | 控制模组复位引脚 | 错误自愈、死机恢复 |
| `GetEntropy32` | 可选 | 获取硬件随机数/噪声 | 随机避退算法 (防冲突) |

### 1.3 Service 层回调 (Application Callbacks)
*文件位置: `src/4_Service/lora_service.h` -> `LoRa_Callback_t`*

| 回调名称 | 类型 | 描述 | 关联功能 |
| :--- | :--- | :--- | :--- |
| `OnRecvData` | **必须** | 接收到有效业务数据 | 业务逻辑处理 |
| `OnEvent` | **必须** | 系统事件通知 (Init, Sent, Error) | 状态监控、UI 交互 |
| `SaveConfig` | 可选 | 保存配置到 NVS/Flash | 参数掉电保存、OTA |
| `LoadConfig` | 可选 | 从 NVS/Flash 读取配置 | 参数掉电保存 |
| `SystemReset` | 可选 | MCU 系统复位 | OTA 重启生效、致命错误恢复 |

---

## 2. 关键实现细节 (Implementation Details)

为了确保协议栈稳定运行，请务必阅读以下关键接口的实现规范。

### 2.1 临界区保护 (Critical Section)

EasyLoRa 内部已在关键路径（如队列读写）自动调用了临界区保护。移植时，您只需实现底层的“锁”机制。

*   **接口原型**:
    ```c
    // 进入临界区：必须返回当前的中断状态/掩码
    uint32_t EnterCritical(void);

    // 退出临界区：必须恢复之前保存的状态，而不是无脑开中断
    void     ExitCritical(uint32_t ctx);
    ```

*   **实现要求**:
    1.  **必须支持嵌套**: 协议栈可能会在中断回调中被调用，也可能在主循环中被调用。简单的“关中断/开中断”会导致逻辑错误。
    2.  **返回值 (`ctx`)**: `Enter` 函数必须返回进入前的 CPU 状态（如 PRIMASK 寄存器），并将其作为参数传给 `Exit` 函数以进行恢复。

*   **参考实现 (抄作业区)**:

    **方案 A: STM32 / ARM Cortex-M (裸机)**
    ```c
    uint32_t User_EnterCritical(void) {
        uint32_t primask = __get_PRIMASK(); // 1. 保存当前中断状态
        __disable_irq();                    // 2. 关中断
        return primask;                     // 3. 返回状态
    }

    void User_ExitCritical(uint32_t ctx) {
        __set_PRIMASK(ctx);                 // 4. 恢复之前的状态
    }
    ```

    **方案 B: FreeRTOS (ESP32 / STM32)**
    ```c
    // FreeRTOS 的临界区自带嵌套计数，通常不需要保存状态，返回 0 即可
    uint32_t User_EnterCritical(void) {
        taskENTER_CRITICAL(); 
        return 0; 
    }

    void User_ExitCritical(uint32_t ctx) {
        (void)ctx;
        taskEXIT_CRITICAL();
    }
    ```

---

### 2.2 UART 发送 (必须非阻塞)

*   **原型**: `uint16_t LoRa_Port_TransmitData(const uint8_t *data, uint16_t len)`
*   **严禁阻塞**: 此函数**绝不能**使用 `while` 等待发送完成。它必须启动 DMA 或中断发送后**立即返回**。
*   **原因**: 协议栈的状态机 (FSM) 是单线程轮询的。如果发送函数阻塞 100ms，整个接收和 ACK 处理逻辑也会停滞 100ms，导致严重的丢包和超时。
*   **返回值**: 返回实际写入硬件缓冲区的字节数。如果硬件忙 (上一包未发完)，应返回 0。

### 2.3 UART 接收 (推荐 RingBuffer)

*   **原型**: `uint16_t LoRa_Port_ReceiveData(uint8_t *buf, uint16_t max_len)`
*   **实现建议**:
    *   **DMA 循环模式 (Circular Mode)** 是最佳选择。
    *   硬件层应维护一个较大的接收缓冲区 (如 512 Bytes)。
    *   此函数被调用时，从硬件缓冲区“搬运”数据到协议栈缓冲区。
*   **注意**: 不要在此函数中进行复杂的协议解析，只负责搬运字节流。

### 2.4 AUX 引脚 (忙闲逻辑)

*   **原型**: `bool LoRa_Port_GetAUX(void)`
*   **逻辑统一**:
    *   返回值 `true` = **模组忙** (Busy, 不可发送)。
    *   返回值 `false` = **模组闲** (Idle, 可以发送)。
*   **硬件差异**:
    *   **Ebyte E32**: 低电平表示忙 (Low=Busy)。实现时应返回 `!GPIO_Read(...)`。
    *   **ATK-LORA-01**: 高电平表示忙 (High=Busy)。实现时应返回 `GPIO_Read(...)`。
    *   *请务必查阅模组手册，确保逻辑映射正确。*

### 2.5 NVS 配置保存 (Flash 操作)

*   **原型**: `void SaveConfig(const LoRa_Config_t *cfg)`
*   **调用时机**: 该回调通常在主循环 (`LoRa_Service_Run`) 中被触发，处于线程上下文。
*   **耗时警告**: Flash 擦写通常耗时较长 (几十毫秒)，且可能暂停 CPU 执行。这是允许的，但需注意不要在中断服务函数 (ISR) 中调用此回调。
*   **数据校验**: 建议在保存时计算 CRC 或校验和，读取时验证 `cfg->magic` 字段，以防止读取到未初始化的 Flash 垃圾数据。

---

## 3. 移植检查清单 (Checklist)

在编译运行前，请自查：

- [ ] `GetTick` 是否以 1ms 为单位递增？
- [ ] `TransmitData` 是否为非阻塞实现？
- [ ] `GetAUX` 的返回值逻辑是否与模组手册一致？
- [ ] 接收缓冲区是否足够大 (建议 > 256 Bytes)？
- [ ] 是否在 `main` 循环中周期性调用了 `LoRa_Service_Run`？
