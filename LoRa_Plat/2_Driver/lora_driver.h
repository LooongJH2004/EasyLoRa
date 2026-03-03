/**
  ******************************************************************************
  * @file    lora_driver.h
  * @brief   LoRa 驱动层对外接口 (支持多模组)
  ******************************************************************************
  */

#ifndef __LORA_DRIVER_H
#define __LORA_DRIVER_H

#include "LoRaPlatConfig.h"
#include <stdbool.h>
#include <stdint.h>

// ============================================================
// 模组选择宏 (建议移至 LoRaPlatConfig.h，此处为演示保留)
// ============================================================
// 取消注释以使用 Ebyte 模组，否则默认使用 ATK 模组
// #define LORA_MODULE_EBYTE 

/**
 * @brief  驱动初始化 (阻塞式，包含进入配置模式、发送AT指令、退出配置模式)
 * @param  cfg: 配置参数指针
 * @return true=初始化成功, false=失败
 */
bool LoRa_Driver_Init(const LoRa_Config_t *cfg);

/**
 * @brief  异步发送数据 (非阻塞)
 * @param  data: 数据指针
 * @param  len: 数据长度
 * @return true=已启动DMA, false=忙或错误
 */
bool LoRa_Driver_AsyncSend(const uint8_t *data, uint16_t len);

/**
 * @brief  读取接收数据
 * @param  buf: 目标缓冲区
 * @param  max_len: 最大读取长度
 * @return 实际读取长度
 */
uint16_t LoRa_Driver_Read(uint8_t *buf, uint16_t max_len);

/**
 * @brief  查询驱动是否忙碌 (物理层发送中或AUX指示忙)
 * @return true=忙, false=闲
 */
bool LoRa_Driver_IsBusy(void);

#endif // __LORA_DRIVER_H

