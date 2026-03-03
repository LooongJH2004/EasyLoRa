/**
  ******************************************************************************
  * @file    lora_driver.c
  * @brief   LoRa 驱动核心调度器 (策略层)
  ******************************************************************************
  */


#include "lora_driver.h"
#include "lora_driver_ops.h"
#include "lora_port.h"
#include "lora_at_command_engine.h"
#include "lora_osal.h"
#include "LoRaPlatConfig.h"

// 声明外部的具体模组实现实例
extern const LoRa_Driver_Ops_t g_DriverOps_ATK;
extern const LoRa_Driver_Ops_t g_DriverOps_Ebyte;

// 根据 Config.h 中的宏选择
#if (LORA_USE_EBYTE_MODULE == 1)
    static const LoRa_Driver_Ops_t *s_Ops = &g_DriverOps_Ebyte;
#else
    static const LoRa_Driver_Ops_t *s_Ops = &g_DriverOps_ATK;
#endif

bool LoRa_Driver_Init(const LoRa_Config_t *cfg) {
    LORA_CHECK(cfg != NULL, false);
    LORA_CHECK(s_Ops != NULL, false);

    // 1. 端口底层初始化 (初始波特率无所谓，EnterConfigMode 会重置)
    LoRa_Port_Init(9600); 
    LoRa_AT_Init();
    
    LORA_LOG("[DRV] Init Start. Target Baud: %d\r\n", LORA_TARGET_BAUDRATE);

    // 2. 进入配置模式 (多态调用，处理引脚和波特率差异)
    if (!s_Ops->EnterConfigMode()) {
        LORA_LOG("[DRV] Enter Config Mode Failed!\r\n");
        // 尝试恢复业务波特率以防死锁
        LoRa_Port_ReInitUart(LORA_TARGET_BAUDRATE);
        return false;
    }
    
    LORA_LOG("[DRV] Handshake OK, Applying Config...\r\n");

    // 3. 应用配置参数 (多态调用，发送特定 AT 指令)
    bool cfg_ok = s_Ops->ApplyConfig(cfg);
    if (!cfg_ok) {
        LORA_LOG("[DRV] Apply Config Failed!\r\n");
    }

    // 4. 退出配置模式 (多态调用，恢复引脚和波特率)
    s_Ops->ExitConfigMode();
    
    LORA_LOG("[DRV] Exited Config Mode.\r\n");
    
    // 5. 清理残留状态
    LoRa_Port_SyncAuxState();
    LoRa_Port_ClearRxBuffer();
    
    return cfg_ok;
}

bool LoRa_Driver_AsyncSend(const uint8_t *data, uint16_t len) {
    // 1. 检查 AUX 忙闲状态
    if (LoRa_Port_GetAUX()) return false;
    
    // 2. 启动 DMA 发送
    return (LoRa_Port_TransmitData(data, len) > 0);
}

uint16_t LoRa_Driver_Read(uint8_t *buf, uint16_t max_len) {
    return LoRa_Port_ReceiveData(buf, max_len);
}

bool LoRa_Driver_IsBusy(void) {
    return LoRa_Port_GetAUX() || LoRa_Port_IsTxBusy();
}
