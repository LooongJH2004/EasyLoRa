
# 注意，以下内容仅为理论分析，还未进行相关测试，仅供参考。

# 性能分析与资源占用 (Performance & Resource Usage)


本文档详细分析了 **EasyLoRa** 核心框架的资源占用情况（RAM, Stack, Flash），并提供了基于 `LoRaPlatConfig.h` 的裁剪指南。

> **⚠️ 统计基准**:
> *   **编译器**: ARMCC / GCC (Optimization -O2)
> *   **平台**: STM32F103 (Cortex-M3)
> *   **配置**: 默认配置 (Default Config)
> *   **范围**: 仅统计 `src/` 下的核心代码，**不包含** 用户移植层 (`lora_port.c`) 中定义的 **DMA 收发 RAM 缓冲区** (即供 DMA 外设搬运数据的目标/源数组)。


---

## 1. 静态 RAM 占用 (SRAM Usage)

EasyLoRa 采用 **静态内存分配** 策略，核心逻辑不使用 `malloc/free`，确保了内存使用的确定性。

### 1.1 核心组件占用表 (Core Framework)

基于默认配置 (`LORA_MAX_PAYLOAD_LEN = 200`, `MGR_TX_BUF_SIZE = 512`) 的详细计算：

| 模块 (Module) | 变量/缓冲区 | 大小 (Bytes) | 说明 |
| :--- | :--- | :--- | :--- |
| **Manager Buffer** | `s_TxBufArr` | 512 | 发送环形缓冲区 (序列化后的字节流) |
| | `s_RxBufArr` | 512 | 接收环形缓冲区 (来自 Port 的原始流) |
| | `s_AckBufArr` | 64 | ACK 专用高优先级队列 |
| | `s_RxWorkspace` | 512 | 接收解析工作区 (用于零拷贝解析) |
| **Manager Queue** | `s_TxQueue` | **832** | 待发送包队列 (4个槽位 * 208字节/槽) |
| **Manager FSM** | `s_FSM` | ~284 | 包含重传缓存、去重表、状态上下文 |
| **Service** | `s_CmdCopyBuf` | 128 | OTA 指令解析缓存 (静态局部变量) |
| | `s_RespBuf` | 64 | OTA 响应缓存 (静态局部变量) |
| **Driver** | `s_AtRxBuf` | 128 | AT 指令接收缓存 |
| **Misc** | 句柄与标志位 | ~60 | 各模块的控制结构体、回调指针等 |
| **总计 (Total)** | | **~3084 Bytes** | **约 3.0 KB** |

### 1.2 用户移植层占用 (User Porting)

这部分内存由用户在 `lora_port.c` 中定义，**不计入核心框架**，但需预留：

*   **DMA 接收缓冲**: 建议 512 Bytes (`LORA_PORT_DMA_RX_SIZE`)。
*   **DMA 发送缓冲**: 建议 512 Bytes (`LORA_PORT_DMA_TX_SIZE`)。
*   **合计**: 约 1 KB。

---


## 2. 堆栈占用 (Stack Usage)

EasyLoRa 经过深度优化，避免了递归调用。但在处理数据包序列化和指令解析时，会使用栈空间。

### 2.1 峰值调用链分析

**场景 A: 业务循环 (LoRa_Service_Run)**
```text
LoRa_Service_Run()
  └─ LoRa_Manager_Run()
       ├─ [Stack] tx_stack_buf [232 Bytes]  <-- 最大的栈消耗点
       └─ LoRa_Manager_FSM_Run()
            └─ _FSM_Action_PhyTxScheduler()
                 └─ LoRa_Port_TransmitData()
```

**场景 B: 接收回调 (OnRecv)**
```text
LoRa_Manager_Run()
  └─ s_MgrCb.OnRecv() -> _Service_OnRecv()
       └─ LoRa_Service_Command_Process()
            └─ snprintf / strtok            <-- 标准库函数消耗
```

### 2.2 建议栈大小
*   **核心逻辑需求**: 约 **400 Bytes**。
*   **安全余量**: 建议为任务/主栈预留至少 **1 KB** (考虑到 `printf` 和中断嵌套的开销)。

---

## 3. Flash 占用 (Code Size)

Flash 占用高度依赖于编译器优化等级 (`-O2` vs `-O0`) 和是否开启调试日志。

| 组件 | 估算大小 (Bytes) | 备注 |
| :--- | :--- | :--- |
| **Manager Layer** | ~3.5 KB | 包含 FSM 状态机、协议封包解包、CRC16 |
| **Service Layer** | ~2.5 KB | 包含 OTA 解析、配置管理、监视器 |
| **Driver Layer** | ~1.0 KB | AT 指令生成与解析 |
| **Utils & OSAL** | ~0.5 KB | 环形缓冲算法、接口适配 |
| **总计 (Total)** | **~7.5 KB** | **(开启 Log 且无优化时可能达到 12KB)** |

---

## 4. 裁剪与优化指南 (Optimization)

通过修改 `LoRaPlatConfig.h` 中的宏定义，可以显著降低资源占用。

### 4.1 内存 (RAM) 深度裁剪

如果您的 MCU RAM 紧张 (如 STM32F030, 4KB RAM)，可按以下方案裁剪：

| 宏定义 (Macro) | 默认值 | 推荐裁剪值 | 节省 RAM | 代价说明 |
| :--- | :--- | :--- | :--- | :--- |
| `LORA_MAX_PAYLOAD_LEN` | 200 | **64** | **~600 B** | 单包最大只能发 64 字节，显著减小 `s_TxQueue` |
| `MGR_TX_BUF_SIZE` | 512 | **128** | **384 B** | 发送吞吐量下降，发太快容易丢包 |
| `MGR_RX_BUF_SIZE` | 512 | **128** | **768 B** | 接收吞吐量下降 (影响 `RxBuf` 和 `RxWorkspace`) |
| `LORA_ENABLE_OTA_CFG` | 1 | **0** | **192 B** | 移除 OTA 功能，节省 `s_CmdCopyBuf` 等 |

> **🚀 极限裁剪效果**:
> 将 Payload 降为 64，缓冲区降为 128，关闭 OTA 后，核心 RAM 占用可降至 **~1.0 KB**。

### 4.2 Flash 空间裁剪

| 宏定义 (Macro) | 动作 | 节省 Flash | 说明 |
| :--- | :--- | :--- | :--- |
| `LORA_DEBUG_PRINT` | 设为 **0** | **~2-4 KB** | 移除所有字符串常量和 printf 调用 (生产环境建议关闭) |
| `LORA_ENABLE_OTA_CFG` | 设为 **0** | **~1.5 KB** | 移除 `lora_service_command.c` 及其解析逻辑 |
| `LORA_ENABLE_CRC` | 设为 **false** | **~0.2 KB** | 移除 CRC16 查表/计算逻辑 |

---

## 5. 总结 (Summary)

*   **标准型 (STM32F103/ESP32)**: 默认配置即可，占用约 3KB RAM / 8KB Flash。
*   **紧凑型 (STM32F0/8位机)**: 建议将 Payload 限制在 64 字节，关闭 OTA 和 Log，占用可压至 1KB RAM / 4KB Flash。