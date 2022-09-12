/*!
 *****************************************************************************
 @file:    AD5940Main.c
 @author:  $Author: nxu2 $
 @brief:   Used to control specific application and process data.
 @version: $Revision: 766 $
 @date:    $Date: 2017-08-21 14:09:35 +0100 (Mon, 21 Aug 2017) $
 -----------------------------------------------------------------------------

Copyright (c) 2017-2019 Analog Devices, Inc. All Rights Reserved.

This software is proprietary to Analog Devices, Inc. and its licensors.
By using this software you agree to the terms of the associated
Analog Devices Software License Agreement.

*****************************************************************************/
#include "SqrWaveVoltammetry.h"

/**
   User could configure following parameters
**/

#define APPBUFF_SIZE 1024
uint32_t AppBuff[APPBUFF_SIZE];
float LFOSCFreq;    /* Measured LFOSC frequency */
double incrementVoltage;
float startVoltage;
static uint32_t rampIndex;

/**
 * @brief An example to deal with data read back from AD5940. Here we just print data to UART
 * @note UART has limited speed, it has impact when sample rate is fast. Try to print some of the data not all of them.
 * @param pData: the buffer stored data for this application. The data from FIFO has been pre-processed.
 * @param DataCount: The available data count in buffer pData.
 * @return return 0.
*/
static int32_t RampShowResult(float *pData, uint32_t DataCount)
{
  static uint32_t index;
  /* Print data*/
  for(int i=0;i<DataCount;i++)
  {
    //printf("%d,%.3f\n", index++, pData[i]); //@annab this is original printf specified in example
		//printf("%d,%.3f\n", rampIndex++, pData[i]);
		printf("%.3f\n", pData[i]); //@anna new statement for labview
    //i += 10;  /* Print though UART consumes too much time. */
  }
	
	//for(int i=0; i<5; i++)
	//{
	//	printf("%d\n",i);
	//}
  return 0;
}

/**
 * @brief The general configuration to AD5940 like FIFO/Sequencer/Clock. 
 * @note This function will firstly reset AD5940 using reset pin.
 * @return return 0.
*/
static int32_t AD5940PlatformCfg(void)
{
  CLKCfg_Type clk_cfg;
  SEQCfg_Type seq_cfg;  
  FIFOCfg_Type fifo_cfg;
  AGPIOCfg_Type gpio_cfg;
  LFOSCMeasure_Type LfoscMeasure;

  /* Use hardware reset */
  AD5940_HWReset();
  AD5940_Initialize();    /* Call this right after AFE reset */
	
  /* Platform configuration */
  /* Step1. Configure clock */
  clk_cfg.HFOSCEn = bTRUE;
  clk_cfg.HFXTALEn = bFALSE;
  clk_cfg.LFOSCEn = bTRUE;
  clk_cfg.HfOSC32MHzMode = bFALSE;
  clk_cfg.SysClkSrc = SYSCLKSRC_HFOSC;
  clk_cfg.SysClkDiv = SYSCLKDIV_1;
  clk_cfg.ADCCLkSrc = ADCCLKSRC_HFOSC;
  clk_cfg.ADCClkDiv = ADCCLKDIV_1;
  AD5940_CLKCfg(&clk_cfg);
  /* Step2. Configure FIFO and Sequencer*/
  fifo_cfg.FIFOEn = bTRUE;           /* We will enable FIFO after all parameters configured */
  fifo_cfg.FIFOMode = FIFOMODE_FIFO;
  fifo_cfg.FIFOSize = FIFOSIZE_2KB;   /* 2kB for FIFO, The reset 4kB for sequencer */
  fifo_cfg.FIFOSrc = FIFOSRC_SINC3;   /* */
  fifo_cfg.FIFOThresh = 4;            /*  Don't care, set it by application paramter */
  AD5940_FIFOCfg(&fifo_cfg);
  seq_cfg.SeqMemSize = SEQMEMSIZE_4KB;  /* 4kB SRAM is used for sequencer, others for data FIFO */
  seq_cfg.SeqBreakEn = bFALSE;
  seq_cfg.SeqIgnoreEn = bTRUE;
  seq_cfg.SeqCntCRCClr = bTRUE;
  seq_cfg.SeqEnable = bFALSE;
  seq_cfg.SeqWrTimer = 0;
  AD5940_SEQCfg(&seq_cfg);
  /* Step3. Interrupt controller */
  AD5940_INTCCfg(AFEINTC_1, AFEINTSRC_ALLINT, bTRUE);   /* Enable all interrupt in INTC1, so we can check INTC flags */
  AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
  AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH|AFEINTSRC_ENDSEQ|AFEINTSRC_CUSTOMINT0, bTRUE); 
  AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
  /* Step4: Configure GPIOs */
  gpio_cfg.FuncSet = GP0_INT|GP1_SLEEP|GP2_SYNC;  /* GPIO1 indicates AFE is in sleep state. GPIO2 indicates ADC is sampling. */
  gpio_cfg.InputEnSet = 0;
  gpio_cfg.OutputEnSet = AGPIO_Pin0|AGPIO_Pin1|AGPIO_Pin2;
  gpio_cfg.OutVal = 0;
  gpio_cfg.PullEnSet = 0;
  AD5940_AGPIOCfg(&gpio_cfg);
  /* Measure LFOSC frequency */
  /**@note Calibrate LFOSC using system clock. The system clock accuracy decides measurement accuracy. Use XTAL to get better result. */
  LfoscMeasure.CalDuration = 1000.0;  /* 1000ms used for calibration. */
  LfoscMeasure.CalSeqAddr = 0;        /* Put sequence commands from start address of SRAM */
  LfoscMeasure.SystemClkFreq = 16000000.0f; /* 16MHz in this firmware. */
  AD5940_LFOSCMeasure(&LfoscMeasure, &LFOSCFreq);
  //printf("LFOSC Freq:%f\n", LFOSCFreq); //@anna commented this out for labview purposes
  AD5940_SleepKeyCtrlS(SLPKEY_UNLOCK);         /*  */
  return 0;
}

/**
 * @brief The interface for user to change application paramters.
 * @return return 0.
*/
void AD5940RampStructInit(void)
{
  AppSWVCfg_Type *pRampCfg;
  
  AppSWVGetCfg(&pRampCfg);
  /* Step1: configure general parmaters */
  pRampCfg->SeqStartAddr = 0x10;                /* leave 16 commands for LFOSC calibration.  */
  pRampCfg->MaxSeqLen = 1024-0x10;              /* 4kB/4 = 1024  */
  pRampCfg->RcalVal = 10000.0;                  /* 10kOhm RCAL */
  pRampCfg->ADCRefVolt = 1820.0f;               /* The real ADC reference voltage. Measure it from capacitor C12 with DMM. */
  pRampCfg->FifoThresh = 480;                   /* Maximum value is 2kB/4-1 = 512-1. Set it to higher value to save power. */
  pRampCfg->SysClkFreq = 16000000.0f;           /* System clock is 16MHz by default */
  pRampCfg->LFOSCClkFreq = LFOSCFreq;           /* LFOSC frequency */
  
	/* Step 2:Configure square wave signal parameters */
  pRampCfg->RampStartVolt = 0.0f;           /* -1.4V */
  pRampCfg->RampPeakVolt = 1200.0f;             /* -0.1V */
	//incrementVoltage = (pRampCfg->RampStartVolt + pRampCfg->RampPeakVolt) / 320; //@anna this is for labview graphing calculations, check 
	//startVoltage = pRampCfg->RampPeakVolt;
  pRampCfg->VzeroStart = 2000.0f;               /* 1.3V */
  pRampCfg->VzeroPeak = 2000.0f;                /* 1.3V */
  pRampCfg->Frequency = 25;                     /* Frequency of square wave in Hz */
  pRampCfg->SqrWvAmplitude = 50;                /* Amplitude of square wave in mV */
  pRampCfg->SqrWvRampIncrement = 5;             /* Increment in mV*/
  pRampCfg->SampleDelay = 10.0f;                /* 10ms. Time between update DAC and ADC sample. Unit is ms. */
  pRampCfg->LPTIARtiaSel = LPTIARTIA_2K;      /* Maximum current decides RTIA value */
	//pRampCfg->AdcPgaGain = ADCPGA_1;          /*PGA gain @Victor */
}

void AD5940_Main(void)
{
  uint32_t temp;  
  AD5940PlatformCfg();
  AD5940RampStructInit();

  AppSWVInit(AppBuff, APPBUFF_SIZE);    /* Initialize RAMP application. Provide a buffer, which is used to store sequencer commands */
  AppSWVCtrl(APPCTRL_START, 0);          /* Control IMP measurement to start. Second parameter has no meaning with this command. */
	//int showResultOnce = 0;
  while(1)
  {
    if(AD5940_GetMCUIntFlag())
    {
			
			//printf("Working\n");//@anna commented this out for labview purposes
      AD5940_ClrMCUIntFlag();
      temp = APPBUFF_SIZE;
      AppSWVISR(AppBuff, &temp);
			RampShowResult((float*)AppBuff, temp);
    }

  }
}

uint32_t swv_stop_sync(uint32_t para1, uint32_t para2)
{
  printf("Stop SYNC SWV measurement right now!!\n");
  AppSWVCtrl(APPCTRL_STOPSYNC, 0);
  return 0;
}


void AD5940RampStructChangeConf(int opt, int param1, int param2)
{
  AppSWVCfg_Type *pRampCfg;
  
  AppSWVGetCfg(&pRampCfg);
 	printf("Changing parameters ...\n");
	/* Step 2:Configure square wave signal parameters */
  
  switch (opt)
  {
  case 0:                                               // HFOSC selected
    pRampCfg->RampStartVolt = (float)param1;           /* -1.4V */
    pRampCfg->RampPeakVolt = (float)param2;             /* -0.1V */
    break;

  case 1:                                               // HFXTAL selected
    pRampCfg->VzeroStart = (float)param1;               /* 1.3V */
    pRampCfg->VzeroPeak = (float)param2;                /* 1.3V */
    break;

  case 2:                                               // SPLL output
    pRampCfg->SqrWvAmplitude = param1;                /* Amplitude of square wave in mV */
    pRampCfg->SqrWvRampIncrement = param2;             /* Increment in mV*/
    break;

  case 3:
    pRampCfg->Frequency = param1;                     /* Frequency of square wave in Hz */
    pRampCfg->SampleDelay = (float)param2;                /* 10ms. Time between update DAC and ADC sample. Unit is ms. */
    break;                                             //clock speed is used, this should be changed

  case 4:
    pRampCfg->LPTIARtiaSel = param1;      /* Maximum current decides RTIA value */
    pRampCfg->RcalVal = 10000.0;                  /* 10kOhm RCAL */
    break;                                             //clock speed is used, this should be changed

  default:
    break;
  }


}

//@anna
//Print measurement parameters
void AD5940RampStructPrintParam(void)
{
  AppSWVCfg_Type *pRampCfg;
 
  AppSWVGetCfg(&pRampCfg);
	printf("RampStartVoltage: %f\n", pRampCfg->RampStartVolt);
  printf("RampPeakVoltage: %f\n", pRampCfg->RampPeakVolt);
	printf("VZeroStart: %f\n", pRampCfg->VzeroStart);
  printf("VZeroPeak: %f\n", pRampCfg->VzeroPeak);
	printf("SqrWvAmplitude: %f\n", pRampCfg->SqrWvAmplitude);
  printf("SqrWaveRampIncrement: %f\n", pRampCfg->SqrWvRampIncrement);
	printf("Frequency: %f\n", pRampCfg->Frequency);
	printf("Sample Delay: %f\n", pRampCfg->SampleDelay);
	printf("LPTIARtiaSel: %d\n", pRampCfg->LPTIARtiaSel);
		
}
uint32_t swv_chg_ramp_s_p(uint32_t para1, uint32_t para2)
{
	int ramp_start=(int)para1; int ramp_peak=(int)para2;
  printf("Change Ramp Start Peak voltages\n");
	printf("para1:0x%08x, para2:0x%08x\n", para1, para2);
	printf("para1:%d, para2:%d\n", (int)para1, (int)para2);
	AD5940RampStructChangeConf(0,ramp_start, ramp_peak);
  return 0;
}

uint32_t swv_chg_vzero_s_p(uint32_t para1, uint32_t para2)
{
	int vzero_start=(int)para1; int vzero_peak=(int)para2;
  printf("Change vzero Start Peak voltages\n");
	printf("para1:0x%08x, para2:0x%08x\n", para1, para2);
	printf("para1:%d, para2:%d\n", (int)para1, (int)para2);
	AD5940RampStructChangeConf(1, vzero_start, vzero_peak);
  return 0;
}

uint32_t swv_chg_sqrwv_a_r(uint32_t para1, uint32_t para2)
{
	int vzero_start=(int)para1; int vzero_peak=(int)para2;
  printf("Change square-wave Amplitude value and Ramp increment\n");
	printf("para1:0x%08x, para2:0x%08x\n", para1, para2);
	printf("para1:%d, para2:%d\n", (int)para1, (int)para2);
	AD5940RampStructChangeConf(2, vzero_start, vzero_peak);
  return 0;
}

uint32_t swv_chg_freq_sdelay(uint32_t para1, uint32_t para2)
{
	int frequency =(int)para1; int sample_delay=(int)para2;
  printf("Change square-wave Frequency and Sample Delay\n");
	printf("para1:0x%08x, para2:0x%08x\n", para1, para2);
	printf("para1:%d, para2:%d\n", (int)para1, (int)para2);
	AD5940RampStructChangeConf(3, frequency, sample_delay);
  return 0;
}

uint32_t swv_chg_ltria_rcal(uint32_t para1, uint32_t para2)
{
	int rcal =(int)para1; int sample_delay=(int)para2;
  printf("Change square-wave LTRIA and Rcal\n");
	printf("para1:0x%08x, para2:0x%08x\n", para1, para2);
	printf("para1:%d, para2:%d\n", (int)para1, (int)para2);
	AD5940RampStructChangeConf(4, rcal, sample_delay);
  return 0;
}


uint32_t swv_start(uint32_t para1, uint32_t para2)
{
  printf("Start SWV measurement\n");
	printf("Running start measurement\n");
	rampIndex = 0;
	//AD5940PlatformCfg();
	AD5940RampStructPrintParam();
	AppSWVInit(AppBuff, APPBUFF_SIZE);    /* Initialize RAMP application. Provide a buffer, which is used to store sequencer commands */
  AppSWVCtrl(APPCTRL_START, 0);
  return 0;
}

