/*
   <:copyright-BRCM:2010:DUAL/GPL:standard
   
      Copyright (c) 2010 Broadcom Corporation
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

#if defined(STAR_FIGHTER2)
#include "drivers/bcm_ethsw_impl1.c"
#elif !defined(OREN)
#include "drivers/bcm_ethsw_impl2.c"
#endif

#include "drivers/bcm_chip_arch.c"

