/*
    Copyright 2000-2010 Broadcom Corporation

    <:label-BRCM:2011:DUAL/GPL:standard
    
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

#ifndef _WAN_DRV_H_
#define _WAN_DRV_H_


/**************************************************************************
* File Name  : wan_drv.h
*
* Description: This file contains the implementation for the BCM6838 WAN
*              block to handle GPON/EPON/ActiveEthernet
*
* Updates    : 02/26/2013  Created.
***************************************************************************/
#include "rdpa_epon.h"


typedef enum
{
    SERDES_WAN_TYPE_GPON,
    SERDES_WAN_TYPE_EPON,
    SERDES_WAN_TYPE_AE,
    SERDES_WAN_TYPE_DSL,
    SERDES_WAN_TYPE_GBE
}E_SERDES_WAN_TYPE;


int ConfigWanSerdes(E_SERDES_WAN_TYPE wan_type, rdpa_epon_speed_mode epon_speed);

void SATA_PRBS_Gen(uint32_t enable, int mode, E_SERDES_WAN_TYPE wan_type, 
            uint32_t *valid, rdpa_epon_speed_mode epon_speed);

void SATA_PRBS_Get_Status(uint32_t *valid);

#endif
