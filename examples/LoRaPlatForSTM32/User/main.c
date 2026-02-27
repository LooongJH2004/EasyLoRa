/**
  ******************************************************************************
  * @file    main.c
  * @author  LoRaPlat Team
  * @brief   LoRaPlat V3.9.3 综合测试例程 (ID Feedback & Layering Fix)
  ******************************************************************************
  */

#include "stm32f10x.h"
#include "Delay.h"
#include "Serial.h"
#include "LED.h"
#include "Flash.h"
#include "lora_service.h" 
#include "lora_service_command.h" 
// [修改] 移除 lora_manager.h
// #include "lora_manager.h"      
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "lora_port.h"

volatile uint8_t g_TimeoutFlag;
extern void Demo_OSAL_Init(void);

// ============================================================================
// [配置区域]
// ============================================================================
#define DEVICE_ROLE     1 // 1 = STM32 (Master)
#define TARGET_ID       2 // 2 = ESP32 (Slave)
#define DEFAULT_TOKEN   0x00000000

// ============================================================================
// 1. 业务层加密实现 (示例：简单异或)
// ============================================================================
uint16_t App_XOR_Crypt(const uint8_t *in, uint16_t len, uint8_t *out) {
    uint32_t key = LoRa_Service_GetConfig()->token;
    for(int i=0; i<len; i++) {
        out[i] = in[i] ^ (uint8_t)((key >> ((i % 4) * 8)) & 0xFF);
    }
    return len;
}

const LoRa_Cipher_t my_cipher = { .Encrypt = App_XOR_Crypt, .Decrypt = App_XOR_Crypt };

// ============================================================================
// 2. 适配层回调 (Adapter Layer)
// ============================================================================

// [关键] NVS 保存回调
void Adapter_SaveConfig(const LoRa_Config_t *cfg) { 
    Serial_Printf("[NVS] Saving Config to Flash...\r\n");
    Flash_WriteLoRaConfig(cfg); 
}

// [关键] NVS 读取回调
void Adapter_LoadConfig(LoRa_Config_t *cfg) { 
    Flash_ReadLoRaConfig(cfg); 
}

uint32_t Adapter_GetRandomSeed(void) { 
    return LoRa_Port_GetEntropy32(); 
}

void Adapter_SystemReset(void) { 
    Serial_Printf("[SYS] Hard Reset Triggered!\r\n");
    NVIC_SystemReset(); 
}

// [核心] 接收数据回调
void Adapter_OnRecvData(uint16_t src_id, const uint8_t *data, uint16_t len, LoRa_RxMeta_t *meta) {
    // 打印接收到的回传数据
    Serial_Printf("[RX] From 0x%04X (RSSI:%d): %.*s\r\n", src_id, meta->rssi, len, data);
    
    // LED2 翻转表示收到数据
    LED2_Turn();
}

// 事件回调
void Adapter_OnEvent(LoRa_Event_t event, void *arg) {
    switch(event) {
        case LORA_EVENT_INIT_SUCCESS: 
            Serial_Printf("[EVT] LoRa Stack Ready. Role: %d\r\n", DEVICE_ROLE); 
            break;
            
        case LORA_EVENT_TX_SUCCESS_ID:
            // [新增] 打印成功消息 ID
            Serial_Printf("[EVT] Msg ID:%d Sent Success (ACKed).\r\n", *(LoRa_MsgID_t*)arg); 
            LED1_Turn(); 
            break; 
            
        case LORA_EVENT_TX_FAILED_ID:    
            // [新增] 打印失败消息 ID
            Serial_Printf("[EVT] Msg ID:%d Failed (Timeout).\r\n", *(LoRa_MsgID_t*)arg); 
            break;
            
        case LORA_EVENT_BIND_SUCCESS: 
            Serial_Printf("[EVT] Bind ID: %d\r\n", *(uint16_t*)arg); 
            break;
            
        case LORA_EVENT_CONFIG_COMMIT:
            // Service 层已经调用了 SaveConfig，这里只是通知 UI
            Serial_Printf("[EVT] Config Commit Event.\r\n");
            break;
            
        default: break;
    }
}

// 定义回调结构体
const LoRa_Callback_t my_adapter = {
    .SaveConfig = Adapter_SaveConfig, 
    .LoadConfig = Adapter_LoadConfig,
    .GetRandomSeed = Adapter_GetRandomSeed, 
    .SystemReset = Adapter_SystemReset,
    .OnRecvData = Adapter_OnRecvData, 
    .OnEvent = Adapter_OnEvent
};

// ============================================================================
// 3. 辅助函数：首次运行初始化
// ============================================================================
void Check_First_Run(void) {
    LoRa_Config_t cfg;
    Flash_ReadLoRaConfig(&cfg);
    if (cfg.magic != LORA_CFG_MAGIC) {
        Serial_Printf("[SYS] First Run, Writing Defaults...\r\n");
        memset(&cfg, 0, sizeof(cfg));
        cfg.magic = LORA_CFG_MAGIC;
        cfg.net_id = DEVICE_ROLE; 
        cfg.group_id = 100;
        cfg.token = DEFAULT_TOKEN;
        cfg.hw_addr = 0;
        cfg.channel = 23;
        cfg.power = 0; 
        cfg.air_rate = 5; 
        cfg.tmode = 0; 
        Flash_WriteLoRaConfig(&cfg);
        NVIC_SystemReset();
    }
}

// ============================================================================
// 4. 主函数
// ============================================================================
int main(void)
{
    // 硬件初始化
    SysTick_Init();
    LED_Init();
    Serial_Init();
    Demo_OSAL_Init();
    Check_First_Run(); 
    
    // 启动协议栈 (传入回调和 ID)
    LoRa_Service_Init(&my_adapter, DEVICE_ROLE); 
    
    // [修改] 使用 Service 层接口注册加密
    LoRa_Service_RegisterCipher(&my_cipher);

    Serial_Printf("\r\n=== LoRaPlat V3.9.3 ID Feedback Test (ID: %d) ===\r\n", DEVICE_ROLE);
    Serial_Printf("Type ANY text to send (e.g., 'hello')\r\n");
    Serial_Printf("Local Admin: 'CMD:00000000:INFO'\r\n");
    Serial_Printf("Remote OTA: Send 'CMD:00000000:CFG=CH:50' from another device\r\n");

    char input_buf[128];
    char resp_buf[128]; // 用于本地指令回显
    uint32_t last_heartbeat = 0;

    uint32_t last_sleep_log = 0; // [新增] 用于控制休眠日志打印频率

    while (1)
    {
        // ============================================================
        // 1. 协议栈驱动 (核心业务)
        // ============================================================
        // 内部会自动处理接收、状态机流转、超时重传等
        LoRa_Service_Run();

        // ============================================================
        // 2. PC 串口指令处理 (用户交互)
        // ============================================================
        if (Serial_GetRxPacket(input_buf, sizeof(input_buf))) {
            Serial_Printf("[PC] Input: %s\r\n", input_buf);
            
            // --- 本地管理指令 (配置自己) ---
            if (strncmp(input_buf, "CMD:", 4) == 0) {
                if (LoRa_Service_Command_Process(input_buf, resp_buf, sizeof(resp_buf))) {
                    Serial_Printf(" -> CMD Result: %s\r\n", resp_buf);
                } else {
                    Serial_Printf(" -> CMD Ignored (Auth Fail or Format Err)\r\n");
                }
            }
            // --- 通用数据发送 (发送给对端) ---
            else {
                // [关键] 使用 LORA_OPT_CONFIRMED 确保可靠传输
                LoRa_MsgID_t msg_id = LoRa_Service_Send((uint8_t*)input_buf, strlen(input_buf), TARGET_ID, LORA_OPT_CONFIRMED);
                
                if (msg_id > 0) {
                    Serial_Printf(" -> Enqueued ID:%d (Confirmed)...\r\n", msg_id);
                } else {
                    Serial_Printf(" -> Send Failed (Busy)\r\n");
                }
            }
        }

        // ============================================================
        // 3. 状态指示 (心跳)
        // ============================================================
        // 仅当协议栈忙碌时（例如正在重传或等待ACK），LED1 才会快闪
        // 平时 LED1 熄灭，表示系统空闲
        if (LoRa_Service_IsSendingBusy()) {
             if (GetTick() - last_heartbeat > 100) { // 忙碌时快闪
                last_heartbeat = GetTick();
                LED1_Turn(); 
            }
        } else {
            LED1_OFF(); // 空闲时熄灭
        }

        // ============================================================
        // 4. 低功耗休眠尝试 (新增部分)
        // ============================================================
        if (LoRa_Service_CanSleep()) {
            // 这里的 CanSleep 返回 true 意味着：
            // 1. 协议栈没有待发送的数据，也没有在等待 ACK。
            // 2. 物理层没有正在进行的 DMA 传输。
            // 3. 刚刚没有收到新的串口中断数据。

            // [日志控制] 为了防止 SysTick (1ms) 频繁唤醒导致串口刷屏，
            // 我们每隔 5 秒打印一次日志，证明系统正在进入 WFI。
            if (GetTick() - last_sleep_log > 5000) {
                last_sleep_log = GetTick();
                Serial_Printf("[PWR] System Idle, Entering WFI (Sleep)...\r\n");
                
                // 确保串口数据发完再睡，否则最后一个字符可能乱码
                // (假设 Serial_IsBusy 是你串口库提供的函数，如果没有可忽略或加个小延时)
                // while(Serial_IsBusy()); 
            }

            // [核心指令] 等待中断 (Wait For Interrupt)
            // CPU 将停止运行，直到发生中断 (如 SysTick, UART接收, 外部中断)
            // 功耗会从 ~30mA 降至 ~10mA (取决于外设开启情况)
            __WFI();
            
            // 代码执行到这里，说明刚刚发生了中断（被唤醒了）
            // 循环回到开头，LoRa_Service_Run 会处理可能到来的数据
        }
    }
}
