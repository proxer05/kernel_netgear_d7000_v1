#
# <:copyright-BRCM:2013:DUAL/GPL:standard
# 
#    Copyright (c) 2013 Broadcom Corporation
#    All Rights Reserved
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 2, as published by
# the Free Software Foundation (the "GPL").
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# 
# A copy of the GPL is available at http://www.broadcom.com/licenses/GPLv2.php, or by
# writing to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA 02111-1307, USA.
# 
# :> 
#
# Makefile for the RDPA CMD
#
ifeq ("$(CONFIG_BCM963138)" ,"y")
obj-$(CONFIG_BCM_RDPA_DRV) := rdpa_cmd_drv.o rdpa_cmd_tm.o rdpa_cmd_ds_wan_udp_filter.o
else ifeq ("$(CONFIG_BCM963148)" ,"y")
obj-$(CONFIG_BCM_RDPA_DRV) := rdpa_cmd_drv.o rdpa_cmd_tm.o rdpa_cmd_ds_wan_udp_filter.o
else
obj-$(CONFIG_BCM_RDPA_DRV) := rdpa_cmd_drv.o rdpa_cmd_tm.o rdpa_cmd_iptv.o rdpa_cmd_ic.o rdpa_cmd_sys.o rdpa_cmd_port.o	rdpa_cmd_br.o rdpa_cmd_llid.o
endif
obj-$(CONFIG_BCM_RDPA_DRV) += rdpa_cmd_spdsvc.o

SRCS := $(subst rdpa_drv.mod.c,,$(notdir $(shell ls $(src)/*.c)))
OBJS := $(SRCS:.c=.o)
rdpa_drv-objs += $(OBJS)


ifeq ("$(CONFIG_BCM96838)" ,"y")
	CONFIG_PLATFORM=oren
	EXTRA_INCLUDES += -I$(INC_BRCMSHARED_PUB_PATH)/rdp
	EXTRA_INCLUDES += -I$(INC_BRCMSHARED_PUB_PATH)/rdp/base
endif
ifeq ("$(CONFIG_BCM963138)" ,"y")
	CONFIG_PLATFORM=dsl
	EXTRA_CFLAGS += -DDSL_63138
	EXTRA_INCLUDES += -I$(INC_BRCMSHARED_PUB_PATH)/rdp
	EXTRA_INCLUDES += -I$(INC_BRCMSHARED_PUB_PATH)/rdp/base
endif
ifeq ("$(CONFIG_BCM963148)" ,"y")
	CONFIG_PLATFORM=dsl
	EXTRA_CFLAGS += -DDSL_63148
	EXTRA_INCLUDES += -I$(INC_BRCMSHARED_PUB_PATH)/rdp
	EXTRA_INCLUDES += -I$(INC_BRCMSHARED_PUB_PATH)/rdp/base
endif

EXTRA_CFLAGS += -I$(BRCMDRIVERS_DIR)/opensource/net/enet/shared
EXTRA_CFLAGS += -I$(INC_BDMF_PATH)/platform/$(CONFIG_PLATFORM) -I$(INC_BRCMDRIVER_PUB_PATH)/$(BRCM_BOARD) -I$(INC_BRCMSHARED_PUB_PATH)/$(BRCM_BOARD)
EXTRA_CFLAGS += -I$(BRCMDRIVERS_DIR)/opensource/char/rdpa_gpl/impl1/include/autogen
EXTRA_CFLAGS += $(EXTRA_INCLUDES) $(EXTRA_CFLAGS_RDPA)
EXTRA_CFLAGS += -Werror -Wall

-include $(TOPDIR)/Rules.make

clean:
	rm -f core *.o *.a *.s .*.cmd *.ko *.mod.c modules.*

