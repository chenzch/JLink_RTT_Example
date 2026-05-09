/*==================================================================================================
* Project : RTD AUTOSAR 4.9
* Platform : CORTEXM
* Peripheral : S32K3XX
* Dependencies : none
*
* Autosar Version : 4.9.0
* Autosar Revision : ASR_REL_4_9_REV_0000
* Autosar Conf.Variant :
* SW Version : 7.0.1
* Build Version : S32K3_RTD_7_0_1_D2602_ASR_REL_4_9_REV_0000_20260206
*
* Copyright 2020 - 2026 NXP
*
* NXP Confidential and Proprietary. This software is owned or controlled by NXP and may only be
*   used strictly in accordance with the applicable license terms. By expressly
*   accepting such terms or by downloading, installing, activating and/or otherwise
*   using the software, you are agreeing that you have read, and that you agree to
*   comply with and are bound by, such license terms. If you do not agree to be
*   bound by the applicable license terms, then you may not retain, install,
*   activate or otherwise use the software.
==================================================================================================*/

/**
*   @file main.c
*
*   @addtogroup main_module main module documentation
*   @{
*/

#ifdef __cplusplus
extern "C"{
#endif

/* Including necessary configuration files. */
#include "Mcal.h"
#include "OsIf.h"
/* User includes */
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include "SEGGER_RTT.h"

float gResult[2];

static void delay(void) {
	uint32_t Count = 48000;
	while (--Count);
}

/*!
  \brief The main function for the project.
  \details The startup initialization sequence is the following:
 * - startup asm routine
 * - main()
*/
int main(void)
{
    /* Write your code here */
	OsIf_SuspendAllInterrupts();

	OsIf_Init(NULL_PTR);

	OsIf_ResumeAllInterrupts();

	SEGGER_RTT_ConfigUpBuffer();

	char end[4] = {0x00, 0x00, 0x80, 0x7f};
	float endval = *((float*)&end[0]);
	gResult[1] = endval;
    for(;;)
    {
    	for (uint32_t degree = 0; degree < 360; ++degree) {
    		gResult[0] = sinf((float)degree / 180.0f * (float)3.14159265358979323846);
			SEGGER_RTT_Write(&gResult[0], sizeof(gResult));
    		delay();
    	}
    }
}

#ifdef __cplusplus
}
#endif

/** @} */
