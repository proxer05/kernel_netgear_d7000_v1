/*
   Copyright (c) 2013 Broadcom Corporation
   All Rights Reserved

    <:label-BRCM:2013:DUAL/GPL:standard
    
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

#ifndef __963138_RDP_MAP_H
#define __963138_RDP_MAP_H

#include "63138_map_part.h"

/*****************************************************************************************/
/* BBH Blocks offsets                                                                        */
/*****************************************************************************************/
#define BBH_TX_0_OFFSET				( RDP_BASE + 0x000e8000 )
#define BBH_TX_1_OFFSET				( RDP_BASE + 0x000ea000 )
#define BBH_TX_2_OFFSET				( RDP_BASE + 0x000ec000 )
#define BBH_TX_3_OFFSET				( RDP_BASE + 0x000ee000 )
#define BBH_TX_4_OFFSET				( RDP_BASE + 0x000f0000 )
#define BBH_TX_5_OFFSET				( RDP_BASE + 0x000f2000 )
#define BBH_TX_6_OFFSET				( RDP_BASE + 0x000f4000 )
#define BBH_TX_7_OFFSET				( RDP_BASE + 0x000f6000 )
#define BBH_RX_0_OFFSET				( RDP_BASE + 0x000de000 )
#define BBH_RX_1_OFFSET				( RDP_BASE + 0x000de800 )
#define BBH_RX_2_OFFSET				( RDP_BASE + 0x000df000 )
#define BBH_RX_3_OFFSET				( RDP_BASE + 0x000df800 )
#define BBH_RX_4_OFFSET				( RDP_BASE + 0x000e0000 )
#define BBH_RX_5_OFFSET				( RDP_BASE + 0x000e0400 )
#define BBH_RX_6_OFFSET				( RDP_BASE + 0x000e1000 )
#define BBH_RX_7_OFFSET				( RDP_BASE + 0x000e2000 )
/*****************************************************************************************/
/* BPM Blocks offsets                                                                        */
/*****************************************************************************************/
#define BPM_MODULE_OFFSET			( RDP_BASE + 0x000c4000 )
/*****************************************************************************************/
/* SBPM Blocks offsets                                                                        */
/*****************************************************************************************/
#define SBPM_BLOCK_OFFSET			( RDP_BASE + 0x000c8000 )
/*****************************************************************************************/
/* DMA Blocks offsets                                                                        */
/*****************************************************************************************/
#define DMA_REGS_0_OFFSET			( RDP_BASE + 0x000d1000 )
#define DMA_REGS_1_OFFSET			( RDP_BASE + 0x000d1800 )
/*****************************************************************************************/
/* IH Blocks offsets                                                                        */
/*****************************************************************************************/
#define IH_REGS_OFFSET				( RDP_BASE + 0x000d0000 )
/*****************************************************************************************/
/* PSRAM Blocks offsets                                                                        */
/*****************************************************************************************/
#define PSRAM_BLOCK_OFFSET			( RDP_BASE + 0x000a0000  )
/*****************************************************************************************/
/* RUNNER Blocks offsets                                                                        */
/*****************************************************************************************/
#define RUNNER_COMMON_0_OFFSET		( RDP_BASE + 0x00000000 )
#define RUNNER_COMMON_1_OFFSET		( RDP_BASE + 0x00040000 )
#define RUNNER_PRIVATE_0_OFFSET		( RDP_BASE + 0x00010000 )
#define RUNNER_PRIVATE_1_OFFSET		( RDP_BASE + 0x00050000 )
#define RUNNER_INST_MAIN_0_OFFSET	( RDP_BASE + 0x00020000 )
#define RUNNER_INST_MAIN_1_OFFSET	( RDP_BASE + 0x00060000 )
#define RUNNER_CNTXT_MAIN_0_OFFSET	( RDP_BASE + 0x00028000 )
#define RUNNER_CNTXT_MAIN_1_OFFSET	( RDP_BASE + 0x00068000 )
#define RUNNER_PRED_MAIN_0_OFFSET	( RDP_BASE + 0x0002c000 )
#define RUNNER_PRED_MAIN_1_OFFSET	( RDP_BASE + 0x0006c000 )
#define RUNNER_INST_PICO_0_OFFSET	( RDP_BASE + 0x00030000 )
#define RUNNER_INST_PICO_1_OFFSET	( RDP_BASE + 0x00070000 )
#define RUNNER_CNTXT_PICO_0_OFFSET	( RDP_BASE + 0x00038000 )
#define RUNNER_CNTXT_PICO_1_OFFSET	( RDP_BASE + 0x00078000 )
#define RUNNER_PRED_PICO_0_OFFSET	( RDP_BASE + 0x0003c000 )
#define RUNNER_PRED_PICO_1_OFFSET	( RDP_BASE + 0x0007c000 )
#define RUNNER_REGS_0_OFFSET		( RDP_BASE + 0x00099000 )
#define RUNNER_REGS_1_OFFSET		( RDP_BASE + 0x0009a000 )
/*****************************************************************************************/
/* UBUS MASTER Blocks offsets                                                            */
/*****************************************************************************************/
#define UBUS_MASTER_1_RDP_UBUS_MASTER_BRDG_REG_EN           ( RDP_BASE + 0x000d2000 )
#define UBUS_MASTER_1_RDP_UBUS_MASTER_BRDG_REG_REQ_CNTRL    ( RDP_BASE + 0x000d2004 )
#define UBUS_MASTER_1_RDP_UBUS_MASTER_BRDG_REG_HP           ( RDP_BASE + 0x000d200c )
#define UBUS_MASTER_2_RDP_UBUS_MASTER_BRDG_REG_EN           ( RDP_BASE + 0x000d2400 )
#define UBUS_MASTER_2_RDP_UBUS_MASTER_BRDG_REG_REQ_CNTRL    ( RDP_BASE + 0x000d2404 )
#define UBUS_MASTER_2_RDP_UBUS_MASTER_BRDG_REG_HP           ( RDP_BASE + 0x000d240c )
#define UBUS_MASTER_3_RDP_UBUS_MASTER_BRDG_REG_EN           ( RDP_BASE + 0x000d2800 )
#define UBUS_MASTER_3_RDP_UBUS_MASTER_BRDG_REG_REQ_CNTRL    ( RDP_BASE + 0x000d2804 )
#define UBUS_MASTER_3_RDP_UBUS_MASTER_BRDG_REG_HP           ( RDP_BASE + 0x000d280c )

#endif
