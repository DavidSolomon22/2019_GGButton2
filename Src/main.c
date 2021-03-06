
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  ** This notice applies to any and all portions of this file
  * that are not between comment pairs USER CODE BEGIN and
  * USER CODE END. Other portions of this file, whether 
  * inserted by the user or by software development tools
  * are owned by their respective copyright owners.
  *
  * COPYRIGHT(c) 2019 STMicroelectronics
  *
  * Redistribution and use in source and binary forms, with or without modification,
  * are permitted provided that the following conditions are met:
  *   1. Redistributions of source code must retain the above copyright notice,
  *      this list of conditions and the following disclaimer.
  *   2. Redistributions in binary form must reproduce the above copyright notice,
  *      this list of conditions and the following disclaimer in the documentation
  *      and/or other materials provided with the distribution.
  *   3. Neither the name of STMicroelectronics nor the names of its contributors
  *      may be used to endorse or promote products derived from this software
  *      without specific prior written permission.
  *
  * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  *
  ******************************************************************************
  */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f4xx_hal.h"

/* USER CODE BEGIN Includes */
#include <string.h>
#include <math.h>
#include "ff.h"
#include "MY_CS43L22.h"
#include "STM_MY_LCD16X2.h"

#define  	FA_READ         	0x01
/* USER CODE END Includes */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

DAC_HandleTypeDef hdac;

I2C_HandleTypeDef hi2c1;

I2S_HandleTypeDef hi2s3;
DMA_HandleTypeDef hdma_spi3_tx;

SPI_HandleTypeDef hspi1;

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2;

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/


// Sound variables
uint8_t wavBuffer1[512];			// first buffer for storing and playing music
uint8_t wavBuffer2[512];			// second buffer for storing and playing music

int flag1 = 0;
int flag2 = 0;

int c = 0;							// counter

// FatFS variables
static FATFS FatFs;    				// handle to FatFs device like disc or SD card
FRESULT fresult;	       			// to storage results of operations on FatFs library
FIL file;                  			// handle to open file
DWORD bytes_read;           		// number of read bytes
FSIZE_t ofs = 0;					// file offset

// Encoder variables
volatile uint16_t pulse_count;		// impulse counter
volatile uint16_t position;			// counter of switched positions
uint16_t posBuffer = 1;
uint16_t timPosBuffer = 1;

// Potentiometer variables
uint16_t potentiometerValue;
char volumeLVL[15];
int volumeHasChanged;

// Listing/parsing variables
char song_list[128][128];			// [MAX_NUMBER_STRINGS][MAX_STRING_SIZE] songs names with .WAV file extension
char pure_song_list[128][128];		// songs names without .WAV file extension (for displaying purposes)
uint32_t data_size[128];
int lseek_starts[128];
int number_of_songs = 0;			// number of songs on device

// Button variables
int start_stop = 1;					// flag for starting and stopping player


/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_I2C1_Init(void);
static void MX_I2S3_Init(void);
static void MX_DAC_Init(void);
static void MX_TIM2_Init(void);
static void MX_SPI1_Init(void);
static void MX_TIM1_Init(void);
static void MX_ADC1_Init(void);
void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);
                                
/* USER CODE BEGIN PFP */
/* Private function prototypes -----------------------------------------------*/

// External interrupt for start/stop button
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
	if(HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0) == GPIO_PIN_SET)
	{
		for(volatile int i = 0; i < 2000000; i++){}			// delay for proper start and stop operation
		if(start_stop == 1)
		{
			start_stop = 0;
			NVIC_EnableIRQ(TIM2_IRQn);						// enable timer
		}
		else
		{
			start_stop = 1;
			NVIC_DisableIRQ(TIM2_IRQn);						// disable timer
		}
	}
}

/* USER CODE END PFP */

/* USER CODE BEGIN 0 */

// Listing function - writes every song name with .WAV extension to song_list[] array
FRESULT list_files(char* path)
{
	FRESULT res;
	DIR dir;
	FILINFO fno;

	res = f_opendir(&dir, path);							// open the directory
	if(res == FR_OK)
	{
		for(int i = 0; i < 256; i++)
		{
			res = f_readdir(&dir, &fno);					// read a directory item
			if(res != FR_OK || fno.fname[0] == 0) break;	// break on error or end of directory
			strcpy(song_list[i], fno.fname);
			number_of_songs++;
		}
		number_of_songs--;
		f_closedir(&dir);									// close the directory
	}
	return res;
}

// Function that modifies names of songs (cut .WAV extension) and writes them into pure_song_list[] array
void clear_song_names()
{
	for(int i = 1; i <= number_of_songs; i++)
	{
		int x = 0;

		while(1)
		{
			if(song_list[i][x] == '.' && song_list[i][x+1] == 'W' && song_list[i][x+2] == 'A' && song_list[i][x+3] == 'V')
			{
				pure_song_list[i][x] = '\0';
				pure_song_list[i][x+1] = '\0';
				pure_song_list[i][x+2] = '\0';
				pure_song_list[i][x+3] = '\0';
				break;
			}
			pure_song_list[i][x] = song_list[i][x];
			x++;
		}
	}
}

// Parsing function - writes every size of song into data_size[] array
void parse_headers()
{
	FIL fil;
	unsigned char buffer4[4];
	int x = 0;

	for(int i = 1; i <= number_of_songs; i++)
	{
		f_open(&fil, song_list[i] , FA_READ);
		x = 0;

		while(1)
		{
			f_lseek(&fil, x);
			f_read(&fil, buffer4, (FSIZE_t)4, &bytes_read);

			if(buffer4[0] == 'd' && buffer4[1] == 'a' && buffer4[2] == 't' && buffer4[3] == 'a')
			{
				lseek_starts[i] = x + 8;
				f_lseek(&fil, x + 4);
				f_read(&fil, buffer4, (FSIZE_t)4, &bytes_read);
				data_size[i] = buffer4[0] |
						(buffer4[1] << 8) |
						(buffer4[2] << 16) |
						(buffer4[3] << 24 );
				break;
			}
			x++;
		}
		f_close(&fil);
	}
}


void display()
{
	LCD1602_clear();
	LCD1602_print(pure_song_list[position]);
	LCD1602_2ndLine();
	LCD1602_print(volumeLVL);
}


int volumeChange(int x) //volume changing function
{
	int times; // variable which counts how many times
	int value = 0;
	if(x < 100)
	{
		CS43_Stop();

		for(int i = 0; i < 16; i++)
		{
			volumeLVL[i] = '\0'; // allows to "show nothing " on the LCD screen
		}
		if(volumeHasChanged != value) // checking if the volumeLVL has changed since a milisecind ago,
		{							// if so display funntion is executed to display changes on the screen
			display();
		}
		volumeHasChanged=value; // saving current volumneLVL

	}
	else
	{
		CS43_Start();
		value = x / 82;
		times = value / 3;
		for(int i = 0; i < 16; i++)
		{
			if(times >= i)
			{
				volumeLVL[i]='|';
			}
			else
			{
				volumeLVL[i]='\0';
			}
		}

		if(volumeHasChanged != value)
		{
			display();
		}
		volumeHasChanged=value;

	}
	return value;

}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  *
  * @retval None
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration----------------------------------------------------------*/

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
  MX_DMA_Init();
  MX_I2C1_Init();
  MX_I2S3_Init();
  MX_DAC_Init();
  MX_TIM2_Init();
  MX_SPI1_Init();
  MX_TIM1_Init();
  MX_ADC1_Init();
  /* USER CODE BEGIN 2 */

  // Timer is disabled because GGbutton doesn't play music at the beginning
  NVIC_DisableIRQ(TIM2_IRQn);


  // Operations like listing and parsing on microSD card
  fresult = f_mount(&FatFs, "", 0);

  if (fresult == FR_OK)
  {
	  fresult = list_files("/");
	  if (fresult == FR_OK)
	  {
		  clear_song_names();
		  parse_headers();
	  }
  }

  fresult = f_open(&file, song_list[1] , FA_READ);		// open file with first song


  // Encoder
  HAL_TIM_Encoder_Start(&htim1, TIM_CHANNEL_ALL);		// start timer encoder
  TIM1->ARR = number_of_songs * 4 - 1;					// calculating reload register


  // LCD display
  LCD1602_Begin4BIT(RS_GPIO_Port,RS_Pin,E_Pin,D4_GPIO_Port,D4_Pin,D5_Pin,D6_Pin,D7_Pin);
  LCD1602_clear();
  LCD1602_print(pure_song_list[position + 1]);
  LCD1602_noCursor();
  LCD1602_noBlink();
  LCD1602_2ndLine();
  LCD1602_print(volumeLVL);

  // Sound
  CS43_Init(hi2c1, MODE_ANALOG); 						// CS43 interface initialization
  CS43_Enable_RightLeft(CS43_RIGHT_LEFT);				// enable two channels
  CS43_Start();											// start CS43
  HAL_I2S_Transmit_DMA(&hi2s3, (uint16_t*)" ", 1);
  HAL_DAC_Start(&hdac, DAC_CHANNEL_1); 					// start DAC
//  HAL_DAC_Start(&hdac, DAC_CHANNEL_2); 				// TODO: try to play stereo music
  HAL_TIM_Base_Start_IT(&htim2); 						// starting "main" timer


  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  HAL_ADC_Start(&hadc1);							//start potentiometer checking

	  pulse_count = TIM1->CNT + 4;
	  position = pulse_count/4;							// transform pulse_count to consecutive natural numbers

	  // read song samples and write them to wavBuffer1
	  if(flag1 == 0 && flag2 == 1)
	  {
		  fresult = f_read(&file, wavBuffer1, (FSIZE_t)512, &bytes_read);
		  flag1 = 1;
	  }

	  // read song samples and write them to wavBuffer2
	  else if(flag1 == 1 && flag2 == 0)
	  {
		  fresult = f_read(&file, wavBuffer2, (FSIZE_t)512, &bytes_read);
		  flag1 = 0;
	  }

	  // switching songs
	  if(posBuffer != position)
	  {
		  fresult = f_close(&file);
		  fresult = f_open(&file, song_list[position] , FA_READ);
		  display(); 									// after switching song, display have to change
		  posBuffer = position;
		  start_stop = 1;
		  NVIC_DisableIRQ(TIM2_IRQn);					// stop playing after switching song
	  }

	  if(HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK)	// function which checks the volumeLVL on potentiometer
	  {
		  potentiometerValue = HAL_ADC_GetValue(&hadc1);
	  }

	  CS43_SetVolume(volumeChange(potentiometerValue));	// volume setting

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

  RCC_OscInitTypeDef RCC_OscInitStruct;
  RCC_ClkInitTypeDef RCC_ClkInitStruct;
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct;

    /**Configure the main internal regulator output voltage 
    */
  __HAL_RCC_PWR_CLK_ENABLE();

  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    /**Initializes the CPU, AHB and APB busses clocks 
    */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

    /**Initializes the CPU, AHB and APB busses clocks 
    */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_I2S;
  PeriphClkInitStruct.PLLI2S.PLLI2SN = 50;
  PeriphClkInitStruct.PLLI2S.PLLI2SR = 2;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

    /**Configure the Systick interrupt time 
    */
  HAL_SYSTICK_Config(HAL_RCC_GetHCLKFreq()/1000);

    /**Configure the Systick 
    */
  HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);

  /* SysTick_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);
}

/* ADC1 init function */
static void MX_ADC1_Init(void)
{

  ADC_ChannelConfTypeDef sConfig;

    /**Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion) 
    */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

    /**Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time. 
    */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

}

/* DAC init function */
static void MX_DAC_Init(void)
{

  DAC_ChannelConfTypeDef sConfig;

    /**DAC Initialization 
    */
  hdac.Instance = DAC;
  if (HAL_DAC_Init(&hdac) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

    /**DAC channel OUT1 config 
    */
  sConfig.DAC_Trigger = DAC_TRIGGER_NONE;
  sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
  if (HAL_DAC_ConfigChannel(&hdac, &sConfig, DAC_CHANNEL_1) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

}

/* I2C1 init function */
static void MX_I2C1_Init(void)
{

  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

}

/* I2S3 init function */
static void MX_I2S3_Init(void)
{

  hi2s3.Instance = SPI3;
  hi2s3.Init.Mode = I2S_MODE_MASTER_TX;
  hi2s3.Init.Standard = I2S_STANDARD_PHILIPS;
  hi2s3.Init.DataFormat = I2S_DATAFORMAT_16B;
  hi2s3.Init.MCLKOutput = I2S_MCLKOUTPUT_ENABLE;
  hi2s3.Init.AudioFreq = I2S_AUDIOFREQ_48K;
  hi2s3.Init.CPOL = I2S_CPOL_LOW;
  hi2s3.Init.ClockSource = I2S_CLOCK_PLL;
  hi2s3.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_DISABLE;
  if (HAL_I2S_Init(&hi2s3) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

}

/* SPI1 init function */
static void MX_SPI1_Init(void)
{

  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

}

/* TIM1 init function */
static void MX_TIM1_Init(void)
{

  TIM_Encoder_InitTypeDef sConfig;
  TIM_MasterConfigTypeDef sMasterConfig;

  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 0;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 0;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  sConfig.EncoderMode = TIM_ENCODERMODE_TI12;
  sConfig.IC1Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC1Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC1Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC1Filter = 15;
  sConfig.IC2Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC2Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC2Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC2Filter = 15;
  if (HAL_TIM_Encoder_Init(&htim1, &sConfig) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

}

/* TIM2 init function */
static void MX_TIM2_Init(void)
{

  TIM_ClockConfigTypeDef sClockSourceConfig;
  TIM_MasterConfigTypeDef sMasterConfig;
  TIM_OC_InitTypeDef sConfigOC;

  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 249;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 20;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

  sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 9;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

  HAL_TIM_MspPostInit(&htim2);

}

/** 
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void) 
{
  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Stream5_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);

}

/** Configure pins as 
        * Analog 
        * Input 
        * Output
        * EVENT_OUT
        * EXTI
*/
static void MX_GPIO_Init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct;

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, D4_Pin|D5_Pin|D6_Pin|D7_Pin 
                          |GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15 
                          |GPIO_PIN_4, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOE, RS_Pin|E_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : PB0 */
  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : D4_Pin D5_Pin D6_Pin D7_Pin 
                           PD12 PD13 PD14 PD15 
                           PD4 */
  GPIO_InitStruct.Pin = D4_Pin|D5_Pin|D6_Pin|D7_Pin 
                          |GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15 
                          |GPIO_PIN_4;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pins : RS_Pin E_Pin */
  GPIO_InitStruct.Pin = RS_Pin|E_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);

}

/* USER CODE BEGIN 4 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
	/* Prevent unused argument(s) compilation warning */
	UNUSED(htim);
	/* NOTE : This function Should not be modified, when the callback is needed,
            the __HAL_TIM_PeriodElapsedCallback could be implemented in the user file
	 */

	if(htim->Instance == TIM2)
	{
		if(flag2 == 0)
		{
			HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_8B_R, (uint32_t)wavBuffer1[c]);
//			HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_8B_R, 0);		// TODO: try to play stereo music
			c++;

			// If 512 bytes played from wavBuffer1, change flag2 to write (in main)
			// next 512 bytes into wavBuffer1 and start playing samples from wavBuffer1
			if(c == 512)
			{
				c = 0;
				flag2 = 1;

				if(timPosBuffer != posBuffer)			// if song was switched
				{
					ofs = 0;
					timPosBuffer = posBuffer;
				}
				ofs = ofs + (FSIZE_t)512;

				if(ofs > data_size[position])			// end of song
				{
					ofs = 0;
					fresult = f_lseek(&file, ofs);
					start_stop = 1;
					NVIC_DisableIRQ(TIM2_IRQn);
				}
				fresult = f_lseek(&file, ofs);
			}
		}

		else if(flag2 == 1)
		{
			HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_8B_R, (uint32_t)wavBuffer2[c]);
//			HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_8B_R, 0);		// TODO: try to play stereo music
			c++;

			// If 512 bytes played from wavBuffer2, change flag2 to write (in main)
			// next 512 bytes into wavBuffer2 and start playing samples from wavBuffer1
			if(c == 512)
			{
				c = 0;
				flag2 = 0;

				if(timPosBuffer != posBuffer)			// if song was switched
				{
					ofs = 0;
					timPosBuffer = posBuffer;
				}
				ofs = ofs + (FSIZE_t)512;

				if(ofs > data_size[position])			// end of song
				{
					ofs = 0;
					fresult = f_lseek(&file, ofs);
					start_stop = 1;
					NVIC_DisableIRQ(TIM2_IRQn);
				}
				fresult = f_lseek(&file, ofs);
			}
		}
	}
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @param  file: The file name as string.
  * @param  line: The line in file as a number.
  * @retval None
  */
void _Error_Handler(char *file, int line)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  while(1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t* file, uint32_t line)
{ 
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     tex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/**
  * @}
  */

/**
  * @}
  */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
