/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : 主程式核心邏輯 (長按3秒切換Mode 0/1, 終端機30~180 BPM控制)
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>  // 導入標準輸入輸出標頭檔 (使用 sprintf)
#include <string.h> // 導入字串處理標頭檔 (使用 strncmp)
#include <stdlib.h> // 導入標準工具庫 (使用 atoi)
#include <ctype.h>  // 導入字元判斷標頭檔 (使用 isdigit)
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef hlpuart1;

TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;

/* USER CODE BEGIN PV */
// --- 系統模式與狀態全域變數 ---
volatile uint8_t current_mode = 0;   // 0: 節拍器模式, 1: Python 音訊連動模式
volatile uint8_t is_muted = 0;       // 0: 蜂鳴器發聲, 1: 蜂鳴器靜音
volatile uint32_t current_bpm = 120; // 當前節拍速度 (預設 120 BPM)
volatile uint16_t audio_pwm_val = 0; // Mode 1 下由 Python 傳入的 PWM 亮度 (0~999)

// --- PC13 按鈕長按判斷變數 ---
volatile uint32_t btn_press_start = 0; // 記錄按鈕按下的起始時間點 (ms)

// --- LPUART1 接收緩衝區變數 ---
uint8_t rx_byte = 0;          // 儲存中斷每次收到的 1 個 Byte 字元
char rx_buf[64];              // 存放組合後指令字串的緩衝區 (64 Bytes 防止溢位)
volatile uint8_t rx_idx = 0;  // 緩衝區目前的寫入索引位置
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM2_Init(void);
static void MX_TIM3_Init(void);
static void MX_LPUART1_UART_Init(void);
/* USER CODE BEGIN PFP */
void parse_uart_command(char *cmd);     // LPUART1 接收指令解析函數宣告
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  MX_LPUART1_UART_Init();
  /* USER CODE BEGIN 2 */
  // 1. 啟動 TIM2 的通道 1, 2, 3 輸出 PWM (分別連接紅、黃、綠三色 LED)
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1); // 紅色 LED
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2); // 黃色 LED
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3); // 綠色 LED

  // 2. 啟動 TIM3 的通道 3 輸出 PWM (連接無源蜂鳴器)
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
  TIM3->CCR3 = 0; // 開機預設靜音 ( Duty Cycle = 0 )

  // 3. 啟動 LPUART1 接收中斷 (每次接收 1 個 Byte 寫入 rx_byte)
  HAL_UART_Receive_IT(&hlpuart1, &rx_byte, 1);

  // 4. 發送開機歡迎訊息至電腦 Console
  HAL_Delay(100); // 稍微延時等待串口穩定
  HAL_UART_Transmit(&hlpuart1, (uint8_t*)"System Ready! Mode 0: input bpm\r\n", 37, 1000);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    if (current_mode == 0)
    {
      // ===== Mode 0: 節拍器模式 =====
      uint32_t beat_interval = 60000 / current_bpm; // 計算每個節拍的總毫秒數 (60000ms / BPM)

      // 1. 節拍點：LED 瞬間點亮，若非靜音則蜂鳴器發聲
      if (!is_muted)
      {
        TIM3->ARR = 1136;   // 設定頻率約 880Hz (A5 音高)
        TIM3->CCR3 = 568;   // 設定 50% 占空比發聲
      }
      TIM2->CCR1 = 999;     // 紅燈設定為最高亮度
      TIM2->CCR2 = 500;     // 黃燈設定為中等亮度
      TIM2->CCR3 = 200;     // 綠燈設定為低亮度

      HAL_Delay(50);        // 發聲與亮燈維持 50ms

      // 關閉蜂鳴器
      TIM3->CCR3 = 0;       // 停止蜂鳴器發聲

      // 2. 節拍間隔：LED 亮度漸暗演繹
      uint32_t remain_time = beat_interval > 50 ? beat_interval - 50 : 0; // 計算剩下的餘額時間
      uint32_t step_delay = remain_time / 20;                             // 拆分為 20 階步進延時

      // 循環 20 次讓 LED 逐漸變暗
      for (int i = 999; i >= 0 && current_mode == 0; i -= 50)
      {
        TIM2->CCR1 = (i * 999) / 999; // 漸暗紅燈
        TIM2->CCR2 = (i * 500) / 999; // 漸暗黃燈
        TIM2->CCR3 = (i * 200) / 999; // 漸暗綠燈
        HAL_Delay(step_delay);        // 步進延時
      }
    }
    else if (current_mode == 1)
    {
      // ===== Mode 1: Python 音訊燈效模式 =====
      TIM3->CCR3 = 0; // 音訊模式下蜂鳴器保持靜音

      // 即時根據 LPUART 收到的 audio_pwm_val 更新三色 LED 亮度
      TIM2->CCR1 = audio_pwm_val;         // 紅燈直接跟隨音量強度 (0~999)
      TIM2->CCR2 = audio_pwm_val / 2;     // 黃燈縮放半倍亮度
      TIM2->CCR3 = (999 - audio_pwm_val); // 綠燈反態呈現漸變

      HAL_Delay(10); // 10ms 更新頻率，維持視覺順暢
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
  RCC_OscInitStruct.PLL.PLLN = 85;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief LPUART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_LPUART1_UART_Init(void)
{

  /* USER CODE BEGIN LPUART1_Init 0 */

  /* USER CODE END LPUART1_Init 0 */

  /* USER CODE BEGIN LPUART1_Init 1 */

  /* USER CODE END LPUART1_Init 1 */
  hlpuart1.Instance = LPUART1;
  hlpuart1.Init.BaudRate = 115200;
  hlpuart1.Init.WordLength = UART_WORDLENGTH_8B;
  hlpuart1.Init.StopBits = UART_STOPBITS_1;
  hlpuart1.Init.Parity = UART_PARITY_NONE;
  hlpuart1.Init.Mode = UART_MODE_TX_RX;
  hlpuart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  hlpuart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  hlpuart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  hlpuart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&hlpuart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&hlpuart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&hlpuart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&hlpuart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN LPUART1_Init 2 */

  /* USER CODE END LPUART1_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 169;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 19999;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */
  HAL_TIM_MspPostInit(&htim2);

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 169;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 2272;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */
  HAL_TIM_MspPostInit(&htim3);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : PA5 */
  GPIO_InitStruct.Pin = GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
// --- A. LPUART1 指令解析器 ---
void parse_uart_command(char *cmd)
{
  // 濾除開頭可能的無效空格、\r 或 \n
  while(*cmd == ' ' || *cmd == '\r' || *cmd == '\n') cmd++;

  // 1. bpm 指令處理 (支援 "bpm 120" 或舊版 "set bpm 120")
  if (strncmp(cmd, "bpm ", 4) == 0 || strncmp(cmd, "set bpm ", 8) == 0)
  {
    int offset = (cmd[0] == 's') ? 8 : 4;
    int new_bpm = atoi(&cmd[offset]);                 // 將字串轉為整數

    // 嚴格限定範圍為 30 ~ 180 BPM
    if (new_bpm >= 30 && new_bpm <= 180)
    {
      current_bpm = (uint32_t)new_bpm;                // 更新當前 BPM
      char msg[40];
      int len = sprintf(msg, "BPM set to: %u\r\n", (unsigned int)current_bpm);
      HAL_UART_Transmit(&hlpuart1, (uint8_t*)msg, len, 100); // 回送確認訊息
    }
    else
    {
      // 提示超出範圍（無視無效輸入）
      HAL_UART_Transmit(&hlpuart1, (uint8_t*)"Err: BPM must be 30-180\r\n", 25, 100);
    }
  }
  // 2. mute 指令處理 (靜音切換)
  else if (strncmp(cmd, "mute", 4) == 0)
  {
    is_muted = !is_muted;                            // 切換靜音狀態 (0 <-> 1)
    if (is_muted)
      HAL_UART_Transmit(&hlpuart1, (uint8_t*)"Buzzer MUTED\r\n", 14, 100);   // 回報已靜音
    else
      HAL_UART_Transmit(&hlpuart1, (uint8_t*)"Buzzer UNMUTED\r\n", 16, 100); // 回報已取消靜音
  }
  // 3. status / st 指令處理 (查詢狀態)
  else if (strncmp(cmd, "status", 6) == 0 || strncmp(cmd, "st", 2) == 0)
  {
    char stat[90];
    int len = sprintf(stat, "Mode: %d | BPM: %u | Mute: %s | SYSCLK: 170MHz\r\n",
                      (int)current_mode, (unsigned int)current_bpm, is_muted ? "ON" : "OFF");
    HAL_UART_Transmit(&hlpuart1, (uint8_t*)stat, len, 100); // 回送系統狀態字串
  }
  // 4. 純數字指令處理
  else if (isdigit((unsigned char)cmd[0]))
  {
    int val = atoi(cmd);                              // 轉換為整數
    if (current_mode == 0)
    {
      // Mode 0：純數字設定 BPM (嚴格限制 30 ~ 180)
      if (val >= 30 && val <= 180)
      {
        current_bpm = (uint32_t)val;                  // 更新當前 BPM
        char msg[40];
        int len = sprintf(msg, "BPM set to: %u\r\n", (unsigned int)current_bpm);
        HAL_UART_Transmit(&hlpuart1, (uint8_t*)msg, len, 100);
      }
      else
      {
        // 提示超出範圍（無視無效輸入）
        HAL_UART_Transmit(&hlpuart1, (uint8_t*)"Err: BPM must be 30-180\r\n", 25, 100);
      }
    }
    else if (current_mode == 1)
    {
      // Mode 1：純數字作為 Python 音訊 PWM 亮度 (0~999)
      if (val < 0) val = 0;                           // 限位下限 0
      if (val > 999) val = 999;                       // 限位上限 999
      audio_pwm_val = (uint16_t)val;                  // 更新 PWM 數值
    }
  }
}

// --- B. UART 接收完成中斷回調函數 (每次收到 1 Byte 觸發) ---
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == LPUART1)                     // 確認為 LPUART1 中斷
  {
    // 💡 測試燈號：只要進到 RX 中斷，PA5 (LD2 綠燈) 就會翻轉亮滅一次
    HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);

    // 判斷是否收到換行符號 (\r 或 \n)，即代表一條完整指令結束
    if (rx_byte == '\r' || rx_byte == '\n')
    {
      if (rx_idx > 0)                                // 若緩衝區內有字元
      {
        rx_buf[rx_idx] = '\0';                       // 補上 C 語言字串結束符號
        parse_uart_command(rx_buf);                  // 呼叫解析器處理指令
        rx_idx = 0;                                  // 解析完畢，重置緩衝區索引
      }
    }
    else
    {
      if (rx_idx < sizeof(rx_buf) - 1)               // 檢查緩衝區是否還有空間
      {
        rx_buf[rx_idx++] = rx_byte;                  // 寫入緩衝區並遞增索引
      }
      else
      {
        rx_idx = 0;                                  // 超出邊界則自動清空重置
      }
    }

    // 處理完畢後重新開啟 1 Byte 的 LPUART1 接收中斷
    HAL_UART_Receive_IT(&hlpuart1, &rx_byte, 1);
  }
}

// --- C. UART 錯誤處理中斷回調函數 (防止 Overrun 鎖死) ---
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == LPUART1)                     // 確認為 LPUART1 錯誤
  {
    __HAL_UART_CLEAR_OREFLAG(huart);                 // 手動清除 Overrun (溢位) 錯誤旗標
    __HAL_UART_CLEAR_NEFLAG(huart);                  // 手動清除 Noise (雜訊) 錯誤旗標
    __HAL_UART_CLEAR_FEFLAG(huart);                  // 手動清除 Framing (訊框) 錯誤旗標
    rx_idx = 0;                                      // 清空緩衝區指標
    HAL_UART_Receive_IT(&hlpuart1, &rx_byte, 1);     // 強制重啟接收中斷
  }
}

// --- D. PC13 藍色按鈕 (B1) 長按 3 秒切換 Mode (高準位觸發 + 消抖檢測) ---
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == GPIO_PIN_13) // PC13 按鈕按下 (Rising Edge)
  {
    // 消抖延時：等待 50ms 避開接點彈跳
    HAL_Delay(50);

    // 確認按鈕此時處於按下狀態 (高準位 GPIO_PIN_SET)
    if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_SET)
    {
      uint32_t press_time = 0;

      // 當按鈕持續被按住 (High) 且時間小於 3 秒 (3000ms) 時進行迴圈等待
      while (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_SET && press_time < 1500)
      {
        HAL_Delay(10);
        press_time += 10;
      }

      // 若按住時間達到或超過 3000ms (3秒)
      if (press_time >= 1500)
      {
        current_mode = !current_mode; // 切換 Mode 0 <-> Mode 1

        char mode_msg[60];
        int len = sprintf(mode_msg, "\r\n>>> Switched to Mode %d (%s) <<<\r\n",
                          current_mode, current_mode == 1 ? "Python Audio LED" : "Rhythm Coach");
        HAL_UART_Transmit(&hlpuart1, (uint8_t*)mode_msg, len, 100);

        // 重置 Mode 1 相關 PWM 與狀態
        TIM3->CCR3 = 0;
        audio_pwm_val = 0;
      }
    }
  }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq(); // 關閉所有中斷
  while (1)
  {
    // 卡死在死迴圈，等待除錯
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
