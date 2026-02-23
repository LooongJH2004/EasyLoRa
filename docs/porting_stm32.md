# STM32 移植指南 (Porting for STM32)

本指南基于 **STM32F103C8T6** 和 **标准外设库 (StdPeriph_Lib)**。

## 1. 核心策略
*   **发送 (TX)**: 必须使用 **DMA 单次模式**。因为协议栈是非阻塞的，若使用 `USART_SendData` 轮询等待，会卡死状态机。
*   **接收 (RX)**: 强烈推荐 **DMA 循环模式 (Circular Mode)**。配合空闲中断 (IDLE IRQ) 或轮询读取，可实现零拷贝的高效接收。
*   **临界区**: 使用 `PRIMASK` 寄存器实现关中断，防止高优先级中断打断队列操作。

## 2. 关键代码片段

### 2.1 OSAL 临界区 (lora_osal.c)
STM32 裸机开发必须保存和恢复中断状态，支持嵌套调用。

```c
// 进入临界区：保存 PRIMASK 并关中断
uint32_t User_EnterCritical(void) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

// 退出临界区：恢复之前的状态
void User_ExitCritical(uint32_t ctx) {
    __set_PRIMASK(ctx);
}
```

### 2.2 DMA 接收配置 (lora_port_stm32f10x.c)
配置 DMA 为循环模式，这样缓冲区填满后会自动回到开头，无需手动重置。

```c
// DMA1_Channel3 -> USART3_RX
DMA_InitStructure.DMA_Mode = DMA_Mode_Circular; // [关键] 循环模式
DMA_InitStructure.DMA_BufferSize = PORT_DMA_RX_BUF_SIZE;
// ... 其他常规配置 ...
DMA_Init(DMA1_Channel3, &DMA_InitStructure);
```

### 2.3 接收数据读取 (RingBuffer 逻辑)
利用 DMA 硬件计数器 (`CNDTR`) 计算写入位置。

```c
uint16_t LoRa_Port_ReceiveData(uint8_t *buf, uint16_t max_len) {
    // 计算 DMA 当前写到了哪里 (总大小 - 剩余传输量)
    uint16_t dma_write_idx = PORT_DMA_RX_BUF_SIZE - DMA_GetCurrDataCounter(DMA1_Channel3);
    uint16_t cnt = 0;

    // 追赶 DMA 的写指针
    while (s_RxReadIndex != dma_write_idx && cnt < max_len) {
        buf[cnt++] = s_DmaRxBuf[s_RxReadIndex++];
        // 处理回绕
        if (s_RxReadIndex >= PORT_DMA_RX_BUF_SIZE) s_RxReadIndex = 0;
    }
    return cnt;
}
```

### 2.4 中断服务函数 (stm32f10x_it.c)
必须处理 DMA 发送完成中断，以释放 `Busy` 标志。

```c
// DMA1 Channel2 (USART3_TX)
void DMA1_Channel2_IRQHandler(void) {
    if (DMA_GetITStatus(DMA1_IT_TC2)) {
        DMA_ClearITPendingBit(DMA1_IT_TC2);
        // 通知 Port 层发送完毕
        // s_TxDmaBusy = false; 
    }
}
```

## 3. 常见坑点 (Pitfalls)
1.  **DMA 中断优先级**: 确保 DMA 中断优先级**高于** SysTick 或其他低优先级业务，但**低于** OSAL 临界区保护（裸机通常无关，但在 RTOS 中要注意）。
2.  **GPIO 模式**:
    *   TX 引脚: `GPIO_Mode_AF_PP` (复用推挽)。
    *   RX 引脚: `GPIO_Mode_IPU` (上拉输入) 或 `IN_FLOATING`。
    *   AUX 引脚: 必须配置为输入，且逻辑电平要查阅模组手册（Ebyte 和 ATK 的忙闲电平逻辑可能相反）。
3.  **Flash 读写**: STM32F103 的 Flash 写入会暂停 CPU 执行。在 `SaveConfig` 回调中操作 Flash 时，协议栈会短暂停止响应，这是正常的。