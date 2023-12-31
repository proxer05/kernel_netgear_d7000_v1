#if defined(CONFIG_BCM_KF_VLANCTL_BIND)

/*
*    Copyright (c) 2003-2014 Broadcom Corporation
*    All Rights Reserved
*
<:label-BRCM:2014:DUAL/GPL:standard 

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
#include "bcm_OS_Deps.h"
#include <linux/bcm_log.h>
#include <linux/blog.h>

#include <linux/kernel.h>
#include <linux/vlanctl_bind.h>

static vlanctl_bind_SnHook_t vlanctl_bind_sn_hook_g[VLANCTL_BIND_CLIENT_MAX] = { (vlanctl_bind_SnHook_t)NULL };
static vlanctl_bind_ScHook_t vlanctl_bind_sc_hook_g[VLANCTL_BIND_CLIENT_MAX] = { (vlanctl_bind_ScHook_t)NULL };
static vlanctl_bind_SdHook_t vlanctl_bind_sd_hook_g[VLANCTL_BIND_CLIENT_MAX] = { (vlanctl_bind_SdHook_t)NULL };

#if defined(CC_VLANCTL_BIND_SUPPORT_DEBUG)
#define vlanctl_assertr(cond, rtn)                                              \
    if ( !cond ) {                                                              \
        printk( CLRerr "VLANCTL_BIND ASSERT %s : " #cond CLRnl, __FUNCTION__ ); \
        return rtn;                                                             \
    }
#else
#define vlanctl_assertr(cond, rtn) NULL_STMT
#endif

/*------------------------------------------------------------------------------
 *  Function    : vlanctl_bind_config
 *  Description : Override default config and deconf hook.
 *  vlanctl_sc  : Function pointer to be invoked in blog_activate()
 *  client      : configuration client
 *------------------------------------------------------------------------------
 */                      
void vlanctl_bind_config(vlanctl_bind_ScHook_t vlanctl_bind_sc, 
	                     vlanctl_bind_SdHook_t vlanctl_bind_sd,  
	                     vlanctl_bind_SnHook_t vlanctl_bind_sn,  
	                     vlanctl_bind_client_t client, 
                         vlanctl_bind_t bind)
{
    BCM_LOG_DEBUG(BCM_LOG_ID_VLAN,  "vlanctl Bind Sc[<%08x>] Sd[<%08x>] Sn[<%08x>] Client[<%u>] bind[<%u>]",
                (int)vlanctl_bind_sc, (int)vlanctl_bind_sd, (int)vlanctl_bind_sn, client, (uint8_t)bind.hook_info);

    if ( bind.bmap.SC_HOOK )
        vlanctl_bind_sc_hook_g[client] = vlanctl_bind_sc;   /* config hook */
    if ( bind.bmap.SD_HOOK )
        vlanctl_bind_sd_hook_g[client] = vlanctl_bind_sd;   /* deconf hook */
    if ( bind.bmap.SN_HOOK )
        vlanctl_bind_sn_hook_g[client] = vlanctl_bind_sn;   /* notify hook */
}



/*
 *------------------------------------------------------------------------------
 * Function     : vlanctl_activate
 * Description  : This function invokes vlanctl configuration hook
 * Parameters   :
 *  blog_p      : pointer to a blog with configuration information
 *  client      : configuration client
 *
 * Returns      :
 *  ActivateKey : If the configuration is successful, a key is returned.
 *                Otherwise, BLOG_KEY_INVALID is returned
 *------------------------------------------------------------------------------
 */
uint32_t vlanctl_activate( Blog_t * blog_p, vlanctl_bind_client_t client )
{
    uint32_t     key;

    key = BLOG_KEY_INVALID;
    
    if ( blog_p == BLOG_NULL || client >= VLANCTL_BIND_CLIENT_MAX )
    {
        vlanctl_assertr((blog_p != BLOG_NULL), key);
        goto bypass;
    }

    if (unlikely(vlanctl_bind_sc_hook_g[client] == (vlanctl_bind_ScHook_t)NULL))
        goto bypass;


    BLOG_LOCK_BH();
    key = vlanctl_bind_sc_hook_g[client](blog_p, BlogTraffic_Layer2_Flow);
    BLOG_UNLOCK_BH();

bypass:
    return key;
}

/*
 *------------------------------------------------------------------------------
 * Function     : vlanctl_deactivate
 * Description  : This function invokes a deconfiguration hook
 * Parameters   :
 *  key         : blog key information
 *  client      : configuration client
 *
 * Returns      :
 *  blog_p      : If the deconfiguration is successful, the associated blog 
 *                pointer is returned to the caller
 *------------------------------------------------------------------------------
 */
Blog_t * vlanctl_deactivate( uint32_t key, vlanctl_bind_client_t client )
{
    Blog_t * blog_p = NULL;

    if ( key == BLOG_KEY_INVALID || client >= VLANCTL_BIND_CLIENT_MAX )
    {
        vlanctl_assertr( (key != BLOG_KEY_INVALID), blog_p );
        goto bypass;
    }

    if ( unlikely(vlanctl_bind_sd_hook_g[client] == (vlanctl_bind_SdHook_t)NULL) )
        goto bypass;

    BLOG_LOCK_BH();
    blog_p = vlanctl_bind_sd_hook_g[client](key, BlogTraffic_Layer2_Flow);
    BLOG_UNLOCK_BH();

bypass:
    return blog_p;
}


int	vlanctl_notify(vlanctl_bind_Notify_t event, void *ptr, vlanctl_bind_client_t client)
{

   if (client >= VLANCTL_BIND_CLIENT_MAX)
       goto bypass;

    BCM_LOG_DEBUG(BCM_LOG_ID_VLAN, "client<%u>" "event<%u>", client, event);

    if (unlikely(vlanctl_bind_sn_hook_g[client] == (vlanctl_bind_SnHook_t)NULL))
        goto bypass;

	BLOG_LOCK_BH();
    vlanctl_bind_sn_hook_g[client](event, ptr);
    BLOG_UNLOCK_BH();

bypass:
    return 0;
}


EXPORT_SYMBOL(vlanctl_bind_config); 
EXPORT_SYMBOL(vlanctl_activate); 
EXPORT_SYMBOL(vlanctl_deactivate); 
EXPORT_SYMBOL(vlanctl_notify);


#endif /* defined(BCM_KF_VLANCTL_BIND */
