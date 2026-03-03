/**
  ******************************************************************************
  * @file    lora_driver_ops.h
  * @brief   LoRa 驱动底层操作接口定义 (面向对象设计)
  ******************************************************************************
  */

#ifndef __LORA_DRIVER_OPS_H
#define __LORA_DRIVER_OPS_H

#include "LoRaPlatConfig.h"
#include <stdbool.h>

/**
 * @brief 模组驱动操作函数指针结构体
 */
typedef struct {
    /**
     * @brief 进入配置模式
     * @note  需处理硬件引脚拉高/拉低，以及 MCU 串口波特率的切换
     * @return true=成功进入并握手OK, false=失败
     */
    bool (*EnterConfigMode)(void);  
    
    /**
     * @brief 退出配置模式
     * @note  需恢复硬件引脚状态，并将 MCU 串口恢复为业务波特率
     * @return true=成功退出
     */
    bool (*ExitConfigMode)(void);   
    
    /**
     * @brief 应用配置参数
     * @note  将通用 Config 转换为特定模组的 AT 指令并发送
     * @param cfg: 通用配置结构体
     * @return true=配置全部成功, false=部分或全部失败
     */
    bool (*ApplyConfig)(const LoRa_Config_t *cfg);
    
} LoRa_Driver_Ops_t;

#endif // __LORA_DRIVER_OPS_H
