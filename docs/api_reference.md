

## 0. 接口速查 (Quick Reference)

### 0.1 提供的 API (Provided APIs)
*应用层调用以下函数来控制协议栈：*

| API 名称 | 功能简述 | 阻塞性 |
| :--- | :--- | :--- |
| **`LoRa_Service_Init`** | 初始化协议栈与驱动 | 阻塞 (约1-2s) |
| **`LoRa_Service_Run`** | 协议栈主循环 (需周期调用) | 非阻塞 |
| **`LoRa_Service_Send`** | 发送数据 (支持 ACK/重传) | 非阻塞 (入队) |
| **`LoRa_Service_CanSleep`** | 查询是否允许休眠 | 非阻塞 |
| **`LoRa_Service_GetSleepDuration`** | 获取建议休眠时长 (Tickless) | 非阻塞 |
| **`LoRa_Service_IsSendingBusy`** | 查询发送逻辑是否忙碌 | 非阻塞 |
| **`LoRa_Service_GetConfig`** | 获取当前配置 (只读) | 非阻塞 |
| **`LoRa_Service_SetConfig`** | 更新配置并触发保存 | 非阻塞 |
| **`LoRa_Service_SoftReset`** | 请求软重启 (配置生效/自愈) | 非阻塞 (异步) |
| **`LoRa_Service_FactoryReset`** | 恢复出厂设置 | 非阻塞 (异步) |
| **`LoRa_Service_RegisterCipher`** | 注册加密算法钩子 | 非阻塞 |

### 0.2 索要的回调 (Required Callbacks)
*应用层需实现以下函数以支持协议栈运行：*

| 回调名称 | 类型 | 描述 |
| :--- | :--- | :--- |
| **`OnRecvData`** | **必须** | 当收到有效业务数据时触发 |
| **`OnEvent`** | **必须** | 当发生系统事件 (如发送完成、初始化成功) 时触发 |
| **`SaveConfig`** | 可选 | 请求保存配置到 NVS/Flash |
| **`LoadConfig`** | 可选 | 请求从 NVS/Flash 读取配置 |
| **`GetRandomSeed`** | 可选 | 获取硬件随机数 (用于 CSMA/CA 避退) |
| **`SystemReset`** | 可选 | 执行 MCU 硬件复位 |

---

# API 参考手册 (API Reference)

本文档详细描述了 **EasyLoRa** Service 层对外暴露的所有功能接口、数据结构及回调机制。

Service 层是应用层（User App）与协议栈交互的唯一窗口。

---

## 1. 核心数据类型 (Data Types)

在调用 API 之前，请先了解以下核心数据结构。

### 1.1 发送选项 (`LoRa_SendOpt_t`)
用于控制单次发送行为的配置结构体。

```c
typedef struct {
    bool NeedAck;  // true=需要ACK确认(可靠传输), false=不需要(发后即忘)
    // [预留] 未来可扩展: uint8_t TxPower; (单包功率)
    // [预留] 未来可扩展: uint8_t Retries; (单包重传次数)
} LoRa_SendOpt_t;
```

**推荐使用的宏：**
*   `LORA_OPT_CONFIRMED`: `{ .NeedAck = true }` —— **可靠传输**，发送失败会触发重传，最终触发成功或失败事件。
*   `LORA_OPT_UNCONFIRMED`: `{ .NeedAck = false }` —— **不可靠传输**，发送后立即视为完成，不占用重传队列。

### 1.2 接收元数据 (`LoRa_RxMeta_t`)
描述接收到的数据包的物理层信息。

```c
typedef struct {
    int16_t rssi; // 接收信号强度 (dBm)
    int8_t  snr;  // 信噪比 (dB)
} LoRa_RxMeta_t;
```

### 1.3 消息 ID (`LoRa_MsgID_t`)
*   **类型**: `uint16_t`
*   **说明**: 每一条成功入队的消息都会被分配一个唯一的 ID (1~65535)。用户可通过此 ID 在 `OnEvent` 回调中追踪发送结果。

---

## 2. 核心功能 API (Core Functions)

### 2.1 初始化与运行

#### `LoRa_Service_Init`
初始化协议栈，加载配置，启动驱动。

```c
void LoRa_Service_Init(const LoRa_Callback_t *callbacks, uint16_t override_net_id);
```
| 参数 | 说明 |
| :--- | :--- |
| `callbacks` | 指向用户实现的回调函数结构体 (详见第 4 节) |
| `override_net_id` | **0**: 使用 Flash/NVS 中保存的 ID<br>**非0**: 强制使用此 ID 作为本机 NetID (常用于调试或动态分配) |

**Usage Example:**
```c
// 定义回调
const LoRa_Callback_t my_cb = {
    .OnRecvData = My_OnRecv,
    .OnEvent    = My_OnEvent
};

// 在 main() 中调用
LoRa_Service_Init(&my_cb, 0x0001); // 初始化为 ID: 1
```

#### `LoRa_Service_Run`
协议栈主循环。**必须在 `main` 循环中周期性调用。**

```c
void LoRa_Service_Run(void);
```
*   **功能**: 处理接收数据、驱动状态机、管理重传队列、执行软重启倒计时。
*   **建议调用频率**: > 100Hz (或在空闲时持续调用)。

**Usage Example:**
```c
while (1) {
    // 必须周期性调用
    LoRa_Service_Run();
    
    // 其他业务逻辑...
}
```

---

### 2.2 数据发送

#### `LoRa_Service_Send`
请求发送一条数据。

```c
LoRa_MsgID_t LoRa_Service_Send(const uint8_t *data, 
                               uint16_t len, 
                               uint16_t target_id, 
                               LoRa_SendOpt_t opt);
```

| 参数 | 说明 |
| :--- | :--- |
| `data` | 指向要发送的数据缓冲区 |
| `len` | 数据长度 (建议 <= 200 字节) |
| `target_id` | **0xFFFF**: 广播 (Broadcast)，所有同信道设备均可收到<br>**其他值**: 单播 (Unicast)，仅目标 ID 设备处理 |
| `opt` | 发送选项 (见 1.1 节) |
| **返回值** | **> 0**: 成功入队，返回消息 ID<br>**0**: 发送失败 (队列满或参数错误) |

**Usage Example:**
```c
// 场景 1: 发送可靠消息给 ID: 2
LoRa_MsgID_t msg_id = LoRa_Service_Send("Hello", 5, 0x0002, LORA_OPT_CONFIRMED);
if (msg_id > 0) {
    printf("Message Enqueued, ID: %d\n", msg_id);
} else {
    printf("Send Failed (Queue Full)\n");
}

// 场景 2: 广播消息 (无需 ACK)
LoRa_Service_Send("Ping", 4, 0xFFFF, LORA_OPT_UNCONFIRMED);

```
注：架构内提供了宏魔法：
```
#define LORA_OPT_CONFIRMED      (LoRa_SendOpt_t){ .NeedAck = true }  /*!< 需要 ACK 确认 (可靠传输) */
#define LORA_OPT_UNCONFIRMED    (LoRa_SendOpt_t){ .NeedAck = false } /*!< 不需要 ACK (发后即忘) */
```

---

### 2.3 低功耗与状态查询

#### `LoRa_Service_CanSleep`
查询协议栈是否允许系统进入休眠。

```c
bool LoRa_Service_CanSleep(void);
```
*   **返回值**:
    *   `true`: 协议栈空闲（无发送任务、无接收数据、硬件空闲）。**此时可以安全调用 `__WFI()` 或进入 Stop 模式。**
    *   `false`: 协议栈忙碌，禁止休眠。

**Usage Example:**
```c
// 在主循环末尾尝试休眠
if (LoRa_Service_CanSleep()) {
    // 进入低功耗模式 (WFI)
    __WFI();
}
```

#### `LoRa_Service_GetSleepDuration`
获取建议的休眠时长 (用于 Tickless 模式)。

```c
uint32_t LoRa_Service_GetSleepDuration(void);
```
*   **返回值**: 距离下一次定时任务（如重传超时）还剩多少毫秒。

**Usage Example:**
```c
// 获取建议休眠时间
uint32_t sleep_ms = LoRa_Service_GetSleepDuration();

// 如果允许休眠且时间足够长
if (sleep_ms > 10) {
    // 进入深度睡眠 (Deep Sleep) 并设置 RTC 唤醒
    Enter_DeepSleep(sleep_ms);
    
    // 唤醒后补偿系统时间
    LoRa_OSAL_CompensateTick(sleep_ms);
}
```

#### `LoRa_Service_IsSendingBusy`
查询发送逻辑是否忙碌。

```c
bool LoRa_Service_IsSendingBusy(void);
```
*   **用途**: 如果返回 `true`，说明有正在进行的可靠传输（等待 ACK 中）。此时不建议发起新的可靠传输，以免阻塞队列。

**Usage Example:**
```c
// 仅当空闲时才发送心跳包
if (!LoRa_Service_IsSendingBusy()) {
    Send_Heartbeat();
}
```

---

### 2.4 配置与维护

| API | 说明 |
| :--- | :--- |
| `LoRa_Service_GetConfig` | 获取当前运行的配置结构体指针 (只读)。 |
| `LoRa_Service_SetConfig` | 更新配置并触发保存。**注意**: 某些配置(如波特率)需重启生效。 |
| `LoRa_Service_SoftReset` | 请求协议栈软重启。用于配置生效或错误自愈。 |
| `LoRa_Service_FactoryReset` | 恢复出厂设置 (清除 Flash 配置并重启)。 |
| `LoRa_Service_RegisterCipher` | 注册自定义加密/解密算法钩子。 |

**Usage Example (修改配置):**
```c
// 修改信道为 50
const LoRa_Config_t *cfg = LoRa_Service_GetConfig();
LoRa_Config_t new_cfg = *cfg;
new_cfg.channel = 50;

// 应用配置 (会自动触发 SaveConfig 回调)
LoRa_Service_SetConfig(&new_cfg);

// 重启生效
LoRa_Service_SoftReset();
```

---

## 3. 业务回调接口 (Service Callbacks)

EasyLoRa 通过 `LoRa_Callback_t` 结构体向应用层“索要”必要的功能支持。用户需实现这些函数并在 Init 时传入。

```c
typedef struct {
    void (*SaveConfig)(const LoRa_Config_t *cfg);
    void (*LoadConfig)(LoRa_Config_t *cfg);
    uint32_t (*GetRandomSeed)(void);
    void (*SystemReset)(void);
    void (*OnRecvData)(uint16_t src_id, const uint8_t *data, uint16_t len, LoRa_RxMeta_t *meta);
    void (*OnEvent)(LoRa_Event_t event, void *arg);
} LoRa_Callback_t;
```

### 3.1 必须实现的回调

#### `OnRecvData`
*   **触发时机**: 收到并通过校验（CRC、地址过滤）的业务数据包时。
*   **参数**:
    *   `src_id`: 发送方 ID。
    *   `data/len`: 数据内容。
    *   `meta`: 信号强度信息。

**Usage Example:**
```c
void My_OnRecv(uint16_t src_id, const uint8_t *data, uint16_t len, LoRa_RxMeta_t *meta) {
    printf("Received from %d (RSSI: %d): %.*s\n", src_id, meta->rssi, len, data);
}
```

#### `OnEvent`
*   **触发时机**: 系统状态发生变化时。
*   **参数**: `event` (事件类型), `arg` (可选参数)。

**事件类型列表 (`LoRa_Event_t`):**

| 事件枚举 | 说明 | `arg` 参数类型 |
| :--- | :--- | :--- |
| `LORA_EVENT_INIT_SUCCESS` | 初始化/软重启完成 | `NULL` |
| `LORA_EVENT_MSG_RECEIVED` | 收到数据 (通常配合 OnRecvData 使用) | `NULL` |
| `LORA_EVENT_TX_SUCCESS_ID` | **发送成功** (收到 ACK 或 Unconfirmed 发送完) | `LoRa_MsgID_t*` (指向消息ID) |
| `LORA_EVENT_TX_FAILED_ID` | **发送失败** (重传次数耗尽) | `LoRa_MsgID_t*` (指向消息ID) |
| `LORA_EVENT_CONFIG_COMMIT` | 配置已变更，请写入 Flash | `const LoRa_Config_t*` |
| `LORA_EVENT_REBOOT_REQ` | 协议栈请求重启 (OTA 或 RST 指令) | `NULL` |

**Usage Example:**
```c
void My_OnEvent(LoRa_Event_t event, void *arg) {
    switch(event) {
        case LORA_EVENT_TX_SUCCESS_ID:
            printf("Msg ID %d Sent Successfully!\n", *(LoRa_MsgID_t*)arg);
            break;
        case LORA_EVENT_TX_FAILED_ID:
            printf("Msg ID %d Send Failed (Timeout)\n", *(LoRa_MsgID_t*)arg);
            break;
        default:
            break;
    }
}
```

### 3.2 可选实现的回调 (推荐实现)

*   **`SaveConfig` / `LoadConfig`**:
    *   若不实现，配置数据掉电后将丢失，每次上电都使用代码中的默认宏定义。
    *   建议对接 MCU 的内部 Flash 或 EEPROM。
*   **`GetRandomSeed`**:
    *   若不实现，CSMA/CA 随机退避算法的效果将大打折扣，容易发生冲突。
    *   建议返回 ADC 悬空引脚的采样值或硬件 RNG。
*   **`SystemReset`**:
    *   若不实现，OTA 重启指令可能无法彻底复位 MCU 外设。