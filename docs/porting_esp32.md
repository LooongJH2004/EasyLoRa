# ESP32 移植指南 (Porting for ESP32)

本指南基于 **ESP32-S3** 和 **ESP-IDF (FreeRTOS)** 环境。

## 1. 核心策略
*   **操作系统适配**: 利用 FreeRTOS 的 `vTaskDelay` 实现延时，利用 `taskENTER_CRITICAL` 实现临界区。
*   **UART 驱动**: 使用 ESP-IDF 的 `driver/uart`。它自带软件 RingBuffer 和中断处理，因此 Port 层只需调用 API，无需直接操作寄存器或 DMA。
*   **任务调度**: `LoRa_Service_Run` 必须运行在一个独立的 Task 中，且必须包含 `vTaskDelay` 以喂狗和让出 CPU。

## 2. 关键代码片段

### 2.1 OSAL 适配 (lora_osal_esp32.c)
ESP32 是双核系统，临界区必须使用自旋锁 (Spinlock) 保护。

```c
static portMUX_TYPE s_lora_spinlock = portMUX_INITIALIZER_UNLOCKED;

uint32_t ESP32_EnterCritical(void) {
    taskENTER_CRITICAL(&s_lora_spinlock); // 关中断 + 自旋锁
    return 0; // FreeRTOS 不需要保存 PRIMASK
}

void ESP32_ExitCritical(uint32_t ctx) {
    taskEXIT_CRITICAL(&s_lora_spinlock);
}

uint32_t ESP32_GetTick(void) {
    return (uint32_t)(esp_timer_get_time() / 1000ULL); // us -> ms
}
```

### 2.2 UART 初始化 (lora_port_esp32.c)
安装驱动时必须指定缓冲区大小，否则无法接收数据。

```c
// 安装驱动：RX=1024, TX=1024, 无事件队列
uart_driver_install(UART_NUM_1, 1024, 1024, 0, NULL, 0);
```

### 2.3 非阻塞收发
ESP-IDF 的 API 默认行为符合要求。

```c
// 发送：写入驱动层 RingBuffer，立即返回
uint16_t LoRa_Port_TransmitData(const uint8_t *data, uint16_t len) {
    int txBytes = uart_write_bytes(UART_NUM_1, (const char*)data, len);
    return (txBytes > 0) ? txBytes : 0;
}

// 接收：查询缓冲区长度后读取，不阻塞
uint16_t LoRa_Port_ReceiveData(uint8_t *buf, uint16_t max_len) {
    size_t available = 0;
    uart_get_buffered_data_len(UART_NUM_1, &available);
    if (available == 0) return 0;
    
    if (available > max_len) available = max_len;
    return uart_read_bytes(UART_NUM_1, buf, available, 0); // timeout=0
}
```

### 2.4 NVS 存储 (main.c)
利用 ESP32 强大的 NVS 分区保存配置。

```c
void App_SaveConfig(const LoRa_Config_t *cfg) {
    nvs_handle_t h;
    nvs_open("lora_store", NVS_READWRITE, &h);
    nvs_set_blob(h, "sys_cfg", cfg, sizeof(LoRa_Config_t));
    nvs_commit(h);
    nvs_close(h);
}
```

## 3. 常见坑点 (Pitfalls)
1.  **看门狗超时 (WDT Reset)**:
    *   `LoRa_Service_Run()` 是一个轮询函数。在 FreeRTOS 任务中调用它时，**必须**在 `while(1)` 循环中加入 `vTaskDelay(10 / portTICK_PERIOD_MS)`。
    *   如果为了追求高性能不加延时，必须手动喂狗 (`esp_task_wdt_reset()`)，但这会独占 CPU，不推荐。
2.  **栈溢出**:
    *   LoRa 任务的栈大小建议至少分配 **4096 字节**。`printf` 和 NVS 操作在 ESP32 上非常消耗栈空间。
3.  **引脚冲突**:
    *   ESP32-S3 的 UART 引脚可任意映射，但请避开 Strapping Pins (如 GPIO0, GPIO45, GPIO46) 和 USB JTAG 引脚 (GPIO19/20)，否则可能导致无法烧录或启动失败。