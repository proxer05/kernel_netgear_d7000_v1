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

//**************************************************************************
// File Name  : bcmenet.c
//
// Description: This is Linux network driver for Broadcom Ethernet controller
//
//**************************************************************************

#define VERSION     "0.1"
#define VER_STR     "v" VERSION " " __DATE__ " " __TIME__

#define _BCMENET_LOCAL_
#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/init.h>
#include <asm/io.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/mii.h>
#include <linux/skbuff.h>
#include <linux/kthread.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/kmod.h>
#include <linux/rtnetlink.h>
#include "linux/if_bridge.h"
#include <net/arp.h>
#include <board.h>
#include <spidevices.h>
#include <bcmnetlink.h>
#include <bcm_intr.h>
#include "linux/bcm_assert_locks.h"
#include <linux/bcm_realtime.h>
#include <linux/stddef.h>
#include <asm/atomic.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/nbuff.h>
#include <net/net_namespace.h>

#include <linux/module.h>

#include <linux/version.h>

#if defined(_CONFIG_BCM_BPM)
#include <linux/gbpm.h>
#include "bpm.h"
#endif

#include "bcmenet.h"
#include "bcmmii.h"
#include "bcm_eth.h"
#include "bcmeth_dma.h"
#include "pktCmf_public.h"
#include "bcmswshared.h"

#if defined(_CONFIG_BCM_INGQOS)
#include <linux/iqos.h>
#include "ingqos.h"
#endif

#include "bcmenet_dma_inline_impl6.h"

#if defined(SUPPORT_ETHTOOL)
#include "bcmenet_ethtool.h"
#endif


/* turn off error prints during bringup */
#define CC_IGNORE_CMS_ERRORS 1

/* In 60333, one net_device is equivalent to one rx channel */
#define MAX_NUM_OF_NETDEVICES ENET_RX_CHANNELS_MAX

/* PLC subsystem boot timeout in seconds */
#define PLC_BOOT_TIMEOUT (120)

/* Communicate the number of free plc0 TX BDs to plc-phy
 * for QoS management*/
#define RELAY_FREE_PKTS

/* Utility macros to check if a network interface is a PLC interface */
#define IS_PLC_PDEVCTRL(pDevCtrl) (global.pVnetDev0_g->EnetInfo[0].sw.phyconn[pDevCtrl->vport_id] == PHY_CONN_TYPE_PLC)
#define IS_PLC_IFACE_NUM(iface_num) (global.pVnetDev0_g->EnetInfo[0].sw.phyconn[iface_num] == PHY_CONN_TYPE_PLC)

extern int kerSysGetMacAddress(unsigned char *pucaMacAddr, unsigned long ulId);
static int bcm63xx_enet_open(struct net_device * dev);
static int bcm63xx_enet_close(struct net_device * dev);
static void bcm63xx_enet_poll_timer(unsigned long arg);
static int bcm63xx_enet_xmit(pNBuff_t pNBuff, struct net_device * dev);
static inline int bcm63xx_enet_xmit2(struct net_device *dev, EnetXmitParams *pParam);
static struct net_device_stats * bcm63xx_enet_query(struct net_device * dev);
static int bcm63xx_enet_change_mtu(struct net_device *dev, int new_mtu);
static int bcm63xx_enet_rx_thread(void *arg);
static uint32 bcm63xx_rx(void *ptr, uint32 budget);
static int bcm_set_mac_addr(struct net_device *dev, void *p);
static int bcm63xx_init_dev(BcmEnet_devctrl *pDevCtrl);
static int bcm63xx_uninit_dev(BcmEnet_devctrl *pDevCtrl);
static void bcm63xx_enet_enable_txrx(int enable);
static void __exit bcmenet_module_cleanup(void);
static int __init bcmenet_module_init(void);
static int bcm63xx_enet_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static void plc_flow_control_disable(unsigned long data);
static FN_HANDLER_RT plc_flowctrl_disable_isr(int irq, void * pContext);
int __init bcm63xx_enet_probe(void);

static DECLARE_COMPLETION(poll_done);
static atomic_t poll_lock = ATOMIC_INIT(1);
static int poll_pid = -1;
struct net_device* vnet_dev[MAX_NUM_OF_NETDEVICES] = {[0 ... (MAX_NUM_OF_NETDEVICES - 1)] = NULL};
int iface_cnt; /* Number of active interfaces */
unsigned long rx_flag; /* Bitmap for RX notification. LSB: Rx on vnet_dev[0] */
int bcmenet_in_init_dev = 0;

static const struct net_device_ops bcm96xx_netdev_ops = {
  .ndo_open   = bcm63xx_enet_open,
  .ndo_stop   = bcm63xx_enet_close,
  .ndo_start_xmit   = (HardStartXmitFuncP)bcm63xx_enet_xmit,
  .ndo_set_mac_address  = bcm_set_mac_addr,
  .ndo_do_ioctl   = bcm63xx_enet_ioctl,
  .ndo_get_stats      = bcm63xx_enet_query,
  .ndo_change_mtu     = bcm63xx_enet_change_mtu
};

enet_global_var_t global = {
    .dump_enable = 0,
    .net_device_stats_from_hw = {0},
    .pVnetDev0_g = NULL,
    .rx_thread = NULL,
    .rx_work_avail = 0
};


/*
 * TODO: Will need to use this semaphore when we implement ioctl functions.
 * Right now only the poll_timer kernel thread is triggering MAC/PHY
 * configuration so there's no need to protect anything.

struct semaphore bcm_link_handler_config;
*/

#ifdef BCM_ENET_DEBUG_BUILD
/* Debug Variables */
/* Number of pkts received on each channel */
static int ch_pkts[ENET_RX_CHANNELS_MAX] = {0};
/* Number of times there are no rx pkts on each channel */
static int ch_no_pkts[ENET_RX_CHANNELS_MAX] = {0};
static int ch_no_bds[ENET_RX_CHANNELS_MAX] = {0};
/* Number of elements in ch_serviced debug array */
#define NUM_ELEMS 4000
/* -1 indicates beginning of an rx(). The bit31 indicates whether a pkt
   is received on that channel or not */
static unsigned int ch_serviced[NUM_ELEMS] = {0};
static int dbg_index;
#define NEXT_INDEX(index) ((++index) % NUM_ELEMS)
#define ISR_START 0xFF
#define WRR_RELOAD 0xEE
#endif

#ifdef DYING_GASP_API
/* OAMPDU Ethernet dying gasp message */
unsigned char dg_ethOam_frame[64] = {
    1, 0x80, 0xc2, 0, 0, 2,
    0, 0,    0,    0, 0, 0, /* Fill Src MAC at the time of sending, from dev */
    0x88, 0x9,
    3, /* Subtype */
    5, /* code for DG frame */
    'B', 'R', 'O', 'A', 'D', 'C', 'O', 'M',
    ' ', 'B', 'C', 'G',
};
/* Socket buffer and buffer pointer for msg */
struct sk_buff *dg_skbp;
/* Flag indicates we're in Dying Gasp and powering down - don't clear once set */
int dg_in_context=0;
#endif

#ifdef MEMCPY_UNALIGNED_WORKAROUND
static char *aligned_buffer;
#endif

static volatile unsigned int *periph_mutex = PERIPH_MUTEX;
static int plc_xoff = 0;

enum
{
  PLC_STATUS_STOPPED = 0,
  PLC_STATUS_RUNNING,
  PLC_STATUS_ERROR,
};
static unsigned int plc_status = PLC_STATUS_STOPPED;

DECLARE_TASKLET(flowc_tasklet, plc_flow_control_disable, 0);

/* --------------------------------------------------------------------------
    Name: bcm63xx_enet_open
 Purpose: Open and Initialize the EMAC on the chip
-------------------------------------------------------------------------- */
static int bcm63xx_enet_open(struct net_device * dev)
{
    BcmEnet_devctrl *pDevCtrl = netdev_priv(dev);

    ASSERT(pDevCtrl != NULL);
    TRACE(("%s: bcm63xx_enet_open\n", dev->name));

    bcmeapi_open_dev(pDevCtrl, dev);
    return 0;
}

/* --------------------------------------------------------------------------
    Name: bcm63xx_enet_close
    Purpose: Stop communicating with the outside world
    Note: Caused by 'ifconfig ethX down'
-------------------------------------------------------------------------- */
static int bcm63xx_enet_close(struct net_device * dev)
{
    BcmEnet_devctrl *pDevCtrl = netdev_priv(dev);

    ASSERT(pDevCtrl != NULL);
    TRACE(("%s: bcm63xx_enet_close\n", dev->name));

    bcmeapi_del_dev_intr(pDevCtrl);
    return 0;
}

/* --------------------------------------------------------------------------
    Name: bcm63xx_enet_query
 Purpose: Return the current statistics. This may be called with the card
          open or closed.
-------------------------------------------------------------------------- */
static struct net_device_stats * bcm63xx_enet_query(struct net_device * dev)
{
    return &(((BcmEnet_devctrl *)netdev_priv(dev))->stats);
}

static int bcm63xx_enet_change_mtu(struct net_device *dev, int new_mtu)
{
    int max_mtu = ENET_MAX_MTU_PAYLOAD_SIZE;

    if (new_mtu < ETH_ZLEN || new_mtu > max_mtu)
        return -EINVAL;
    dev->mtu = new_mtu;
    return 0;
}

static void plc_flow_control_disable(unsigned long data)
{
    volatile BridgePLCPhyDatapaths *bridge_plcphy = BRIDGE_PLCPHY_DATAPATHS;
    BcmEnet_devctrl *pDevCtrl = (BcmEnet_devctrl *)netdev_priv(vnet_dev[0]);

    plc_xoff = 0;
    periph_mutex[0] = 0;
    BcmHalInterruptEnable(INTERRUPT_ID_PERIPHS_MBOX);
    netif_wake_queue(vnet_dev[data]);
    if (pDevCtrl->chipRev == 0xA0)
    {
        bridge_plcphy->channelCtrl |= BRIDGE_PLCPHY_SDR_START_RX;
    }
}

static FN_HANDLER_RT plc_flowctrl_disable_isr(int irq, void * pContext)
{
    volatile Misc *misc_regs = MISC;

    misc_regs->miscPeriphMbox &= ~MISC_PERIPH_MBOX_IRQ;
    tasklet_schedule(&flowc_tasklet);

    return BCM_IRQ_HANDLED;
}

/*
 * bcm63xx_enet_poll_timer: reclaim transmit frames which have been sent out
 */
static void bcm63xx_enet_poll_timer(unsigned long arg)
{
    struct net_device *dev;
    BcmEnet_devctrl *priv;
    int link_change = 0;
    int i;
    static unsigned long init_ts;

    init_ts = jiffies;
    daemonize("bcmsw");
    bcmeapiPhyIntEnable(1);

#if defined(CONFIG_BCM_ENET)
    set_current_state(TASK_INTERRUPTIBLE);
    /* Sleep for 1 tick) */
    schedule_timeout(HZ/100);
#endif

    /* Start with Ethernet interfaces as DOWN, leave PLC interface UP */
    for (i = 0; i < iface_cnt; i++)
    {
        netif_carrier_on(vnet_dev[i]);
        if (netif_carrier_ok(vnet_dev[i]) != 0)
        {
            netif_carrier_off(vnet_dev[i]);
        }
    }
    while (atomic_read(&poll_lock) > 0)
    {
        bcmeapi_enet_poll_timer();

        for (i = 0; i < iface_cnt; i++)
        {
            dev = vnet_dev[i];
            priv = (BcmEnet_devctrl *)netdev_priv(dev);
            /* Skip the non-Ethernet interface */
            if (IS_PLC_IFACE_NUM(i))
            {
                continue;
            }

            /* If PLC is not ready, do not update link status. 
               No news about plboot in 2 minutes -> consider boot failed and enable enet tx_rx */
            if (plc_status == PLC_STATUS_STOPPED)
            {
                if ((jiffies - init_ts)/HZ < PLC_BOOT_TIMEOUT)
                {
                  continue;
                }
                printk(KERN_INFO"[PLC] boot timeout\n");
                plc_status = PLC_STATUS_ERROR;
                bcm63xx_enet_enable_txrx(1);
            }

            link_change = eth_update_mac_mode(priv);

            if (link_change)
            {
                printk(KERN_INFO"Link change [%s]. link_speed: %lu,"
                          " duplex: %lu\n",
                          dev->name, priv->MibInfo.ulIfSpeed,
                          priv->MibInfo.ulIfDuplex);
                if (priv->MibInfo.ulIfSpeed == 0)
                {
                    netif_carrier_off(vnet_dev[i]);
                    priv->linkState = 0;
                }
                else
                {
                    netif_carrier_on(vnet_dev[i]);
                    priv->linkState = 1;
                }
            }
        }

        set_current_state(TASK_INTERRUPTIBLE);
        /* Sleep for HZ jiffies (1sec) */
        schedule_timeout(HZ);
    }
    complete_and_exit(&poll_done, 0);
    printk("bcm63xx_enet_poll_timer: thread exits!\n");
}

/* --------------------------------------------------------------------------
    Name: bcm63xx_enet_xmit
 Purpose: Send ethernet traffic
-------------------------------------------------------------------------- */
static int bcm63xx_enet_xmit(pNBuff_t pNBuff, struct net_device *dev)
{
    bool is_chained = FALSE;
    EnetXmitParams param = {{0}, 0};
    int ret;

#if defined(PKTC)
    pNBuff_t pNBuff_next = NULL;
    /* for PKTC, pNBuff is chained skb */
    if (IS_SKBUFF_PTR(pNBuff))
    {
        is_chained = PKTISCHAINED(pNBuff);
    }
#endif

    do {
        param.pDevPriv = netdev_priv(dev);
        param.vstats   = &param.pDevPriv->stats;
        param.pNBuff = pNBuff;
        param.is_chained = is_chained;

        BCM_ENET_TX_DEBUG("The physical port_id is %d\n", param.port_id);
        if (nbuff_get_params_ext(pNBuff, &param.data, &param.len,
                    &param.mark, &param.priority, &param.r_flags) == NULL)
            return 0;

        if (global.dump_enable)
            DUMP_PKT(param.data, param.len);

        BCM_ENET_TX_DEBUG("Using mark for channel and queue \n");
        param.egress_queue = SKBMARK_GET_Q_PRIO(param.mark);
        bcmeapi_select_tx_nodef_queue(&param);

        BCM_ENET_TX_DEBUG("The egress queue is %d \n", param.egress_queue);
#if defined(PKTC)
        if (is_chained)
            pNBuff_next = PKTCLINK(pNBuff);
#endif

        ret = bcm63xx_enet_xmit2(dev, &param);

#if defined(PKTC)
        if (is_chained)
            pNBuff = pNBuff_next;
#endif
    } while (is_chained && pNBuff && IS_SKBUFF_PTR(pNBuff));

    return ret;
}

static inline int bcm63xx_enet_xmit2(struct net_device *dev, EnetXmitParams *pParam)
{
    BcmEnet_devctrl *pDevPriv = netdev_priv(dev);
    volatile BridgePLCPhyDatapaths *bridge_plcphy = BRIDGE_PLCPHY_DATAPATHS;

    pParam->skb = PNBUFF_2_SKBUFF(pParam->pNBuff);
    if (IS_FKBUFF_PTR(pParam->pNBuff))
    {
        pParam->pFkb = PNBUFF_2_FKBUFF(pParam->pNBuff);
    }

    /* This substitutes bcmeapi_get_tx_queue for Duna */
    pParam->channel = pDevPriv->vport_id;
    pParam->blog_chnl = pParam->channel;
    pParam->blog_phy  = BLOG_ENETPHY;

#ifdef CONFIG_BLOG
    /*
     * Pass to blog->fcache, so it can construct the customized
     * fcache based execution stack.
     */
    if (pParam->is_chained == FALSE)
    {
        blog_emit(pParam->pNBuff, dev, TYPE_ETH, pParam->blog_chnl, pParam->blog_phy); /* CONFIG_BLOG */
    }
#endif

    /* This locks global.pVnetDev0_g->ethlock_tx */
    bcmeapi_buf_reclaim(pParam);

    /* gemid parameter is unused in impl6, so we recycle it for parameter
     * passing */
    pParam->gemid = 0;

    /* Workaround for unaligned PLCPHY buffers only applies to 60333_A0 */
    if ((pDevPriv->chipRev == 0xA0) && IS_PLC_PDEVCTRL(pDevPriv)
                                    && ((int)(pParam->data) & 0x7))
    {
#ifdef MEMCPY_UNALIGNED_WORKAROUND
        BCM_ENET_TX_DEBUG("Correct unaligned buffer. original data addr: 0x%p\n", pParam->data);
        aligned_buffer = memcpy(aligned_buffer, pParam->data, pParam->len);
        pParam->data = aligned_buffer;
        BCM_ENET_TX_DEBUG("aligned data addr: 0x%p\n", pParam->data);
#else
        BCM_ENET_TX_DEBUG("Correct unaligned buffer. original data addr: 0x%p, len: %d\n", pParam->data, pParam->len);
        pParam->len += ((int)(pParam->data) & 0x7);
        pParam->gemid = ((int)(pParam->data) & 0x7);
        pParam->data = skb_push(pParam->skb, (int)(pParam->data) & 0x7);
        pParam->data[0] = pParam->gemid;
        BCM_ENET_TX_DEBUG("new data addr: 0x%p, len: %d\n", pParam->data, pParam->len);
#endif
    }
    if(bcmeapi_queue_select(pParam) == BCMEAPI_CTRL_BREAK)
    {
        goto unlock_drop_exit;
    }

    /* Setup DMA TX descriptor status */
    bcmeapi_config_tx_queue_impl6(pParam);

    /* If frame length < Minimum Eth frame length, add padding.
     * frame_length = padded_frame_length + alignment bytes
     */
    if (pParam->len < ETH_ZLEN)
    {
        pParam->len = ETH_ZLEN + pParam->gemid;
    }

    if (IS_PLC_PDEVCTRL(pDevPriv))
    {
#ifdef RELAY_FREE_PKTS
        /* Send buffering information to plc-phy using a shared register.
         * Pack the number of plc0 free and TX BDs in a 32bit integer:
         * (Considering a little endian CPU)
         *   LSB: Free BDs
         *   MSB: Total BDs
         */
        BcmPktDma_LocalEthTxDma *txdma = pDevPriv->txdma[0];
        *(pDevPriv->plcphy_ac_register) = (unsigned short)(txdma->txFreeBds)
                                          | ((unsigned short)(txdma->numTxBds) << 16);
#endif
        if (periph_mutex[0] & 0x00000001)
        {
            /* Spinlock was unlocked, keep it unlocked for FirePath
             * (we locked it when we read it) */
            periph_mutex[0] = 0;
        }
        else
        {
            /* Mutex is locked */
            if (!plc_xoff)
            {
                BcmHalInterruptEnable(INTERRUPT_ID_PERIPHS_MBOX);
                if (pDevPriv->chipRev == 0xA0)
                {
                    bridge_plcphy->channelCtrl &= ~BRIDGE_PLCPHY_SDR_START_RX;
                }
                plc_xoff = 1;
            }
        }
    }

    BCM_ENET_TX_DEBUG("[bcm63xx_enet_xmit2] netdev %d, Call bcmeapi_pkt_xmt_dispatch\n", pDevPriv->vport_id);
    switch(bcmeapi_pkt_xmt_dispatch_impl6(pParam))
    {
        case BCMEAPI_CTRL_SKIP:
            goto unlock_drop_exit;
        default:
            break;
    }

    /* update stats */
    pDevPriv->stats.tx_bytes += pParam->len;
    pDevPriv->stats.tx_packets++;
    pDevPriv->dev->trans_start = jiffies;

    /* This unlocks global.pVnetDev0_g->ethlock_tx */
    bcmeapi_xmit_unlock_exit_post(pParam);
    return 0;

unlock_drop_exit:
    /* This unlocks global.pVnetDev0_g->ethlock_tx */
    bcmeapi_xmit_unlock_drop_exit_post(pParam);
    return 0;
}    

/* Driver for kernel 3.4 uses a dedicated thread for rx processing */
static int bcm63xx_enet_rx_thread(void *arg)
{
    uint32 work_done;                       // work done on single interface
    uint32 ret_done;
    const uint32 budget = NETDEV_WEIGHT / 2;   // per interface budget
    uint32 sched_work_done;                    // work done over all interfaces
    const uint32 sched_budget = NETDEV_WEIGHT; // budget over all interfaces 
                                            // should not be higher than MAX_SKB_FREE_BUDGET 
    int i = 0;
    int current_dev = 0;

    while (1)
    {
        sched_work_done = 0;  // reset this here
        wait_event_interruptible(global.rx_thread_wqh,
                                 global.rx_work_avail);
        global.rx_work_avail = 0;
        if (kthread_should_stop())
        {
            break;
        }

        while (1)
        {
            BCM_ENET_RX_DEBUG("[bcm63xx_enet_rx_thread] Enter rx_flag: 0x%lx\n", rx_flag);
            if (rx_flag == 0)
            {
                /* No more packets. No pending RX on any interface: Go back to
                 * sleep */
                i = 0;
                BCM_ENET_RX_DEBUG("No more packets on any interface\n");
                break;
            }
            /* Skip interfaces that didn't raise their RX flag */
            if ((rx_flag & (1 << i)) == 0)
            {
                i = (i + 1) % iface_cnt;
                continue;
            }
            local_bh_disable();

            bcmeapi_update_rx_queue(netdev_priv(vnet_dev[i]));
            BCM_ENET_RX_DEBUG("[bcm63xx_enet_rx_thread] netdev: %d\n", i);
            work_done = bcm63xx_rx(netdev_priv(vnet_dev[i]), min(budget, sched_budget-sched_work_done));
            ret_done = work_done & ENET_POLL_DONE;
            work_done &= ~ENET_POLL_DONE;
            sched_work_done += work_done;

            local_bh_enable();

            if (work_done >= budget || ret_done != ENET_POLL_DONE)
            {
                /* Proceed to the next pending interface */
                BCM_ENET_RX_DEBUG("[bcm63xx_enet_rx_thread] Yield to next interface\n");
                i = (i + 1) % iface_cnt;

                /* TODO: Investigate whether it's necessary to implement a
                 * budget policy to optimize reception on multiple interfaces */
            }
            else
            {
                clear_bit(i, (volatile unsigned long *)&rx_flag);
                current_dev = i;
                bcmeapi_napi_post(netdev_priv(vnet_dev[current_dev]));
            }
            if (sched_work_done >= sched_budget) {
                // allow other tasks at same priority to run (also allows rt runtime to
                // to be checked)

                // NOTE: despite the warnings in the code about using yield, it is in fact
                // the correct call at this point. 
                sched_work_done = 0;
                yield();
            }
        }
    }
    return 0;
}

/*
 *  bcm63xx_rx: Process all received packets.
 */
static uint32 bcm63xx_rx(void *ptr, uint32 budget)
{
    BcmEnet_devctrl *pDevCtrl = ptr;
    struct net_device *dev = NULL;
    unsigned char *pBuf = NULL;
    struct sk_buff *skb = NULL;
    int len=0, no_stat = 0, ret;
    uint32 rxpktgood = 0, rxpktprocessed = 0;
    FkBuff_t * pFkb = NULL;
    uint32_t blog_chnl, blog_phy; /* used if CONFIG_BLOG enabled */
    uint32 cpuid=0;  /* initialize to silence compiler.  It is correctly set at runtime */
#if defined(CONFIG_BLOG)
    BlogAction_t blogAction;
#endif
    /* bulk blog locking optimization only used in SMP builds */
    int got_blog_lock=0;

    volatile BridgePLCPhyDatapaths *bridge_plcphy = BRIDGE_PLCPHY_DATAPATHS;

    if (pDevCtrl->chipRev == 0xA0)
    {
        if (periph_mutex[0] & 0x00000001)
        {
            /* Spinlock was unlocked, keep it unlocked for FirePath
             * (we locked it when we read it) */
            periph_mutex[0] = 0;
        }
        else
        {
            bridge_plcphy->channelCtrl &= ~BRIDGE_PLCPHY_SDR_START_RX;
        }
    }

    /* as optimization on SMP, hold blog lock across multiple pkts */
    /* must grab blog_lock before enet_rx_lock */
    if (!got_blog_lock)
    {
        blog_lock();
        got_blog_lock=1;
        /*
         * Get the processor id AFTER we acquire the blog_lock.
         * Holding a lock disables preemption and migration, so we know
         * the processor id will not change as long as we hold the lock.
         */
        cpuid = smp_processor_id();
    }

    /* as optimization on SMP, hold rx lock across multiple pkts */
    if (0 == BULK_RX_LOCK_ACTIVE())
    {
        ENET_RX_LOCK();
        RECORD_BULK_RX_LOCK();
    }
    ret = bcmeapi_rx_pkt_impl6(pDevCtrl, &pBuf, &len, &rxpktgood);
    if(ret & BCMEAPI_CTRL_FLAG_TRUE)
    {
        no_stat = 1;
        ret &= ~BCMEAPI_CTRL_FLAG_MASK;
    }
    else
    {
        no_stat = 0;
    }
    if (ret != BCMEAPI_CTRL_TRUE)
    {
        goto after_rx;
    }

    BCM_ENET_RX_DEBUG("Processing Rx packet");
    rxpktprocessed++;
    dev = vnet_dev[pDevCtrl->vport_id];

    if (!no_stat)
    {
        /* Store packet & byte count */
        pDevCtrl->stats.rx_packets++;
        pDevCtrl->stats.rx_bytes += len;
    }

    blog_chnl = pDevCtrl->vport_id; /* blog rx channel is vport_id */
    blog_phy = BLOG_ENETPHY;

    /* FkBuff_t<data,len> in-placed leaving headroom */
    pFkb = fkb_init(pBuf, BCM_PKT_HEADROOM, pBuf, len);
    bcmeapi_set_fkb_recycle_hook_impl6(pFkb, pDevCtrl);

#ifdef CONFIG_BLOG
    /* SMP: bulk rx, bulk blog optimization */
    blogAction = blog_finit_locked(pFkb, dev, TYPE_ETH, blog_chnl, blog_phy);
    if (blogAction == PKT_DROP)
    {
        bcmeapi_blog_drop(pDevCtrl, pFkb, pBuf);
        /* Store dropped packet count in switch structure */
        pDevCtrl->stats.rx_dropped++;
        goto after_rx;
    }
    else
    {
        bcmeapi_buf_alloc(pDevCtrl);
    }

    /* packet consumed, proceed to next packet*/
    if (blogAction == PKT_DONE)
    {
        goto after_rx;
    }
#endif /* CONFIG_BLOG */

    if (bcmeapi_alloc_skb(pDevCtrl, &skb) == BCMEAPI_CTRL_FALSE) {
        RECORD_BULK_RX_UNLOCK();
        ENET_RX_UNLOCK();
        fkb_release(pFkb);
        pDevCtrl->stats.rx_dropped++;
        /* Not necessary to flush cache as no access was done.  */
        bcmeapi_kfree_buf_irq(pDevCtrl, NULL, pBuf);
        goto after_rx;
    }

    /*
     * We are outside of the fast path and not touching any
     * critical variables, so release all locks.
     */
    RECORD_BULK_RX_UNLOCK();
    ENET_RX_UNLOCK();
    got_blog_lock=0;
    blog_unlock();

    if(bcmeapi_skb_headerinit_impl6(len, pDevCtrl, skb, pFkb, pBuf) == BCMEAPI_CTRL_CONTINUE)
    {
        goto after_rx;
    }

    skb->protocol = eth_type_trans(skb, dev);
    skb->dev = dev;

    if (global.dump_enable)
    {
        DUMP_PKT(skb->data, skb->len);
    }

    BCM_ENET_RX_DEBUG("[bcm63xx_rx] netif_receive_skb\n");
    netif_receive_skb(skb);

after_rx:
    pDevCtrl->dev->last_rx = jiffies;

    if (got_blog_lock)
    {
        /*
         * Only need to check for BULK_RX_LOCK_ACTIVE if we have the blog_lock.
         * And we have the blog_lock, then cpuid was correctly set at runtime.
         */
        if (BULK_RX_LOCK_ACTIVE())
        {
            RECORD_BULK_RX_UNLOCK();
            ENET_RX_UNLOCK();
        }
        got_blog_lock=0;
        blog_unlock();
    }
    bcmeapi_rx_post(&rxpktgood);
    BCM_ASSERT_C(0 == got_blog_lock);
    BCM_ASSERT_NOT_HAS_SPINLOCK_C(&global.pVnetDev0_g->ethlock_rx);
    return rxpktgood;
}

/*
 * Set the hardware MAC address.
 */
static int bcm_set_mac_addr(struct net_device *dev, void *p)
{
    struct sockaddr *addr = p;

    if(netif_running(dev))
        return -EBUSY;
    memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);
    return 0;
}

/*
 * bcm63xx_init_dev: initialize Ethernet MACs,
 * allocate Tx/Rx buffer descriptors pool, Tx header pool.
 * Note that freeing memory upon failure is handled by calling
 * bcm63xx_uninit_dev, so no need of explicit freeing.
 */
static int bcm63xx_init_dev(BcmEnet_devctrl *pDevCtrl)
{
    int rc = 0;

    TRACE(("bcm63xxenet: bcm63xx_init_dev\n"));

    bcmenet_in_init_dev = 1;
    if ((rc = bcmeapi_init_queue(pDevCtrl)) < 0)
    {
        return rc;
    }
    pDevCtrl->use_default_txq = 0;
    bcmeapiPhyIntEnable(0);
    bcmenet_in_init_dev = 0;

    /* if we reach this point, we've init'ed successfully */
    return 0;
}

/* Uninitialize tx/rx buffer descriptor pools */
static int bcm63xx_uninit_dev(BcmEnet_devctrl *pDevCtrl)
{
    if (pDevCtrl)
    {
        bcmeapiPhyIntEnable(0);
        bcmeapi_free_queue(pDevCtrl);

        /* TODO: Delete the additional proc files */

        /* unregister and free the net device */
        if (pDevCtrl->dev)
        {
            if (IS_PLC_PDEVCTRL(pDevCtrl))
            {
                iounmap(pDevCtrl->plcphy_ac_register);
            }
            if (pDevCtrl->dev->reg_state != NETREG_UNINITIALIZED)
            {
                kerSysReleaseMacAddress(pDevCtrl->dev->dev_addr);
                unregister_netdev(pDevCtrl->dev);
            }
            free_netdev(pDevCtrl->dev);
        }
    }
    return 0;
}

/*
 *      bcm63xx_enet_probe: - Probe Ethernet switch and allocate device
 */
int __init bcm63xx_enet_probe(void)
{
    static int probed = 0;
    struct net_device *dev[MAX_NUM_OF_NETDEVICES] = {[0 ... (MAX_NUM_OF_NETDEVICES - 1)] = NULL};
    BcmEnet_devctrl *pDevCtrl[MAX_NUM_OF_NETDEVICES] = {[0 ... (MAX_NUM_OF_NETDEVICES - 1)] = NULL};
    struct net_device *dummy_dev = NULL;
    BcmEnet_devctrl *dummy_pDevCtrl = NULL;
    unsigned int chipid;
    unsigned int chiprev;
    unsigned char macAddr[ETH_ALEN];
    ETHERNET_MAC_INFO *EnetInfo;
    int status = 0;
    int ret;
    int i, j;

    TRACE(("bcm63xxenet: bcm63xx_enet_probe\n"));

    if (probed)
    {
        /* device has already been initialized */
        return -ENXIO;
    }
    probed++;

    bcmeapi_get_chip_idrev(&chipid, &chiprev);

    /* Allocate a dummy net_device and save a pointer to it for global use */
    dummy_dev = alloc_etherdev(sizeof(*dummy_pDevCtrl));
    dummy_pDevCtrl = netdev_priv(dummy_dev);
    spin_lock_init(&dummy_pDevCtrl->ethlock_tx);
    spin_lock_init(&dummy_pDevCtrl->ethlock_rx);
    global.pVnetDev0_g = dummy_pDevCtrl;
    EnetInfo = global.pVnetDev0_g->EnetInfo;

    if(BpGetEthernetMacInfo(EnetInfo, 1) != BP_SUCCESS)
    {
        printk(KERN_DEBUG CARDNAME" board id not set\n");
        return -ENODEV;
    }

    /* Retrieve the total number of interfaces from boardparms */
    bitcount(iface_cnt, EnetInfo->sw.port_map);

    /* Wait queue to be used by bcm63xx_enet_rx_thread */
    global.rx_work_avail = 0;
    init_waitqueue_head(&global.rx_thread_wqh);

#ifdef MEMCPY_UNALIGNED_WORKAROUND
    aligned_buffer = kmalloc(2000, __GFP_DMA);
#endif
    /*
     * i iterates over dev[] (vnet_dev[])
     * j iterates over boardparms interfaces
     */
    for (i = 0, j = 0; j < iface_cnt; j++)
    {
        dev[i] = alloc_etherdev(sizeof(*pDevCtrl[i]));
        if (dev[i] == NULL)
        {
            printk(KERN_ERR CARDNAME": Unable to allocate net_device %d!\n", i);
            return -ENOMEM;
        }

        pDevCtrl[i] = netdev_priv(dev[i]);
        memset(netdev_priv(dev[i]), 0, sizeof(BcmEnet_devctrl));
        pDevCtrl[i]->dev = dev[i];
        /* unit = 0. This is used in functions such as proc_get_dma_summary
         * to get the right pair of channel numbers. Set to 0 so that they always
         * get channels 0 and 1 (rx and tx) */
        pDevCtrl[i]->unit = 0;
        pDevCtrl[i]->chipId  = chipid;
        pDevCtrl[i]->chipRev = chiprev;
        /* vport_id stores the interface index (for access to vnet_dev[]) */
        pDevCtrl[i]->vport_id = i;
        /* sw_port_id stores the interface ID retrieved from boardparms */
        pDevCtrl[i]->sw_port_id = EnetInfo->sw.phy_id[j];

        if ((status = bcm63xx_init_dev(pDevCtrl[i])))
        {
            printk((KERN_ERR CARDNAME ": device initialization error!\n"));
            bcm63xx_uninit_dev(pDevCtrl[i]);
            return -ENXIO;
        }

        /* Take the interface name from boardparms */
        strncpy(dev[i]->name, EnetInfo->sw.phy_devName[j], IFNAMSIZ);
        dev_alloc_name(dev[i], dev[i]->name);
        SET_MODULE_OWNER(dev[i]);

        bcmeapi_add_proc_files(dev[i], pDevCtrl[i]);
        dev[i]->netdev_ops = &bcm96xx_netdev_ops;
#if defined(SUPPORT_ETHTOOL)
        dev[i]->ethtool_ops = &bcm63xx_enet_ethtool_ops;
#endif

        /* TODO: Add additional proc files */
        netdev_path_set_hw_port(dev[i], i, BLOG_ENETPHY);
        dev[i]->watchdog_timeo = 2 * HZ;

        /* Indicate we're supporting extended statistics */
        dev[i]->features |= NETIF_F_EXTSTATS;

        if (EnetInfo->sw.phyconn[i] != PHY_CONN_TYPE_PLC)
        {
            eth_init(pDevCtrl[i]);
        }
        else
        {
            /* Register the Periphs Mbox interrupt for PLC XON */
            BcmHalMapInterrupt(plc_flowctrl_disable_isr, 0, INTERRUPT_ID_PERIPHS_MBOX);
            flowc_tasklet.data = i;
            /* ioremap a dummy register for sending the number of free and
             * total plc0 TX BDs to plc-phy.
             * (Considering a little endian CPU)
             *   LSB: Free BDs
             *   MSB: Total BDs
             */
            pDevCtrl[i]->plcphy_ac_register = ioremap_nocache(PLC_PHY_AC_CONTROL_REGISTER, 4);
            *(pDevCtrl[i]->plcphy_ac_register) = (unsigned short)(pDevCtrl[i]->txdma[0]->txFreeBds)
                                              | ((unsigned short)(pDevCtrl[i]->txdma[0]->numTxBds) << 16);
        }

        status = register_netdev(dev[i]);
        if (status != 0)
        {
            bcm63xx_uninit_dev(pDevCtrl[i]);
            printk(KERN_ERR CARDNAME "bcm63xx_enet_probe failed, returns %d\n", status);
            return status;
        }

        /* Reserve MAC for PLC SoC */
        if (EnetInfo->sw.phyconn[i] == PHY_CONN_TYPE_PLC) {
           status = kerSysGetMacAddress(macAddr, MAC_ADDRESS_PLC);
           if (status == 0) {
               macAddr[0] ^= 0x2;
           }
        }
        else {
            status = kerSysGetMacAddress(macAddr, dev[i]->ifindex);
        }

        if(status != 0)
        {
            return status;
        }
        memmove(dev[i]->dev_addr, macAddr, ETH_ALEN);

        if( (ret = bcmeapi_init_dev(dev[i])) < 0)
            return ret;

        pDevCtrl[i]->linkState = 0;

        global.pVnetDev0_g->txdma[i] = pDevCtrl[i]->txdma[0];
        global.pVnetDev0_g->rxdma[i] = pDevCtrl[i]->rxdma[0];
        vnet_dev[i] = dev[i];
        i++;
    }

    /*
     * In Linux 3.4, we do not use softirq or NAPI.  We create a thread to
     * do the rx processing work.
     */
    global.rx_thread = kthread_create(bcm63xx_enet_rx_thread, NULL, "bcmsw_rx");
    wake_up_process(global.rx_thread);

    poll_pid = kernel_thread((int(*)(void *))bcm63xx_enet_poll_timer, 0, CLONE_KERNEL);

    return ((poll_pid < 0)? -ENOMEM: 0);
}

static int bridge_notifier(struct notifier_block *nb, unsigned long event, void *brName);
struct notifier_block br_notifier = {
    .notifier_call = bridge_notifier,
};

static int bridge_stp_handler(struct notifier_block *nb, unsigned long event, void *portInfo);
static struct notifier_block br_stp_handler = {
    .notifier_call = bridge_stp_handler,
};

static void __exit bcmenet_module_cleanup(void)
{
    BcmEnet_devctrl *pDevCtrl;
    int i;

    TRACE(("bcm63xxenet: bcmenet_module_cleanup\n"));
    bcmeapi_enet_module_cleanup();

    tasklet_kill(&flowc_tasklet);

#ifdef MEMCPY_UNALIGNED_WORKAROUND
    kfree(aligned_buffer);
#endif

    if (poll_pid >= 0)
    {
      atomic_dec(&poll_lock);
      wait_for_completion(&poll_done);
    }
    for (i = 0; i < iface_cnt; i++)
    {
        pDevCtrl = (BcmEnet_devctrl *)netdev_priv(vnet_dev[i]);
        if (pDevCtrl)
        {
            bcm63xx_uninit_dev(pDevCtrl);
        }
    }
    unregister_bridge_notifier(&br_notifier);
    unregister_bridge_stp_notifier(&br_stp_handler);
}

void display_software_stats(BcmEnet_devctrl * pDevCtrl)
{
    printk("\n");
    printk("TxPkts:       %10lu \n", pDevCtrl->stats.tx_packets);
    printk("TxOctets:     %10lu \n", pDevCtrl->stats.tx_bytes);
    printk("TxDropPkts:   %10lu \n", pDevCtrl->stats.tx_dropped);
    printk("\n");
    printk("RxPkts:       %10lu \n", pDevCtrl->stats.rx_packets);
    printk("RxOctets:     %10lu \n", pDevCtrl->stats.rx_bytes);
    printk("RxDropPkts:   %10lu \n", pDevCtrl->stats.rx_dropped);
}

static void bcm63xx_enet_enable_txrx(int enable)
{
    BcmEnet_devctrl *pDevCtrl;
    int i;

    for (i=0; i<iface_cnt; i++)
    {
        pDevCtrl = (BcmEnet_devctrl *)netdev_priv(vnet_dev[i]);
        if (pDevCtrl && !IS_PLC_IFACE_NUM(i))
        {
            eth_mac_enable_txrx(pDevCtrl, 1);
        }
    }
}

static int bcm63xx_enet_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
    BcmEnet_devctrl *pDevCtrl;
    int *data=(int*)rq->ifr_data;
    struct ethctl_data *ethctl=(struct ethctl_data*)rq->ifr_data;
    int set_link_up = 0;
    int val = 0;

    pDevCtrl = netdev_priv(dev);
    BCM_ENET_DEBUG("ioctl, ifr_name: %s, ifr_ifindex: %d, dev_name: %s, cmd: %d\n",
            rq->ifr_name, rq->ifr_ifindex, dev->name, cmd);

    switch (cmd)
    {
        case SIOCGLINKSTATE:
            val = pDevCtrl->linkState;
            if (copy_to_user((void*)data, (void*)&val, sizeof(int)))
            {
                return -EFAULT;
            }
            break;
        case SIOCETHSWCTLOPS:
#ifdef CC_IGNORE_CMS_ERRORS
            val = 0;
#else
            val = -EOPNOTSUPP;
#endif
            break;
        case SIOCETHCTLOPS:
            switch(ethctl->op) {
                case ETHGETNUMTXDMACHANNELS:
                case ETHGETNUMRXDMACHANNELS:
                    ethctl->ret_val = iface_cnt;
                    val = 0;
                    break;
                case ETHSETNUMTXDMACHANNELS:
                case ETHSETNUMRXDMACHANNELS:
                    val = -EOPNOTSUPP;
                    break;
                case ETHGETSOFTWARESTATS:
                    display_software_stats(pDevCtrl);
                    val = 0;
                    break;
                case ETHSETSPOWERUP:
                    val = -EOPNOTSUPP;
                    break;
                case ETHSETSPOWERDOWN:
                    eth_phy_powerdown(pDevCtrl);
                    val = 0;
                    break;
                case ETHGETMIIREG:       /* Read MII PHY register. */
                    val = -EOPNOTSUPP;
                    break;
                case ETHSETMIIREG:       /* Write MII PHY register. */
                    val = -EOPNOTSUPP;
                    break;
                case ETHGETCOREID:
                    if(!IS_PLC_PDEVCTRL(pDevCtrl))
                    {
                        ethctl->ret_val = pDevCtrl->sw_port_id;
                    }
                    else
                    {
                        return -EFAULT;
                    }

                case ETHSETLINKSTATE:
                    if (IS_PLC_PDEVCTRL(pDevCtrl))
                    {
                        /* ioctl on PLC interface */
                        if ((plc_status == PLC_STATUS_STOPPED) && (ethctl->val != PLC_STATUS_STOPPED))
                        {
                            bcm63xx_enet_enable_txrx(1);
                        }
                        plc_status = ethctl->val;
                        if (plc_status == PLC_STATUS_RUNNING)
                        {
                            set_link_up = 1;
                        }
                    }
                    else
                    {
                        /* ioctl for a Ethernet interface */
                        if (ethctl->val != 0)
                        {
                            set_link_up = 1;
                        }
                    }

                    if (set_link_up)
                    {
                        pDevCtrl->linkState = 1;
                        netif_carrier_on(dev);
                    }
                    else
                    {
                        pDevCtrl->linkState = 0;
                        netif_carrier_off(dev);
                    }
                    val = 0;
                    break;
                default:
                    val = -EOPNOTSUPP;
                    break;
            }
            break;
        case SIOCSWANPORT:
            if ((int)data) {
                // attempting to set port as WAN:
                val = -EOPNOTSUPP;
            } else {
                // attempting to set port as LAN -- should already be LAN, just return
                val = 0;
            }
        default:
            val = -EOPNOTSUPP;
            break;
    }

    return val;
}

static int __init bcmenet_module_init(void)
{
    int status;

    TRACE(("bcm63xxenet: bcmenet_module_init\n"));

    bcmeapi_module_init();

    if (BCM_SKB_ALIGNED_SIZE != skb_aligned_size())
    {
        printk("skb_aligned_size mismatch. Need to recompile enet module\n");
        return -ENOMEM;
    }

    status = bcm63xx_enet_probe();

    bcmeapi_module_init2();
    register_bridge_stp_notifier(&br_stp_handler);

    return status;
}

static int bridge_notifier(struct notifier_block *nb, unsigned long event, void *brName)
{
    switch (event)
    {
        case BREVT_IF_CHANGED:
            break;
    }
    return NOTIFY_DONE;
}

static int bridge_stp_handler(struct notifier_block *nb, unsigned long event, void *portInfo)
{
    switch (event)
    {
        case BREVT_STP_STATE_CHANGED:
        {
            break;
        }
    }
    return NOTIFY_DONE;
    return 0;
}

module_init(bcmenet_module_init);
module_exit(bcmenet_module_cleanup);
MODULE_LICENSE("GPL");
