/**
  ******************************************************************************
  * @file    driver_ebyte_e32.c
  * @author  LoRaPlat Team
  * @brief   Ebyte E32 模组驱动实现 (基于新版 AT 固件)
  ******************************************************************************
  */


#include "LoRaPlatConfig.h"


#if (LORA_USE_EBYTE_MODULE == 1)
#include "lora_driver_ops.h"
#include "lora_port.h"
#include "lora_osal.h"
#include "lora_at_command_engine.h"
#include <stdio.h>

// Ebyte 规定：配置模式(模式3)下固定使用 9600, 8N1
#define EBYTE_CONFIG_BAUDRATE 9600

// 辅助：波特率转参数
static int _Ebyte_GetBaudParam(uint32_t baudrate) {
    switch(baudrate) {
        case 1200:   return 0;
        case 2400:   return 1;
        case 4800:   return 2;
        case 9600:   return 3;
        case 19200:  return 4;
        case 38400:  return 5;
        case 57600:  return 6;
        case 115200: return 7;
        default:     return 3; 
    }
}

static bool Ebyte_EnterConfigMode(void) {
    // 1. 拉高 M0, M1 进入休眠/配置模式 (模式3)
    LoRa_Port_SetPin_M0(true);
    LoRa_Port_SetPin_M1(true);
    
    OSAL_DelayMs(100); // 等待模组状态切换
    
    // 2. 强制 MCU 串口切换到 9600
    LoRa_Port_ReInitUart(EBYTE_CONFIG_BAUDRATE);
    OSAL_DelayMs(50); 
    
    // 3. 握手检查 (重试3次)
    for (int i = 0; i < 3; i++) {
        // 注意：Ebyte 新版固件才支持 AT 指令。老固件需发 C3 C3 C3
        if (LoRa_AT_Execute("AT\r\n", "OK", 200) == AT_STATUS_OK) {
            return true;
        }
        OSAL_DelayMs(100);
    }
    return false;
}

static bool Ebyte_ExitConfigMode(void) {
    // 1. 拉低 M0, M1 回到一般模式 (模式0)
    LoRa_Port_SetPin_M0(false);
    LoRa_Port_SetPin_M1(false);
    OSAL_DelayMs(100); 
    
    // 2. 切回目标业务波特率
    LoRa_Port_ReInitUart(LORA_TARGET_BAUDRATE);
    OSAL_DelayMs(100); 
    
    return true;
}

static bool Ebyte_ApplyConfig(const LoRa_Config_t *cfg) {
    char cmd[64];
    bool ok = true;
    
    // 1. 设置地址 (AT+ADDR=1234) - Ebyte 使用十进制
    snprintf(cmd, sizeof(cmd), "AT+ADDR=%d\r\n", cfg->hw_addr);
    if (LoRa_AT_Execute(cmd, "OK", 500) != AT_STATUS_OK) ok = false;
    
    // 2. 设置信道 (AT+CHANNEL=23)
    snprintf(cmd, sizeof(cmd), "AT+CHANNEL=%d\r\n", cfg->channel);
    if (LoRa_AT_Execute(cmd, "OK", 500) != AT_STATUS_OK) ok = false;
    
    // 3. 设置空速 (AT+RATE=2)
    uint8_t rate = (cfg->air_rate > 7) ? 2 : cfg->air_rate; // Ebyte 默认2(2.4k)
    snprintf(cmd, sizeof(cmd), "AT+RATE=%d\r\n", rate);
    if (LoRa_AT_Execute(cmd, "OK", 500) != AT_STATUS_OK) ok = false;

    // 4. 设置功率 (AT+POWER=0)
    uint8_t power = (cfg->power > 3) ? 0 : cfg->power; // Ebyte 默认0(最大功率)
    snprintf(cmd, sizeof(cmd), "AT+POWER=%d\r\n", power);
    if (LoRa_AT_Execute(cmd, "OK", 500) != AT_STATUS_OK) ok = false;
    
    // 5. 设置传输模式 (AT+TRANS=0/1)
    snprintf(cmd, sizeof(cmd), "AT+TRANS=%d\r\n", cfg->tmode);
    if (LoRa_AT_Execute(cmd, "OK", 500) != AT_STATUS_OK) ok = false;

    // 6. 设置波特率 (AT+UART=3,0)
    int baud_param = _Ebyte_GetBaudParam(LORA_TARGET_BAUDRATE);
    snprintf(cmd, sizeof(cmd), "AT+UART=%d,0\r\n", baud_param);
    if (LoRa_AT_Execute(cmd, "OK", 500) != AT_STATUS_OK) ok = false;
    
    // 7. 显式保存参数 (Ebyte 特有)
    // 如果不发此指令，掉电后参数丢失
    // LoRa_AT_Execute("AT+FLASH=1\r\n", "OK", 500);
    
    return ok;
}

// 导出实例
const LoRa_Driver_Ops_t g_DriverOps_Ebyte = {
    .EnterConfigMode = Ebyte_EnterConfigMode,
    .ExitConfigMode  = Ebyte_ExitConfigMode,
    .ApplyConfig     = Ebyte_ApplyConfig
};
#endif
