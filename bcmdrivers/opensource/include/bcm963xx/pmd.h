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

#ifndef _PMD_H_
#define _PMD_H_

#include <linux/ioctl.h>
#include "laser.h"

#define PMD_I2C_HEADER 6 /*  Consist: 1 byte - CSR address, 1byte - config reg, 4 byte - register address */

#define PMD_DEV_CLASS   "laser_dev"
#define PMD_BUF_MAX_SIZE 300

typedef struct {
    uint8_t client;
    uint16_t offset; /* is used as message_id when using the messaging system */
    uint16_t len;
    unsigned char *buf;
} pmd_params;

/* IOctl */
#define PMD_IOCTL_SET_PARAMS     _IOW(LASER_IOC_MAGIC, 11, pmd_params)
#define PMD_IOCTL_GET_PARAMS     _IOR(LASER_IOC_MAGIC, 12, pmd_params)
#define PMD_IOCTL_CAL_FILE_WRITE _IOW(LASER_IOC_MAGIC, 13, pmd_params)
#define PMD_IOCTL_CAL_FILE_READ  _IOR(LASER_IOC_MAGIC, 14, pmd_params)
#define PMD_IOCTL_MSG_WRITE      _IOW(LASER_IOC_MAGIC, 15, pmd_params)
#define PMD_IOCTL_MSG_READ       _IOR(LASER_IOC_MAGIC, 16, pmd_params)
#define PMD_IOCTL_RSSI_CAL       _IOR(LASER_IOC_MAGIC, 17, pmd_params)



#define PMD_RSSI_GET_MSG               8 /* hmid_rssi_get */
#define PMD_NON_CAL_RSSI_GET_MSG       0xe /* hmid_rssi_non_cal_get */
#define PMD_RSSI_A_FACTOR_CAL_SET_MSG  0x9d /* hmid_rssi_a_factor_cal_set */
#define PMD_RSSI_B_FACTOR_CAL_SET_MSG  0x9e /* hmid_rssi_b_factor_cal_set */


typedef enum
{
    pmd_file_watermark          = 0,
    pmd_file_version            = 1,
    pmd_level_0_dac             = 2,
    pmd_level_1_dac             = 3,
    pmd_bias                    = 4,
    pmd_mod                     = 5,
    pmd_apd                     = 6,
    pmd_mpd_config              = 7,
    pmd_mpd_gains               = 8,
    pmd_apdoi_ctrl              = 9,
    pmd_rssi_a                  = 10, /* optic_power = a * rssi + b */
    pmd_rssi_b                  = 11,
    pmd_num_of_cal_param,
}pmd_calibration_param;


typedef struct
{
    uint32_t alarms;
    uint32_t sff;
}pmd_msg_addr;

#endif /* ! _PMD_H_ */
