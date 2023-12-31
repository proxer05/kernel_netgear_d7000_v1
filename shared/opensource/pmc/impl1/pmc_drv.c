/*
<:copyright-BRCM:2013:DUAL/GPL:standard

   Copyright (c) 2013 Broadcom Corporation
   All Rights Reserved

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License, version 2, as published by
the Free Software Foundation (the "GPL").

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.


A copy of the GPL is available at http://www.broadcom.com/licenses/GPLv2.php, or by
writing to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.

:>
*/

/*****************************************************************************
 *  Description:
 *      This contains the PMC driver.
 *****************************************************************************/

#include "pmc_drv.h"
#include "bcm_map_part.h"

#define PMC_MODE_DQM		0
#define PMC_MODE_PMB_DIRECT	1

static int pmc_mode = PMC_MODE_DQM;

#ifdef _CFE_
#include "lib_printf.h"
#define printk	printf
#else
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/module.h>
static DEFINE_SPINLOCK(lock);
#endif

#include "command.h"
#include "BPCM.h"
#include "shared_utils.h"

#if defined CONFIG_BCM963138 || defined _BCM963381_
/* translate new cmdID into old cmdID that the pmc will understand
 * NB: requires cmdIDs below to be the new versions
 */
static const unsigned char newToOldcmdIDMap[] = {
	[cmdSetRunState]	=  64,	// cmdSetRunState,
	[cmdSetPowerState]	=  65,	// cmdSetPowerState,
	[cmdShutdownAllowed]	=  66,	// cmdShutdownAllowed,
	[cmdGetSelect0]		=  67,	// cmdGetSelect0,
	[cmdGetSelect3]		=  68,	// cmdGetSelect3,
	[cmdGetAvsDisableState]	=  69,	// cmdGetAvsDisableState,
	[cmdGetPVT]		=  70,	// cmdGetPVT,
	[cmdPowerDevOnOff]	= 129,	// cmdPowerDevOnOff,
	[cmdPowerZoneOnOff]	= 130,	// cmdPowerZoneOnOff,
	[cmdResetDevice]	= 131,	// cmdResetDevice,
	[cmdResetZone]		= 132,	// cmdResetZone,
	[cmdAllocateG2UDQM]	= 133,	// cmdAllocateG2UDQM,
	[cmdQSMAvailable]	= 134,	// cmdQSMAvailable,
	[cmdRevision]		= 135,	// cmdRevision,
};

static int pmc_remap = 0;
#endif

#define CHECK_DEV_ADDRESSABLE(devAddr)   \
      if ( !deviceAddressable(devAddr) ) \
         return kPMC_INVALID_DEVICE;

static bool deviceAddressable( int devAddr )
{
	switch( devAddr )
   {
#if defined(CONFIG_BCM96838)
      case PMB_ADDR_APM:
      {
         if(UtilGetChipRev() <= 1)
         {
            return FALSE;
         }
      }
      break;
#endif /* defined(CONFIG_BCM96838) */

      default:
      break;
   }
   return TRUE;
}

static int SendAndWait(TCommand *cmd, TCommand *rsp)
{
	static uint32 reqdID = 1;
	int status = kPMC_COMMAND_TIMEOUT;
	TCommand dummy;

#ifndef _CFE_
	unsigned long flags;

	/* do we want to use spin_lock_irqsave here? what if it takes a while
	 * to get unlock, then all the irq are disabled until then. */
	spin_lock_irqsave(&lock, flags);
#endif

#if defined CONFIG_BCM963138 || defined _BCM963381_
	if (pmc_remap && cmd->word0.Bits.cmdID < sizeof newToOldcmdIDMap &&
	    newToOldcmdIDMap[cmd->word0.Bits.cmdID])
		cmd->word0.Bits.cmdID = newToOldcmdIDMap[cmd->word0.Bits.cmdID];
#endif

	cmd->word0.Bits.msgID = reqdID;

	/* send the command */
	PMC->dqmQData[PMC_DQM_REQ_NUM].word[0] = cmd->word0.Reg32;
	PMC->dqmQData[PMC_DQM_REQ_NUM].word[1] = cmd->word1.Reg32;
	PMC->dqmQData[PMC_DQM_REQ_NUM].word[2] = cmd->u.cmdGenericParams.params[0];
	PMC->dqmQData[PMC_DQM_REQ_NUM].word[3] = cmd->u.cmdGenericParams.params[1];

	/* wait for no more than 5ms for the reply */
#ifdef CONFIG_BRCM_IKOS
	/* We do not enable PMC TIMER here for IKOS, or it will wait forever */
	while (!(PMC->dqm.notEmptySts & PMC_DQM_RPL_STS));
#else
	// XXX repeating timer never times out
	PMC->ctrl.gpTmr2Ctl = ((1 << 31) | (1 << 30) | (1 << 29) | 5000);

	while (!(PMC->dqm.notEmptySts & PMC_DQM_RPL_STS) &&
			(PMC->ctrl.gpTmr2Ctl & (1 << 31)));
#endif

	if (PMC->dqm.notEmptySts & PMC_DQM_RPL_STS) {
		if (!rsp)
			rsp = &dummy;

		/* command didn't timeout, fill in the response */
		rsp->word0.Reg32 = PMC->dqmQData[PMC_DQM_RPL_NUM].word[0];
		rsp->word1.Reg32 = PMC->dqmQData[PMC_DQM_RPL_NUM].word[1];
		rsp->u.cmdGenericParams.params[0] = PMC->dqmQData[PMC_DQM_RPL_NUM].word[2];
		rsp->u.cmdGenericParams.params[1] = PMC->dqmQData[PMC_DQM_RPL_NUM].word[3];


		if (rsp->word0.Bits.msgID == reqdID)
			status = rsp->word0.Bits.error;
		else
			status = kPMC_MESSAGE_ID_MISMATCH;
	}

	reqdID = (reqdID + 1) & 0xff;

#ifndef _CFE_
	spin_unlock_irqrestore(&lock, flags);
#endif

	return status;
}

static int SendCmd(TCommand *cmd, int cmdID, int devAddr, int zone, TCommand *rsp)
{
	cmd->word0.Reg32 = 0;
	cmd->word0.Bits.cmdID = cmdID;
	cmd->word1.Reg32 = 0;
	cmd->word1.Bits.devAddr = devAddr;
	cmd->word1.Bits.zoneIdx = zone;

	return SendAndWait(cmd, rsp);
}

static int SendCommand(int cmdID, int devAddr, int zone, uint32 word2,
		uint32 word3, TCommand *rsp)
{
	TCommand cmd;

	cmd.u.cmdGenericParams.params[0] = word2;
	cmd.u.cmdGenericParams.params[1] = word3;

	return SendCmd(&cmd, cmdID, devAddr, zone, rsp);
}

static int SendStateCommand(int cmdID, int island, int state)
{
	TCommand cmd;
	int status;

	if ((unsigned)island > 2)
		status = kPMC_INVALID_ISLAND;
	else if ((unsigned)state > 15)
		status = kPMC_INVALID_STATE;
	else {
		cmd.word0.Reg32 = 0;
		cmd.word0.Bits.cmdID = cmdID;
		cmd.word1.Reg32 = 0;
		cmd.word1.Bits.island = island;
		cmd.u.cmdStateOnlyParam.state = state;
		status = SendAndWait(&cmd, 0);
	}

	return status;
}

#ifndef _CFE_
int GetDevPresence(int devAddr, int *value)
{
	CHECK_DEV_ADDRESSABLE(devAddr);

	if (pmc_mode == PMC_MODE_DQM) {
		TCommand rsp;
		int status = SendCommand(cmdGetDevPresence, devAddr, 0, 0, 0, &rsp);

		if (status == kPMC_NO_ERROR)
			*value = (rsp.u.cmdResponse.word2 & 1) ? TRUE : FALSE;
		else
			*value = FALSE;

		return status;
	} else
		return kPMC_INVALID_COMMAND;
}

int GetSWStrap(int devAddr, int *value)
{
	CHECK_DEV_ADDRESSABLE(devAddr);

	if (pmc_mode == PMC_MODE_DQM) {
		TCommand rsp;
		int status = SendCommand(cmdGetSWStrap, devAddr, 0, 0, 0, &rsp);

		if (status == kPMC_NO_ERROR)
			*value = rsp.u.cmdResponse.word2 & 0xffff;

		return status;
	} else
		return kPMC_INVALID_COMMAND;
}

int GetHWRev(int devAddr, int *value)
{
	CHECK_DEV_ADDRESSABLE(devAddr);

	if (pmc_mode == PMC_MODE_DQM) {
		TCommand rsp;
		int status = SendCommand(cmdGetHWRev, devAddr, 0, 0, 0, &rsp);

		if (status == kPMC_NO_ERROR)
			*value = rsp.u.cmdResponse.word2 & 0xff;

		return status;
	} else
		return kPMC_INVALID_COMMAND;
}

int GetNumZones(int devAddr, int *value)
{
	CHECK_DEV_ADDRESSABLE(devAddr);

	if (pmc_mode == PMC_MODE_DQM) {
		TCommand rsp;
		int status = SendCommand(cmdGetNumZones, devAddr, 0, 0, 0, &rsp);

		if (status == kPMC_NO_ERROR)
			*value = rsp.u.cmdResponse.word2 & 0xff;

		return status;
	} else
		return kPMC_INVALID_COMMAND;
}

int GetRevision(int *value)
{
	if (pmc_mode == PMC_MODE_DQM) {
		TCommand rsp;
		int status = SendCommand(cmdRevision, 0, 0, 0, 0, &rsp);

		if (status == kPMC_NO_ERROR)
			// word2 is partid, word3 is revision for new firmware
			*value = rsp.u.cmdResponse.word3 ?: rsp.u.cmdResponse.word2;

		return status;
	}
	else
		return kPMC_INVALID_COMMAND;
}

int GetPVT(int sel, int *value)
{
	if (pmc_mode == PMC_MODE_DQM) {
		TCommand rsp;
		int status = SendCommand(cmdGetPVT, 0, 0, sel, 0, &rsp);

		if (status == kPMC_NO_ERROR)
			*value = rsp.u.cmdResponse.word2;
		return status;
	}
	else
		return kPMC_INVALID_COMMAND;
}

int Ping(void)
{
	if (pmc_mode == PMC_MODE_DQM)
		return SendCommand(cmdPing, 0, 0, 0, 0, 0);
	else
		return kPMC_INVALID_COMMAND;
}

int GetErrorLogEntry(TErrorLogEntry *logEntry)
{
	if (pmc_mode == PMC_MODE_DQM)
		return SendCommand(cmdGetNextLogEntry, 0, 0, 0, 0, (TCommand *)logEntry);
	else
		return kPMC_INVALID_COMMAND;
}

int SetClockHighGear(int devAddr, int zone, int clkN)
{
	CHECK_DEV_ADDRESSABLE(devAddr);

	if (clkN < 0 || clkN > 7)
		return kPMC_INVALID_PARAM;

	if (pmc_mode == PMC_MODE_DQM) {
		TCommand cmd;
		cmd.u.cmdGenericParams.params[0] = 0;
		cmd.u.cmdGenericParams.params[1] = 0;
		cmd.u.cmdSetClockGear.gear = clkN & 0xff;
		return SendCmd(&cmd, cmdSetClockHighGear, devAddr, zone, 0);
	}
	else
		return kPMC_INVALID_COMMAND;
}

int SetClockLowGear(int devAddr, int zone, int clkN)
{
	CHECK_DEV_ADDRESSABLE(devAddr);

	if (clkN < 0 || clkN > 7)
		return kPMC_INVALID_PARAM;

	if (pmc_mode == PMC_MODE_DQM) {
		TCommand cmd;
		cmd.u.cmdGenericParams.params[0] = 0;
		cmd.u.cmdGenericParams.params[1] = 0;
		cmd.u.cmdSetClockGear.gear = clkN & 0xff;
		return SendCmd(&cmd, cmdSetClockLowGear, devAddr, zone, 0);
	}
	else
		return kPMC_INVALID_COMMAND;

}

int SetClockGear(int devAddr, int zone, int gear)
{
	CHECK_DEV_ADDRESSABLE(devAddr);

	if ((unsigned)gear > 3)
		return kPMC_INVALID_PARAM;

	if (pmc_mode == PMC_MODE_DQM) {
		TCommand cmd;
		cmd.u.cmdGenericParams.params[0] = 0;
		cmd.u.cmdGenericParams.params[1] = 0;
		cmd.u.cmdSetClockGear.gear = gear & 0xff;
		return SendCmd(&cmd, cmdSetClockGear, devAddr, zone, 0);
	}
	else
		return kPMC_INVALID_COMMAND;
}
#endif

/* GetRCalSetting reads resistor value and calculates the calibration setting for the SGMII, PCIe, SATA
   and USB HW blocks that requires resistor calibration to meet specification requirement.
   The HW driver should call this function and write the value to calibration register during initialzation

   input param: resistor - the resistor type that specific HW calibration care about:
   inout param: rcal - 4 bit RCAL value [0 -15] representing the increment or decrement to the internal resistor.   
   return: kPMC_NO_ERROR or kPMC_INVALID_COMMAND if error condition
*/
int GetRCalSetting(int resistor, int* rcal)
{
	int res_int, res_ext, ratio, ratio1;
	int rc = kPMC_NO_ERROR;

	if (pmc_mode == PMC_MODE_DQM) 
	{
		/* make sure the resistor selection is valid */
		if( resistor < RCAL_0P25UM_HORZ || resistor > RCAL_1UM_VERT )
			return kPMC_INVALID_COMMAND;

		/* make sure the resistor data is collected by PMC */
		if( (PROCMON->Misc.misc[PMMISC_RMON_EXT_REG]&PMMISC_RMON_VALID_MASK) == 0 )
			return kPMC_INVALID_COMMAND;

		res_int = PROCMON->Misc.misc[resistor>>1];
		if( resistor % 2 )
			res_int >>= 16;
		res_int &= 0xffff;
		res_ext = (PROCMON->Misc.misc[PMMISC_RMON_EXT_REG])&0xffff;

		/* Ratio = CLAMP((INT) (128.0 * V(REXT)/V(RINT)), 0, 255) */
		ratio = (128*res_ext)/res_int;
		if( ratio > 255 )
			ratio = 255;

		/* Ratio1 = CLAMP(128 - (Ratio - 128) * 4, 0, 255) */
		ratio1 = (128 - (ratio - 128 )*4);
		if( ratio1 < 0 )
			ratio1 = 0;
		if( ratio1 > 255 )
			ratio1 = 255;

		/* convert to 4 bit rcal setting value */
		*rcal = (ratio1>>4)&0xf;
#if 0
		printk("getrcal for res select %d, int %d, ext %d, ratio %d ratio1 %d, rcal %d\n", 
			resistor, res_int, res_ext, ratio, ratio1, *rcal);
#endif
	}
	else  
	{
		/* not supported if PMC is not running for now. To support that, need to copy the PMC rom 
		   code to read out resistor value manually */
		rc = kPMC_INVALID_COMMAND;
	}

	return rc;
}

/* note: all the [Read|Write][BPCM|Zone]Register functions are different from
 * how they are defined in firmware code.  In the driver code, it takes in
 * wordOffset as the argument, but in the firmware code, it uses byteOffset */
int ReadBPCMRegister(int devAddr, int wordOffset, uint32 *value)
{
	int status = kPMC_NO_ERROR;

	CHECK_DEV_ADDRESSABLE(devAddr);

	if (pmc_mode == PMC_MODE_DQM) {
		TCommand rsp;
		status = SendCommand(cmdReadBpcmReg, devAddr, 0, wordOffset, 0, &rsp);

		if (status == kPMC_NO_ERROR)
			*value = rsp.u.cmdResponse.word2;
	}
	else {	/* if (pmc_mode == PMC_MODE_PMB_DIRECT) */
		int bus = (devAddr >> 8) & 0x3;
		volatile PMBMaster *pmbm_ptr;
#ifndef _CFE_
		unsigned long flags;
#endif

		if (bus >= PMB_BUS_MAX)
			return kPMC_INVALID_BUS;

		pmbm_ptr = &(PROCMON->PMBM[bus]);

#ifndef _CFE_
		spin_lock_irqsave(&lock, flags);
#endif
		/* Make sure PMBM is not busy */
//		while (pmbm_ptr->ctrl & PMC_PMBM_BUSY);

		pmbm_ptr->ctrl = PMC_PMBM_START | PMC_PMBM_Read |
			((devAddr & 0xff) << 12) | wordOffset;

		while (pmbm_ptr->ctrl & PMC_PMBM_START);

		if (pmbm_ptr->ctrl & PMC_PMBM_TIMEOUT)
			status = kPMC_COMMAND_TIMEOUT;
		else
			*value = pmbm_ptr->rd_data;

#ifndef _CFE_
		spin_unlock_irqrestore(&lock, flags);
#endif
	}
	return status;
}

int ReadZoneRegister(int devAddr, int zone, int wordOffset, uint32 *value)
{
	int status = kPMC_NO_ERROR;

	CHECK_DEV_ADDRESSABLE(devAddr);

	if ((unsigned)wordOffset >= 4)
		return kPMC_INVALID_PARAM;

	if (pmc_mode == PMC_MODE_DQM) {
		TCommand rsp;

		status = SendCommand(cmdReadZoneReg, devAddr, zone, wordOffset,
				0, &rsp);
		if (status == kPMC_NO_ERROR)
			*value = rsp.u.cmdResponse.word2;
	}
	else	/* if (pmc_mode == PMC_MODE_PMB_DIRECT) */
		return ReadBPCMRegister(devAddr,
				BPCMRegOffset(zones[zone].control) + wordOffset,
				value);

	return status;
}

int WriteBPCMRegister(int devAddr, int wordOffset, uint32 value)
{
	CHECK_DEV_ADDRESSABLE(devAddr);

	if (pmc_mode == PMC_MODE_DQM)
		return SendCommand(cmdWriteBpcmReg, devAddr, 0, wordOffset, value, 0);
	else {	/* if (pmc_mode == PMC_MODE_PMB_DIRECT) */
		int bus = (devAddr >> 8) & 0x3;
		int status = kPMC_NO_ERROR;
		volatile PMBMaster *pmbm_ptr;
#ifndef _CFE_
		unsigned long flags;
#endif

		if (bus >= PMB_BUS_MAX)
			return kPMC_INVALID_BUS;

		pmbm_ptr = &(PROCMON->PMBM[bus]);

#ifndef _CFE_
		spin_lock_irqsave(&lock, flags);
#endif
		/* Make sure PMBM is not busy */
//		while (pmbm_ptr->ctrl & PMC_PMBM_BUSY);

		pmbm_ptr->wr_data = value;
		pmbm_ptr->ctrl = PMC_PMBM_START | PMC_PMBM_Write |
			((devAddr & 0xff) << 12) | wordOffset;

		while (pmbm_ptr->ctrl & PMC_PMBM_START);

		if (pmbm_ptr->ctrl & PMC_PMBM_TIMEOUT)
 			status = kPMC_COMMAND_TIMEOUT;

#ifndef _CFE_
		spin_unlock_irqrestore(&lock, flags);
#endif
		return status;
	}
}

int WriteZoneRegister(int devAddr, int zone, int wordOffset, uint32 value)
{
	CHECK_DEV_ADDRESSABLE(devAddr);

	if ((unsigned)wordOffset >= 4)
		return kPMC_INVALID_PARAM;

	if (pmc_mode == PMC_MODE_DQM)
		return SendCommand(cmdWriteZoneReg, devAddr, zone, wordOffset, value, 0);
	else	/* if (pmc_mode == PMC_MODE_PMB_DIRECT) */
		return WriteBPCMRegister(devAddr,
				BPCMRegOffset(zones[zone].control) + wordOffset,
				value);
}

int SetRunState(int island, int state)
{
	if (pmc_mode == PMC_MODE_DQM) {
		if ((unsigned)state >= 16)
			return kPMC_INVALID_PARAM;

		return SendStateCommand(cmdSetRunState, island, state);
	} else
		return kPMC_INVALID_COMMAND;
}

int SetPowerState(int island, int state)
{
	if (pmc_mode == PMC_MODE_DQM) {
		if ((unsigned)state >= 4)
			return kPMC_INVALID_PARAM;

		return SendStateCommand(cmdSetPowerState, island, state);
	} else
		return kPMC_INVALID_COMMAND;
}

int PowerOnDevice(int devAddr)
{
	CHECK_DEV_ADDRESSABLE(devAddr);

	if (pmc_mode == PMC_MODE_DQM) {
		TCommand cmd;
		cmd.u.cmdGenericParams.params[0] = 0;
		cmd.u.cmdGenericParams.params[1] = 0;
		cmd.u.cmdPowerDevice.state = 1;
#if defined _BCM963381_
		/* Only needed for 63381A0 boards from cfe where */
		/* this command has to be remapped to the old number */
		/* and the old byte-swapping has to be kludged */
		/* This will work for both A0 and B0 */
		cmd.u.cmdPowerDevice.reserved[1] = 1;
#endif
		return SendCmd(&cmd, cmdPowerDevOnOff, devAddr, 0, 0);
	}
	else {	/* if (pmc_mode == PMC_MODE_PMB_DIRECT) */
		int ix, status;
		BPCM_CAPABILITES_REG capabilities;

		status = ReadBPCMRegister(devAddr, BPCMRegOffset(capabilities), &capabilities.Reg32);
		for (ix = 0; (ix < capabilities.Bits.num_zones) && (status == kPMC_NO_ERROR); ix++) {
			status = PowerOnZone(devAddr, ix);
		}

		return status;
	}
}

int PowerOffDevice(int devAddr, int repower)
{
	CHECK_DEV_ADDRESSABLE(devAddr);

	if (pmc_mode == PMC_MODE_DQM) {
		TCommand cmd;
		cmd.u.cmdGenericParams.params[0] = 0;
		cmd.u.cmdGenericParams.params[1] = 0;
		cmd.u.cmdPowerDevice.state = 0;
		cmd.u.cmdPowerDevice.restore = repower;
		return SendCmd(&cmd, cmdPowerDevOnOff, devAddr, 0, 0);
	}
	else
	{	/* if (pmc_mode == PMC_MODE_PMB_DIRECT) */
		/* we can power off the entire device by powering off the 0th zone. */
		BPCM_PWR_ZONE_N_CONTROL reg;
		int status;

		status = ReadBPCMRegister(devAddr, BPCMRegOffset(zones[0].control), &reg.Reg32);

		if (status == kPMC_NO_ERROR && reg.Bits.pwr_off_state == 0) {
			reg.Bits.pwr_dn_req = 1;
			WriteBPCMRegister(devAddr, BPCMRegOffset(zones[0].control), reg.Reg32);
		}

		return status;
	}
}

int PowerOnZone(int devAddr, int zone)
{
	CHECK_DEV_ADDRESSABLE(devAddr);

#if 0
	/* Do not use DQM command cmdPowerZoneOnOff because this command is only available if a
	   PMC application has been uploaded to expand the PMC boot rom functionality */
	if (pmc_mode == PMC_MODE_DQM) {
		TCommand cmd;
		cmd.u.cmdGenericParams.params[0] = 0;
		cmd.u.cmdGenericParams.params[1] = 0;
		cmd.u.cmdStateOnlyParam.state = 1;
		return SendCmd(&cmd, cmdPowerZoneOnOff, devAddr, zone, 0);
	}
	else
#endif
	{	/* if (pmc_mode == PMC_MODE_PMB_DIRECT) */
		BPCM_PWR_ZONE_N_CONTROL reg;
		int status;

		status = ReadBPCMRegister(devAddr, BPCMRegOffset(zones[zone].control), &reg.Reg32);

		if (status == kPMC_NO_ERROR && reg.Bits.pwr_on_state == 0) {
			reg.Bits.pwr_dn_req = 0;
			reg.Bits.dpg_ctl_en = 1;
			reg.Bits.pwr_up_req = 1;
			reg.Bits.mem_pwr_ctl_en = 1;
			reg.Bits.blk_reset_assert = 1;
			status = WriteBPCMRegister(devAddr, BPCMRegOffset(zones[zone].control), reg.Reg32);
		}
		return status;
	}
}

int PowerOffZone(int devAddr, int zone)
{
	CHECK_DEV_ADDRESSABLE(devAddr);

#if 0
	/* Do not use DQM command cmdPowerZoneOnOff because this command is only available if a
	   PMC application has been uploaded to expand the PMC boot rom functionality */
	if (pmc_mode == PMC_MODE_DQM) {
		TCommand cmd;
		cmd.u.cmdGenericParams.params[0] = 0;
		cmd.u.cmdGenericParams.params[1] = 0;
		cmd.u.cmdStateOnlyParam.state = 0;
		return SendCmd(&cmd, cmdPowerZoneOnOff, devAddr, zone, 0);
	}
	else
#endif
	{	/* if (pmc_mode == PMC_MODE_PMB_DIRECT) */
	 	BPCM_PWR_ZONE_N_CONTROL reg;
		int status;

		status = ReadBPCMRegister(devAddr, BPCMRegOffset(zones[zone].control), &reg.Reg32);

		if (status == kPMC_NO_ERROR) {
			reg.Bits.pwr_dn_req = 1;
			reg.Bits.pwr_up_req = 0;
			status = WriteBPCMRegister(devAddr, BPCMRegOffset(zones[zone].control), reg.Reg32);
		}

		return status;
	}
}

int ResetDevice(int devAddr)
{
	CHECK_DEV_ADDRESSABLE(devAddr);

#if 0
	/* Do not use DQM command cmdResetDevice because this command is only available if a
	   PMC application has been uploaded to expand the PMC boot rom functionality */
	if (pmc_mode == PMC_MODE_DQM)
		return SendCommand(cmdResetDevice, devAddr, 0, 0, 0, 0);
	else
#endif
	{	/* if (pmc_mode == PMC_MODE_PMB_DIRECT) */
		/* all zones had their blk_reset_assert bits set at initial config time */
		BPCM_PWR_ZONE_N_CONTROL reg;
		int status;

		status = PowerOffDevice(devAddr, 0);

		do {
			status = ReadBPCMRegister(devAddr, BPCMRegOffset(zones[0].control), &reg.Reg32);
		} while ((reg.Bits.pwr_off_state != 1) && (status == kPMC_NO_ERROR));

		if (status == kPMC_NO_ERROR)
			status = PowerOnDevice(devAddr);

		return status;
	}
}

int ResetZone(int devAddr, int zone)
{
	CHECK_DEV_ADDRESSABLE(devAddr);

#if 0
	/* Do not use DQM command cmdResetZone because this command is only available if a
	   PMC application has been uploaded to expand the PMC boot rom functionality */
	if (pmc_mode == PMC_MODE_DQM)
		return SendCommand(cmdResetZone, devAddr, zone, 0, 0, 0);
	else
#endif
	{	/* if (pmc_mode == PMC_MODE_PMB_DIRECT) */
		BPCM_PWR_ZONE_N_CONTROL reg;
		int status;

		status = PowerOffZone(devAddr, zone);

		do {
			status = ReadBPCMRegister(devAddr, BPCMRegOffset(zones[zone].control), &reg.Reg32);
		} while ((reg.Bits.pwr_off_state != 1) && (status == kPMC_NO_ERROR));

		if (status == kPMC_NO_ERROR)
			status = PowerOnZone(devAddr, zone);

		return status;
	}
}

#if defined(_BCM963138_) || defined(CONFIG_BCM963138) || defined(_BCM963148_) || defined(CONFIG_BCM963148)
/* close AVS with margin(mV) */
int CloseAVS(int margin1, int margin2)
{
	if (pmc_mode == PMC_MODE_DQM) {
		TCommand rsp;
		int status = SendCommand(cmdCloseAVS, 0, 0, margin1, margin2, &rsp);
		return status;
	}
	else
		return kPMC_INVALID_COMMAND;
}
#endif

void WaitPmc(int runState)
{
	if (pmc_mode == PMC_MODE_DQM)
		while (((PMC->ctrl.hostMboxOut >> 24) & 7) != runState);
	else {	/* if (pmc_mode == PMC_MODE_PMB_DIRECT) */
		/* do nothing */
	}
}

void BootPmcNoRom(unsigned long physAddr)
{
	PMC->ctrl.addr1WndwMask = 0xffffc000;
	PMC->ctrl.addr1WndwBaseIn = 0x1fc00000;
	PMC->ctrl.addr1WndwBaseOut = physAddr;

	PMC->ctrl.softResets &= ~1;
}

#if defined(_BCM963138_) || defined(CONFIG_BCM963138)
/* Host need to stall/unstall PMC during image update when PMC is running from flash.
   The handshaking uing the gpIn and gpOut is not working with current hardware. Will
   find another register for this purpose. */
#define PMC_CTRL_GP_IN       0
#define PMC_CTRL_GP_OUT      1

#ifdef PMC_STALL_GPIO
static int CheckPMCStall(int dir)
{
    int i  = 0;
    if( dir == PMC_CTRL_GP_IN )
    {
        while(PMC->ctrl.gpIn&PMC_CTRL_GP_FLASH_BOOT_STALL)
	{
	    i++;
            if( i > 1000 )
            {
                printk("Wait PMC in gp clear time out.PMC maybe run in memory\n");
	        return -1;
            }
	}
    }
    else
    {
        while(!(PMC->ctrl.gpOut&PMC_CTRL_GP_FLASH_BOOT_STALL))
	{
	    i++;
            if( i > 1000 )
            {
                printk("Wait PMC out gp set time out.PMC maybe run in memory\n");
	        return -1;
            }
	}
    }

    return 0;
}
#endif

int StallPmc(void)
{
    int rc = 0;
    if( MISC->miscStrapBus&MISC_STRAP_BUS_PMC_ROM_BOOT && !(MISC->miscStrapBus&MISC_STRAP_BUS_PMC_BOOT_FLASH_N) )
    {
#ifdef PMC_STALL_GPIO
        PMC->ctrl.gpIn |= PMC_CTRL_GP_FLASH_BOOT_STALL;
        /* wait pmc to clear this bit and set the out bit */
        rc = CheckPMCStall(PMC_CTRL_GP_IN);
        rc |= CheckPMCStall(PMC_CTRL_GP_OUT);
#endif
    }
    //printk("PMC stalled\n");
    return rc;
}

int UnstallPmc(void)
{
    int rc = 0;
    if( MISC->miscStrapBus&MISC_STRAP_BUS_PMC_ROM_BOOT && !(MISC->miscStrapBus&MISC_STRAP_BUS_PMC_BOOT_FLASH_N) )
    {
#ifdef PMC_STALL_GPIO
        PMC->ctrl.gpIn |= PMC_CTRL_GP_FLASH_BOOT_STALL;
        /* wait pmc to clear in bit */
        rc = CheckPMCStall(PMC_CTRL_GP_IN);
#endif
    }

    //printk("PMC unstalled\n");
    return rc;
}
#elif defined(_BCM963148_) || defined(CONFIG_BCM963148)
/* new pmc firmware implements stall command */
/* state indicated by stalled bit in run status */

#include <asm/byteorder.h>
#define PMC_STALLED __constant_htonl(1 << 30)

/* return value doesn't appear to be used */
int StallPmc(void)
{
	TCommand rsp;

	/* check if already stalled */
	if (pmc_mode == PMC_MODE_PMB_DIRECT ||
	    PMC->ctrl.hostMboxIn & PMC_STALLED)
		return 0;

	/* return non-zero if stall command fails */
	return SendCommand(cmdStall, 0, 0, 0, 0, &rsp);
}

/* return value doesn't appear to be used */
int UnstallPmc(void)
{
	if (pmc_mode != PMC_MODE_PMB_DIRECT)
		PMC->ctrl.hostMboxIn &= ~PMC_STALLED;

	return 0;
}
#endif

int pmc_init(void)
{
#if defined MISC_STRAP_BUS_PMC_ROM_BOOT
	/* read the strap pin and based on the strap pin, choose the mode */
	if ((MISC->miscStrapBus & MISC_STRAP_BUS_PMC_ROM_BOOT) == 0)
		pmc_mode = PMC_MODE_PMB_DIRECT;

	if (pmc_mode == PMC_MODE_PMB_DIRECT)
		printk("%s:PMC using %s mode\n", __func__, "PMB_DIRECT");
	else {
#if defined (CONFIG_BCM963138) || (defined(_BCM963381_) && defined(CFG_RAMAPP))
		/* needed by linux on the 63138A0 (in case of old cfe) */
		/* needed by cfe on the 63381A0 (since pmc won't get upgraded until linux starts) */
		TCommand cmd, rsp;
		cmd.u.cmdGenericParams.params[0] = 0;
		cmd.u.cmdGenericParams.params[1] = 0;

		// send new cmdRevision (28) to see if we're talking to a recent pmc
		if (UtilGetChipRev() < 0xb0 &&
		    SendCmd(&cmd, 28, 0, 0, &rsp) != kPMC_NO_ERROR) {
			printk("%s:PMC remapping enabled\n", __func__);
			pmc_remap = 1;
		}
#endif
		printk("%s:PMC using %s mode\n", __func__, "DQM");
	}
#endif
	return 0;
}

#ifndef _CFE_

#if defined CONFIG_BCM963381
static __initconst
#include "app63381.h"

int __init pmc_app_init(void)
{
	/* too early to alloc; statically align the buffer */
#	define logsize ilog2(2 * (sizeof pmcappdata - 4) - 1)
	static char __attribute__ (( aligned(1 << logsize) ))
	    pmcbuf[sizeof pmcappdata - 4]; // aligned buffer
	unsigned physaddr = (unsigned) pmcbuf;
	unsigned linkaddr = 0x80000000; // translated code start
	unsigned sleeps = 1000;

	// only for 63381A[0-F]
	if (UtilGetChipRev() >= 0xb0)
		return pmc_init();

	if (((PMC->ctrl.hostMboxIn >> 24) & 7) != kPMCRunStateAVSCompleteWaitingForImage) {
		/* pmc bootrom disabled? */
		return pmc_init();
	}

	/* window 2 baseout specifies destination of pmcapp image */
	/* e4k will translate the block at BaseIn to BaseOut */
	/* pmc ram application linked for 0x80000000 */
	PMC->ctrl.addr2WndwMask = -(1 << logsize);
	PMC->ctrl.addr2WndwBaseIn = linkaddr & 0x1fffffff;
	PMC->ctrl.addr2WndwBaseOut = virt_to_phys(physaddr);

	/* mailbox specifies source of pmcapp image */
	PMC->ctrl.hostMboxOut = virt_to_phys(pmcappdata);

	/* pmc dma transfers from pmcappdata to physaddr */
	/* (first word of pmcappdata is byte size) */
	while (((PMC->ctrl.hostMboxIn >> 24) & 7) == kPMCRunStateAVSCompleteWaitingForImage) {
		if (--sleeps == 0) break;
		else msleep_interruptible(1);
	}

	return pmc_init();
}
// must be before pmc_powerup in arch_init
postcore_initcall(pmc_app_init);
#endif

EXPORT_SYMBOL(ReadBPCMRegister);
EXPORT_SYMBOL(ReadZoneRegister);
EXPORT_SYMBOL(WriteBPCMRegister);
EXPORT_SYMBOL(WriteZoneRegister);
EXPORT_SYMBOL(ResetZone);
EXPORT_SYMBOL(ResetDevice);
EXPORT_SYMBOL(PowerOffZone);
EXPORT_SYMBOL(PowerOnZone);
EXPORT_SYMBOL(PowerOffDevice);
EXPORT_SYMBOL(PowerOnDevice);
EXPORT_SYMBOL(GetDevPresence);
EXPORT_SYMBOL(GetSWStrap);
EXPORT_SYMBOL(GetHWRev);
EXPORT_SYMBOL(GetNumZones);
EXPORT_SYMBOL(GetRevision);
EXPORT_SYMBOL(GetPVT);
EXPORT_SYMBOL(Ping);
EXPORT_SYMBOL(GetErrorLogEntry);
EXPORT_SYMBOL(SetClockHighGear);
EXPORT_SYMBOL(SetClockLowGear);
EXPORT_SYMBOL(SetClockGear);
EXPORT_SYMBOL(SetRunState);
#if defined(CONFIG_BCM963148)
EXPORT_SYMBOL(CloseAVS);
#endif
EXPORT_SYMBOL(SetPowerState);
EXPORT_SYMBOL(BootPmcNoRom);
#if defined(PMC_STALL_GPIO) || defined(CONFIG_BCM963138) || defined(CONFIG_BCM963148)
EXPORT_SYMBOL(StallPmc);
EXPORT_SYMBOL(UnstallPmc);
#endif
EXPORT_SYMBOL(GetRCalSetting);
#endif
