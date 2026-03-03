/**
  ******************************************************************************
  * @file    driver_atk_lora01.c
  * @author  LoRaPlat Team
  * @brief   ATK-LORA-01 模组驱动实现
  ******************************************************************************
  */

#include "LoRaPlatConfig.h"


#if (LORA_USE_EBYTE_MODULE == 0)
#include "lora_driver_ops.h"
#include "lora_port.h"
#include "lora_osal.h"
#include "lora_at_command_engine.h"
#include <stdio.h>

// ATK 规定：配置模式下固定使用 115200
#define ATK_CONFIG_BAUDRATE 115200

// 辅助：波特率转参数
static int _ATK_GetBaudParam(uint32_t baudrate) {
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

static bool ATK_EnterConfigMode(void) {
    // 1. 拉高 M0(MD0) 进入配置模式
    LoRa_Port_SetPin_M0(true);
    OSAL_DelayMs(600); // ATK 模组进入配置模式需要较长延时
    
    // 2. 强制 MCU 串口切换到 115200
    LoRa_Port_ReInitUart(ATK_CONFIG_BAUDRATE);
    OSAL_DelayMs(100); 
    
    // 3. 握手检查 (重试3次)
    for (int i = 0; i < 3; i++) {
        if (LoRa_AT_Execute("AT\r\n", "OK", 200) == AT_STATUS_OK) {
            return true;
        }
        OSAL_DelayMs(100);
    }
    return false;
}

static bool ATK_ExitConfigMode(void) {
    // 1. 拉低 M0(MD0) 退出配置模式
    LoRa_Port_SetPin_M0(false);
    OSAL_DelayMs(100); 
    
    // 2. 等待模组重启 (等待 AUX 变低再变高)
    uint32_t wait_start = OSAL_GetTick();
    while(!LoRa_Port_GetAUX()) {
         if (OSAL_GetTick() - wait_start > 500) break; 
    }
    wait_start = OSAL_GetTick();
    while(LoRa_Port_GetAUX()) {
        if (OSAL_GetTick() - wait_start > 2000) break;
    }
    
    // 3. 切回目标业务波特率
    LoRa_Port_ReInitUart(LORA_TARGET_BAUDRATE);
    OSAL_DelayMs(100); 
    
    return true;
}

static bool ATK_ApplyConfig(const LoRa_Config_t *cfg) {
    char cmd[64];
    bool ok = true;
    
    // 1. 设置地址 (AT+ADDR=00,12)
    snprintf(cmd, sizeof(cmd), "AT+ADDR=%02X,%02X\r\n", (cfg->hw_addr >> 8) & 0xFF, cfg->hw_addr & 0xFF);
    if (LoRa_AT_Execute(cmd, "OK", 500) != AT_STATUS_OK) ok = false;
    
    // 2. 设置空速和信道 (AT+WLRATE=23,5)
    uint8_t rate = (cfg->air_rate > 5) ? 5 : cfg->air_rate;
    snprintf(cmd, sizeof(cmd), "AT+WLRATE=%d,%d\r\n", cfg->channel, rate);
    if (LoRa_AT_Execute(cmd, "OK", 500) != AT_STATUS_OK) ok = false;
    
    // 3. 设置功率 (AT+TPOWER=3)
    uint8_t power = (cfg->power > 3) ? 3 : cfg->power;
    snprintf(cmd, sizeof(cmd), "AT+TPOWER=%d\r\n", power);
    if (LoRa_AT_Execute(cmd, "OK", 500) != AT_STATUS_OK) ok = false;
    
    // 4. 设置传输模式 (AT+TMODE=0/1)
    const char *mode_cmd = (cfg->tmode == 0) ? "AT+TMODE=0\r\n" : "AT+TMODE=1\r\n";
    if (LoRa_AT_Execute(mode_cmd, "OK", 500) != AT_STATUS_OK) ok = false;

    // 5. 设置波特率 (AT+UART=3,0)
    int baud_param = _ATK_GetBaudParam(LORA_TARGET_BAUDRATE);
    snprintf(cmd, sizeof(cmd), "AT+UART=%d,0\r\n", baud_param);
    if (LoRa_AT_Execute(cmd, "OK", 500) != AT_STATUS_OK) ok = false;
    
    return ok;
}

// 导出实例
const LoRa_Driver_Ops_t g_DriverOps_ATK = {
    .EnterConfigMode = ATK_EnterConfigMode,
    .ExitConfigMode  = ATK_ExitConfigMode,
    .ApplyConfig     = ATK_ApplyConfig
};

#endif
