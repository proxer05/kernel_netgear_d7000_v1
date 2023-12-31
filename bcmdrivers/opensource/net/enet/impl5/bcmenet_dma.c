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


//**************************************************************************
// File Name  : bcmeapi_legacy.c
//
// Description: This is Linux network driver for Broadcom Ethernet controller
//
//**************************************************************************
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
#include <bcm_map_part.h>
#include <bcm_intr.h>
#include "linux/bcm_assert_locks.h"
#include <linux/bcm_realtime.h>
#include "bcmenet.h"
#include "bcmmii.h"
#include "ethsw.h"
#include "ethsw_phy.h"
#include "bcmsw.h"
#include <linux/stddef.h>
#include <asm/atomic.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/nbuff.h>
#include "pktCmf_public.h"
#include "bcmPktDma.h"
#include "eth_pwrmngt.h"

#include <net/net_namespace.h>

#if defined(_CONFIG_BCM_INGQOS)
#include <linux/iqos.h>
#include "ingqos.h"
#endif
#if defined(_CONFIG_BCM_BPM)
#include <linux/gbpm.h>
#include "bpm.h"
#endif
#if defined(_CONFIG_BCM_ARL)
#include <linux/blog_rule.h>
#endif
#if defined(CONFIG_BCM_GMAC)
#include <bcmgmac.h>
#endif

#if defined(PKTC)
#include <linux_osl_dslcpe_pktc.h>
#endif

static FN_HANDLER_RT bcm63xx_ephy_isr(int irq, void *);
#if defined(CONFIG_BCM963268) || defined(CONFIG_BCM96828)
static FN_HANDLER_RT bcm63xx_gphy_isr(int irq, void *);
#endif


void uninit_buffers(BcmEnet_RxDma *rxdma);
static int init_buffers(BcmEnet_devctrl *pDevCtrl, int channel);
static inline void _assign_rx_buffer(BcmEnet_devctrl *pDevCtrl, int channel, uint8 * pData);

#if defined(_CONFIG_BCM_FAP)
static thresh_t enet_rx_dqm_iq_thresh[ENET_RX_CHANNELS_MAX];

static void enet_iq_dqm_update_cong_status(BcmEnet_devctrl *pDevCtrl);
static void enet_iq_dqm_status(void);
#endif

#if defined(RXCHANNEL_PKT_RATE_LIMIT)
static void bcm63xx_timer(unsigned long arg);
#endif /* RXCHANNEL_PKT_RATE_LIMIT */
static void bcm63xx_uninit_rxdma_structures(int channel, BcmEnet_devctrl *pDevCtrl);
static int bcm63xx_init_rxdma_structures(int channel, BcmEnet_devctrl *pDevCtrl);
/*
 * This macro can only be used inside enet_xmit2 because it uses the local
 * variables defined in that function.
 */

#if defined(_CONFIG_BCM_BPM) && defined(_CONFIG_BCM_FAP)
/* BPM status dump handler hook */
extern gbpm_thresh_hook_t gbpm_enet_thresh_hook_g;

static void enet_bpm_init_tx_drop_thr(BcmEnet_devctrl *pDevCtrl, int chnl);
static void enet_bpm_set_tx_drop_thr( BcmEnet_devctrl *pDevCtrl, int chnl );
static void enet_bpm_dma_dump_tx_drop_thr(void);
static void enet_bpm_dump_tx_drop_thr(void);
/* Sanity checks */
#if (BPM_ENET_BULK_ALLOC_COUNT > FAP_BPM_ENET_BULK_ALLOC_MAX)
#error "ERROR - BPM_ENET_BULK_ALLOC_COUNT > FAP_BPM_ENET_BULK_ALLOC_MAX"
#endif
#endif

void uninit_rx_channel(BcmEnet_devctrl *pDevCtrl, int channel);
void uninit_tx_channel(BcmEnet_devctrl *pDevCtrl, int channel);
static int init_tx_channel(BcmEnet_devctrl *pDevCtrl, int channel);
static int init_rx_channel(BcmEnet_devctrl *pDevCtrl, int channel);

/* The number of rx and tx dma channels currently used by enet driver */
#if defined(CONFIG_BCM_GMAC)
   int cur_rxdma_channels = ENET_RX_CHANNELS_MAX;
   int cur_txdma_channels = ENET_TX_CHANNELS_MAX;
#else
   int cur_rxdma_channels = CONFIG_BCM_DEF_NR_RX_DMA_CHANNELS;
   int cur_txdma_channels = CONFIG_BCM_DEF_NR_TX_DMA_CHANNELS;
#endif
int next_channel[ENET_RX_CHANNELS_MAX];

#if defined (_CONFIG_BCM_FAP) && defined(_CONFIG_BCM_XTMCFG)
/* Add code for buffer quick free between enet and xtm - June 2010 */
RecycleFuncP xtm_fkb_recycle_hook = NULL;
#endif /* _CONFIG_BCM_XTMCFG && _CONFIG_BCM_XTMCFG*/

#if defined(_CONFIG_BCM_FAP)
#if defined(CONFIG_BCM963268)
RecycleFuncP xtm_skb_recycle_hook = NULL;
#endif
#endif

#if defined(_CONFIG_BCM_INGQOS)
extern uint32_t iqos_enable_g;
extern uint32_t iqos_debug_g;
extern uint32_t iqos_cpu_cong_g;

/* IQ status dump handler hook */
extern iqos_status_hook_t iqos_enet_status_hook_g;

static thresh_t enet_rx_dma_iq_thresh[ENET_RX_CHANNELS_MAX];

#if defined(_CONFIG_BCM_FAP)
static thresh_t enet_rx_dqm_iq_thresh[ENET_RX_CHANNELS_MAX];

/* FAP get Eth DQM queue length handler hook */
extern iqos_fap_ethRxDqmQueue_hook_t iqos_fap_ethRxDqmQueue_hook_g;

static void enet_iq_dqm_update_cong_status(BcmEnet_devctrl *pDevCtrl);
static void enet_iq_dqm_status(void);
#endif

static void enet_rx_set_iq_thresh( BcmEnet_devctrl *pDevCtrl, int chnl );
static void enet_rx_init_iq_thresh( BcmEnet_devctrl *pDevCtrl, int chnl );
static void enet_iq_dma_status(void);
static void enet_iq_status(void);
#endif

#if defined(_CONFIG_BCM_BPM)
static void enet_rx_set_bpm_alloc_trig( BcmEnet_devctrl *pDevCtrl, int chnl );
static int enet_bpm_alloc_buf_ring(BcmEnet_devctrl *pDevCtrl,int channel, uint32 num);
static void enet_bpm_free_buf_ring(BcmEnet_RxDma *rxdma, int channel);
#if defined(_CONFIG_BCM_FAP)
static uint16_t enet_bpm_dma_tx_drop_thr[ENET_TX_CHANNELS_MAX][ENET_TX_EGRESS_QUEUES_MAX];
#endif /* _CONFIG_BCM_FAP */
#endif /* _CONFIG_BCM_BPM */

static unsigned int bcmenet_rxToss = 0;
extern BcmPktDma_Bds *bcmPktDma_Bds_p;
static int bcm63xx_xmit_reclaim(void);

#ifdef DYING_GASP_API
extern unsigned char dg_ethOam_frame[64];
extern struct sk_buff *dg_skbp;
#endif


extern struct notifier_block br_notifier;

static inline int get_rxIrq( int channel )
{
    int rxIrq;

#if defined(CONFIG_BCM_GMAC)
    if ( IsGmacInfoActive && (channel == GMAC_LOG_CHAN ) )
        rxIrq = INTERRUPT_ID_GMAC_DMA_0;
    else
#endif
        rxIrq = bcmPktDma_EthSelectRxIrq(channel);

    return rxIrq;
}

#if defined(RXCHANNEL_PKT_RATE_LIMIT)
static void switch_rx_ring(BcmEnet_devctrl *pDevCtrl, int channel, int toStdBy);
/* default pkt rate is 100 pkts/100ms */
int rxchannel_rate_credit[ENET_RX_CHANNELS_MAX] = {100};
int rxchannel_rate_limit_enable[ENET_RX_CHANNELS_MAX] = {0};
int rxchannel_isr_enable[ENET_RX_CHANNELS_MAX] = {1};
int rx_pkts_from_last_jiffies[ENET_RX_CHANNELS_MAX] = {0};
static int last_pkt_jiffies[ENET_RX_CHANNELS_MAX] = {0};
int timer_pid = -1;
atomic_t timer_lock = ATOMIC_INIT(1);
static DECLARE_COMPLETION(timer_done);
#endif /* defined(RXCHANNEL_PKT_RATE_LIMIT) */

/* When TX iuDMA channel is used for determining the egress queue,
   this array provides the Tx iuDMA channel to egress queue mapping
   information */
int channel_for_queue[NUM_EGRESS_QUEUES] = {0};
int use_tx_dma_channel_for_priority = 0;
/* rx scheduling control and config variables */
int scheduling = WRR_SCHEDULING;
int max_pkts = 1280;
int weights[ENET_RX_CHANNELS_MAX] = {[0 ... (ENET_RX_CHANNELS_MAX-1)] = 1};
int weight_pkts[ENET_RX_CHANNELS_MAX] = {[0 ... (ENET_RX_CHANNELS_MAX-1)] = 320};
int pending_weight_pkts[ENET_RX_CHANNELS_MAX] = {[0 ... (ENET_RX_CHANNELS_MAX-1)] = 320};
int pending_channel[ENET_RX_CHANNELS_MAX] = {0}; /* Initialization is done during module init */
int channel_ptr = 0;
int loop_index = 0;
int global_channel = 0;
int pending_ch_tbd;
int channels_tbd;
int channels_mask;
int pending_channels_mask;
static int bcm63xx_alloc_rxdma_bds(int channel, BcmEnet_devctrl *pDevCtrl);
static int bcm63xx_alloc_txdma_bds(int channel, BcmEnet_devctrl *pDevCtrl);

#if defined(RXCHANNEL_BYTE_RATE_LIMIT)
static int channel_rx_rate_limit_enable[ENET_RX_CHANNELS_MAX] = {0};
static int rx_bytes_from_last_jiffies[ENET_RX_CHANNELS_MAX] = {0};
/* default rate in bytes/sec */
int channel_rx_rate_credit[ENET_RX_CHANNELS_MAX] = {1000000};
static int last_byte_jiffies[ENET_RX_CHANNELS_MAX] = {0};
#endif /* defined(RXCHANNEL_BYTE_RATE_LIMIT) */

extern extsw_info_t extSwInfo;
extern BcmEnet_devctrl *pVnetDev0_g;

#ifdef BCM_ENET_RX_LOG
//Budget stats are useful when testing WLAN Tx Chaining feature.
// These stats provide an idea how many packets are processed per budget. 
// More the number of packets processed per budget, more probability of creating a longer chain quickly.
typedef struct {
        uint32 budgetStats_1;
        uint32 budgetStats_2to5;
        uint32 budgetStats_6to10;
        uint32 budgetStats_11to20;
        uint32 budgetStats_21tobelowBudget;
        uint32 budgetStats_budget;
    }budgetStats;

budgetStats  gBgtStats={0};
#endif

#if defined(_CONFIG_BCM_BPM)
extern gbpm_status_hook_t gbpm_enet_status_hook_g;
#endif /* _CONFIG_BCM_BPM */
#if defined(_CONFIG_BCM_INGQOS)
#if defined(_CONFIG_BCM_FAP)
/* print the IQ DQM status */
static void enet_iq_dqm_status(void)
{
    int chnl;
    int iqDepth = 0;

    for (chnl = 0; chnl < cur_rxdma_channels; chnl++)
    {
        BcmEnet_devctrl *pDevCtrl = (BcmEnet_devctrl *)netdev_priv(vnet_dev[0]);
        BcmPktDma_EthRxDma *rxdma = &pDevCtrl->rxdma[chnl]->pktDmaRxInfo;

        if (g_Eth_rx_iudma_ownership[chnl] == HOST_OWNED)
            continue;

        if (iqos_fap_ethRxDqmQueue_hook_g == NULL)
            iqDepth = 0xFFFF;           /* Invalid value */
        else
            iqDepth = iqos_fap_ethRxDqmQueue_hook_g( chnl );

        printk("[DQM ] ENET %4d %5d %5d %5d %10u %8x\n",
                chnl,
                (int) rxdma->iqLoThreshDqm,
                (int) rxdma->iqHiThreshDqm,
                (int) iqDepth,
                (uint32_t)
#if defined(CC_IQ_STATS)
                rxdma->iqDroppedDqm,
#else
                0,
#endif
                iqos_cpu_cong_g
              );
    }
}
#endif

/* init ENET IQ thresholds */
static void enet_rx_init_iq_thresh(BcmEnet_devctrl *pDevCtrl, int chnl)
{
    BcmPktDma_EthRxDma *rxdma = &pDevCtrl->rxdma[chnl]->pktDmaRxInfo;
    int nr_rx_bds;

#if defined(_CONFIG_BCM_FAP)
    {
        nr_rx_bds = bcmPktDma_EthGetRxBds( rxdma, chnl );
        BCM_ASSERT(nr_rx_bds > 0);
        enet_rx_dma_iq_thresh[chnl].loThresh =
            (nr_rx_bds * IQ_ENET_LO_THRESH_PCT)/100;
        enet_rx_dma_iq_thresh[chnl].hiThresh =
            (nr_rx_bds * IQ_ENET_HI_THRESH_PCT)/100;
        BCM_ENET_RX_DEBUG("Enet: rxbds=%u, iqLoThresh=%u, iqHiThresh=%u\n",
                    nr_rx_bds,
                    enet_rx_dma_iq_thresh[chnl].loThresh,
                    enet_rx_dma_iq_thresh[chnl].hiThresh);
    }

    {/* DQM */
        nr_rx_bds = bcmPktDma_Bds_p->host.eth_rxdqm[chnl];

        enet_rx_dqm_iq_thresh[chnl].loThresh =
                        (nr_rx_bds * IQ_ENET_LO_THRESH_PCT)/100;
        enet_rx_dqm_iq_thresh[chnl].hiThresh =
                        (nr_rx_bds * IQ_ENET_HI_THRESH_PCT)/100;

        BCM_ENET_RX_DEBUG("Enet: dqm=%u, iqLoThresh=%u, iqHiThresh=%u\n",
                    nr_rx_bds,
                    enet_rx_dqm_iq_thresh[chnl].loThresh,
                    enet_rx_dqm_iq_thresh[chnl].hiThresh);
    }
#else
    {
        nr_rx_bds = bcmPktDma_EthGetRxBds( rxdma, chnl );

        enet_rx_dma_iq_thresh[chnl].loThresh =
                        (nr_rx_bds * IQ_ENET_LO_THRESH_PCT)/100;
        enet_rx_dma_iq_thresh[chnl].hiThresh =
                        (nr_rx_bds * IQ_ENET_HI_THRESH_PCT)/100;
    }
#endif
}


static void enet_rx_set_iq_thresh( BcmEnet_devctrl *pDevCtrl, int chnl )
{
    BcmPktDma_EthRxDma *rxdma = &pDevCtrl->rxdma[chnl]->pktDmaRxInfo;

    BCM_ENET_RX_DEBUG("Enet: chan=%d iqLoThresh=%d iqHiThresh=%d\n",
        chnl, (int) rxdma->iqLoThresh, (int) rxdma->iqHiThresh );

#if defined(_CONFIG_BCM_FAP)
    bcmPktDma_EthSetIqDqmThresh(rxdma,
                enet_rx_dqm_iq_thresh[chnl].loThresh,
                enet_rx_dqm_iq_thresh[chnl].hiThresh);
#endif

    bcmPktDma_EthSetIqThresh(rxdma,
                enet_rx_dma_iq_thresh[chnl].loThresh,
                enet_rx_dma_iq_thresh[chnl].hiThresh);
}

/* print the IQ status */
static void enet_iq_dma_status(void)
{
    int chnl;
    BcmPktDma_EthRxDma *rxdma;
    BcmEnet_devctrl *pDevCtrl = (BcmEnet_devctrl *)netdev_priv(vnet_dev[0]);

    for (chnl = 0; chnl < cur_rxdma_channels; chnl++)
    {
        rxdma = &pDevCtrl->rxdma[chnl]->pktDmaRxInfo;

#if defined(_CONFIG_BCM_FAP)
        if (g_Eth_rx_iudma_ownership[chnl] != HOST_OWNED)
            continue;
#endif

        printk("[HOST] ENET %4d %5d %5d %5d %10u %8x\n",
               chnl,
               (int) rxdma->iqLoThresh,
               (int) rxdma->iqHiThresh,
               (rxdma->numRxBds - rxdma->rxAssignedBds),
               (uint32_t)
#if defined(CC_IQ_STATS)
               rxdma->iqDropped,
#else
               0,
#endif
               iqos_cpu_cong_g
        );
    }
}

/* print the IQ status */
static void enet_iq_status(void)
{
#if defined(_CONFIG_BCM_FAP)
    enet_iq_dqm_status();
#endif
    enet_iq_dma_status();
}
#endif


int ephy_int_cnt = 1;   /* PHY ISR interrupt count */
int ephy_int_lock = 0;      /* PHY ISR count when start link handling */

/* Called from ISR context. No sleeping locks */
void ethsw_set_mac_link_down(void)
{
    int i = 0;
    uint16 v16 = 0;
    uint8 v8 = 0;
    int phyId;

    for (i = 0; i < EPHY_PORTS; i++) {
        phyId = enet_logport_to_phyid(PHYSICAL_PORT_TO_LOGICAL_PORT(i, 0));
        if(!IsExtPhyId(phyId)) {
            ethsw_phy_rreg(phyId, MII_INTERRUPT, &v16);
            if ((pVnetDev0_g->EnetInfo[0].sw.port_map & (1U<<i)) != 0) {
                if (v16 & MII_INTR_LNK) {
                    ethsw_phy_rreg(phyId, MII_BMSR, &v16);
                    if (!(v16 & BMSR_LSTATUS)) {
                        ethsw_rreg(PAGE_CONTROL, REG_PORT_STATE + i, &v8, 1);
                        if (v8 & REG_PORT_STATE_LNK) {
                            v8 &= (~REG_PORT_STATE_LNK);
                            ethsw_wreg(PAGE_CONTROL, REG_PORT_STATE + i, &v8, 1);
                        }
                    }
                }
            }
        }
    }
}

static FN_HANDLER_RT bcm63xx_ephy_isr(int irq, void * dev_id)
{
    /* PHY Interrupt is disabled here. */
    ethsw_set_mac_link_down();
    ephy_int_cnt++;

    /* re-enable PHY interrupt */
    bcmeapiPhyIntEnable(1);
    return BCM_IRQ_HANDLED;
}

#if defined(CONFIG_BCM963268) || defined(CONFIG_BCM96828)
/*
 * bcm63xx_gphy_isr: Acknowledge Gphy interrupt.
 */
FN_HANDLER_RT bcm63xx_gphy_isr(int irq, void * dev_id)
{
    /* Link Interrupt is disabled here. */
    ethsw_set_mac_link_down();
    ephy_int_cnt++;

    /* re-enable PHY interrupt */
    bcmeapiPhyIntEnable(1);
    return BCM_IRQ_HANDLED;
}
#endif

int bcmeapi_map_interrupt(BcmEnet_devctrl *pDevCtrl)
{
#if defined(CONFIG_BCM963381)
    /* 963381A0 has hardware bug on interrupt; Don't register interrupt handler */
    if(pDevCtrl->chipId == 0x3381 && pDevCtrl->chipRev == 0xa0)
    {
        return 0;
    }
#endif
 
    BcmHalMapInterrupt(bcm63xx_ephy_isr, (unsigned int)pDevCtrl, INTERRUPT_ID_EPHY);
#if defined(CONFIG_BCM963268)
    BcmHalMapInterrupt(bcm63xx_gphy_isr, (unsigned int)pDevCtrl, INTERRUPT_ID_GPHY);
#endif
#if defined(CONFIG_BCM96828)
    BcmHalMapInterrupt(bcm63xx_gphy_isr, (unsigned int)pDevCtrl, INTERRUPT_ID_GPHY0);
    BcmHalMapInterrupt(bcm63xx_gphy_isr, (unsigned int)pDevCtrl, INTERRUPT_ID_GPHY1);
#endif
#if defined(CONFIG_BCM_GMAC)
    BcmHalMapInterrupt(bcm63xx_gmac_isr, (unsigned int)pDevCtrl, INTERRUPT_ID_GMAC);
#endif
    return BCMEAPI_INT_MAPPED_INTPHY;
}

int bcmeapi_create_vport(struct net_device *dev)
{
    BcmEnet_devctrl *pDevCtrl = netdev_priv(dev);

    dev->mtu = ENET_MAX_MTU_PAYLOAD_SIZE; /* GPON - Explicitly assign the MTU size based on buffer size allocated */
    pDevCtrl->default_txq = 0;
    return 0;
}

#if defined(CONFIG_BCM_GMAC)
#if defined(_CONFIG_BCM_FAP)
volatile int fapDrv_getEnetRxEnabledStatus( int channel );
#endif /* _CONFIG_BCM_FAP */

void enet_rxdma_channel_enable(int chan)
{
    BcmEnet_devctrl *pDevCtrl = netdev_priv(vnet_dev[0]);
    BcmEnet_RxDma *rxdma = pDevCtrl->rxdma[chan];

    /* Enable the Rx channel */
    bcmPktDma_EthRxEnable(&rxdma->pktDmaRxInfo);

#if defined(_CONFIG_BCM_FAP)
    /* Wait for Enet RX to be enabled in FAP */
    while(!fapDrv_getEnetRxEnabledStatus( chan ));
#endif
}

void enet_txdma_channel_enable(int chan)
{
    BcmEnet_devctrl *pDevCtrl = netdev_priv(vnet_dev[0]);

    /* Enable the Tx channel */
    bcmPktDma_EthTxEnable(pDevCtrl->txdma[chan]);
}

int enet_del_rxdma_channel(int chan)
{
    BcmEnet_devctrl *pDevCtrl = netdev_priv(vnet_dev[0]);
    BcmEnet_RxDma *rxdma = pDevCtrl->rxdma[chan];

    /* Stop the RXDMA channel */
    if (rxdma->pktDmaRxInfo.rxDma->cfg & DMA_ENABLE)
    {
        rxdma->pktDmaRxInfo.rxDma->cfg = DMA_PKT_HALT;
        while(rxdma->pktDmaRxInfo.rxDma->cfg & DMA_ENABLE)
        {
            rxdma->pktDmaRxInfo.rxDma->cfg = DMA_PKT_HALT;
        }
    }

    /* Disable the interrupts */
    bcmPktDma_BcmHalInterruptDisable(chan, rxdma->rxIrq);

    /* Disable the Rx channel */
    bcmPktDma_EthRxDisable(&rxdma->pktDmaRxInfo);

#if defined(_CONFIG_BCM_FAP)
    /* Wait for Enet RX to be disabled in FAP */
    while(fapDrv_getEnetRxEnabledStatus( chan ));
#endif

    /*free the BD ring */
    uninit_rx_channel(pDevCtrl, chan);

    return 0;
}

int enet_add_txdma_channel(int chan)
{
    BcmEnet_devctrl *pDevCtrl = netdev_priv(vnet_dev[0]);

    if (init_tx_channel(pDevCtrl, chan))
    {
        uninit_tx_channel(pDevCtrl, chan);
        return -1;
    }

    /* Enable the Tx channel */
    bcmPktDma_EthTxEnable(pDevCtrl->txdma[chan]);

    return 0;
}

int enet_del_txdma_channel(int chan)
{
    BcmEnet_devctrl *pDevCtrl = netdev_priv(vnet_dev[0]);

    /* Disable the Tx channel */
    bcmPktDma_EthTxDisable(pDevCtrl->txdma[chan]);

    /*Un-allocate the BD ring */
    uninit_tx_channel(pDevCtrl, chan);

    return 0;
}
#endif /* CONFIG_BCM_GMAC */


/******************************************************************************
* Function: enetDmaStatus (for debug); Not called from anywhere now.          *
* Description: Dumps information about the status of the ENET IUDMA channel   *
******************************************************************************/
void enetDmaStatus(int channel)
{
    BcmPktDma_EthRxDma *rxdma;
    BcmPktDma_EthTxDma *txdma;

    rxdma = &g_pEnetDevCtrl->rxdma[channel]->pktDmaRxInfo;
    txdma = g_pEnetDevCtrl->txdma[channel];

    printk("ENET IUDMA INFO CH %d\n", channel);
    if(channel < cur_rxdma_channels)
    {
        printk("enet dmaStatus: rxdma 0x%x, cfg at 0x%x\n",
            (unsigned int)rxdma, (unsigned int)&rxdma->rxDma->cfg);


        printk("RXDMA STATUS: HeadIndex: %d TailIndex: %d numRxBds: %d rxAssignedBds: %d\n",
                  rxdma->rxHeadIndex, rxdma->rxTailIndex,
                  rxdma->numRxBds, rxdma->rxAssignedBds);

        printk("RXDMA CFG: cfg: 0x%lx intStat: 0x%lx intMask: 0x%lx\n",
                     rxdma->rxDma->cfg,
                     rxdma->rxDma->intStat,
                     rxdma->rxDma->intMask);
    }

    if(channel < cur_txdma_channels)
    {

        printk("TXDMA STATUS: HeadIndex: %d TailIndex: %d txFreeBds: %d\n",
                  txdma->txHeadIndex,
                  txdma->txTailIndex,
                  txdma->txFreeBds);

        printk("TXDMA CFG: cfg: 0x%lx intStat: 0x%lx intMask: 0x%lx\n",
                     txdma->txDma->cfg,
                     txdma->txDma->intStat,
                     txdma->txDma->intMask);
    }
}

static int set_cur_rxdma_channels(int num_channels)
{
    int i, j, tmp_channels, total_of_weights = 0;
    BcmEnet_RxDma *rxdma;
    BcmEnet_devctrl *pDevCtrl = netdev_priv(vnet_dev[0]);

    if (cur_rxdma_channels == num_channels) {
        BCM_ENET_DEBUG("Not changing current rxdma channels"
                       "as it is same as what is given \n");
        return 0;
    }
    if (num_channels > ENET_RX_CHANNELS_MAX) {
        BCM_ENET_DEBUG("Not changing current rxdma channels"
                       "as it is greater than MAX (%d) \n",ENET_RX_CHANNELS_MAX);
        return 0;
    }

    /* Increasing the number of Rx channels */
    if (num_channels > cur_rxdma_channels) {
        for (i = cur_rxdma_channels; i < num_channels; i++) {
            /* Init the Rx Channel. */
            if (init_rx_channel(pDevCtrl, i)) {
                for (j = cur_rxdma_channels; j < i; j++) {
                    uninit_rx_channel(pDevCtrl, j);
                }
                return -1;
            }
        }

        for (i = cur_rxdma_channels; i < num_channels; i++) {
            rxdma = pDevCtrl->rxdma[i];
            bcmPktDma_EthRxEnable(&rxdma->pktDmaRxInfo);
            bcmPktDma_BcmHalInterruptEnable(i, rxdma->rxIrq);
            rxdma->pktDmaRxInfo.rxDma->cfg |= DMA_ENABLE;
        }

        /* Set the current Rx DMA channels to given num_channels */
        cur_rxdma_channels = num_channels;

    } else { /* Decreasing the number of Rx channels */
        /* Stop the DMA channels */
        for (i = num_channels; i < cur_rxdma_channels; i++) {
            rxdma = pDevCtrl->rxdma[i];
            rxdma->pktDmaRxInfo.rxDma->cfg = 0;
        }

        /* Disable the interrupts */
        for (i = num_channels; i < cur_rxdma_channels; i++) {
            rxdma = pDevCtrl->rxdma[i];
            bcmPktDma_BcmHalInterruptDisable(i, rxdma->rxIrq);
            bcmPktDma_EthRxDisable(&rxdma->pktDmaRxInfo);
        }

        /* Remember the cur_rxdma_channels as we are changing it now */
        tmp_channels = cur_rxdma_channels;

        /* Set the current Rx DMA channels to given num_channels */
        /* Set this before unint_rx_channel, so that ISR will not
           try to service a channel which is uninitialized. */
        cur_rxdma_channels = num_channels;

        /* Free the buffers and BD ring */
        for (i = num_channels; i < tmp_channels; i++) {
            uninit_rx_channel(pDevCtrl, i);
        }
    }

    /* Recalculate the WRR weights based on cur_rxdma_channels */
    for(i=0; i<cur_rxdma_channels; i++) {
        total_of_weights += weights[i];
    }
    for(i=0; i<cur_rxdma_channels; i++) {
        weight_pkts[i] = (max_pkts/total_of_weights) * weights[i];
        pending_weight_pkts[i] = weight_pkts[i];
        BCM_ENET_DEBUG("weight[%d]_pkts: %d \n", i, weight_pkts[i]);
        pending_channel[i] = i;
    }
    global_channel = channel_ptr = loop_index = 0;
    pending_ch_tbd = cur_rxdma_channels;

    return 0;
}

static void setup_rxdma_channel(int channel)
{
    BcmEnet_RxDma *rxdma = global.pVnetDev0_g->rxdma[channel];
    volatile DmaRegs *dmaCtrl = get_dmaCtrl( channel );
    int phy_chan = get_phy_chan( channel );
    DmaStateRam *StateRam = (DmaStateRam *)&dmaCtrl->stram.s[phy_chan*2];

    memset(StateRam, 0, sizeof(DmaStateRam));

    BCM_ENET_DEBUG("Setup rxdma channel %d, baseDesc 0x%x\n", (int)channel,
        (unsigned int)VIRT_TO_PHY((uint32 *)rxdma->pktDmaRxInfo.rxBds));

        rxdma->pktDmaRxInfo.rxDma->cfg = 0;
        rxdma->pktDmaRxInfo.rxDma->maxBurst = DMA_MAX_BURST_LENGTH;
        rxdma->pktDmaRxInfo.rxDma->intMask = 0;
        rxdma->pktDmaRxInfo.rxDma->intStat = DMA_DONE | DMA_NO_DESC | DMA_BUFF_DONE;
        rxdma->pktDmaRxInfo.rxDma->intMask = DMA_DONE | DMA_NO_DESC | DMA_BUFF_DONE;

    dmaCtrl->stram.s[phy_chan * 2].baseDescPtr =
            (uint32)VIRT_TO_PHY((uint32 *)rxdma->pktDmaRxInfo.rxBds);
}

static void setup_txdma_channel(int channel)
{
    DmaStateRam *StateRam;
    BcmPktDma_EthTxDma *txdma;
    volatile DmaRegs *dmaCtrl = get_dmaCtrl( channel );
    int phy_chan = get_phy_chan( channel );
    txdma = global.pVnetDev0_g->txdma[channel];

    StateRam = (DmaStateRam *)&dmaCtrl->stram.s[(phy_chan*2) + 1];
    memset(StateRam, 0, sizeof(DmaStateRam));

    BCM_ENET_DEBUG("setup_txdma_channel: %d, baseDesc 0x%x\n",
        (int)channel, (unsigned int)VIRT_TO_PHY((uint32 *)txdma->txBds));

    txdma->txDma->cfg = 0;
#if defined(DBL_DESC)
    txdma->txDma->maxBurst = DMA_MAX_BURST_LENGTH | DMA_DESCSIZE_SEL;
#else
    txdma->txDma->maxBurst = DMA_MAX_BURST_LENGTH;
#endif
    txdma->txDma->intMask = 0;

    dmaCtrl->stram.s[(phy_chan * 2) + 1].baseDescPtr =
        (uint32)VIRT_TO_PHY((uint32 *)txdma->txBds);
}
/*
 * init_rx_channel: Initialize Rx DMA channel
 */
static int init_rx_channel(BcmEnet_devctrl *pDevCtrl, int channel)
{
    BcmEnet_RxDma *rxdma;
    volatile DmaRegs *dmaCtrl = get_dmaCtrl( channel );
    int phy_chan = get_phy_chan( channel );

    TRACE(("bcm63xxenet: init_rx_channel\n"));
    BCM_ENET_DEBUG("Initializing Rx channel %d \n", channel);

    /* setup the RX DMA channel */
    rxdma = pDevCtrl->rxdma[channel];

    /* init rxdma structures */
    rxdma->pktDmaRxInfo.rxDma = &dmaCtrl->chcfg[phy_chan * 2];
    rxdma->rxIrq = get_rxIrq( channel );

    /* disable the interrupts from device */
    bcmPktDma_BcmHalInterruptDisable(channel, rxdma->rxIrq);

    /* Reset the DMA channel */
    dmaCtrl->ctrl_channel_reset = 1 << (phy_chan * 2);
    dmaCtrl->ctrl_channel_reset = 0;

    /* allocate RX BDs */
#if defined(ENET_RX_BDS_IN_PSM)
    if (!rxdma->bdsAllocated)
#endif
    {
        if (bcm63xx_alloc_rxdma_bds(channel,pDevCtrl) < 0)
            return -1;
    }

   printk("ETH Init: Ch:%d - %d rx BDs at 0x%x\n",
          channel, rxdma->pktDmaRxInfo.numRxBds, (unsigned int)rxdma->pktDmaRxInfo.rxBds);

    setup_rxdma_channel( channel );

    bcmPktDma_EthInitRxChan(rxdma->pktDmaRxInfo.numRxBds, &rxdma->pktDmaRxInfo);

#if defined(_CONFIG_BCM_INGQOS)
    enet_rx_set_iq_thresh( pDevCtrl, channel );
#endif
#if defined(_CONFIG_BCM_BPM)
    enet_rx_set_bpm_alloc_trig( pDevCtrl, channel );
#endif

    /* initialize the receive buffers */
    if (init_buffers(pDevCtrl, channel)) {
        printk(KERN_NOTICE CARDNAME": Low memory.\n");
        uninit_buffers(pDevCtrl->rxdma[channel]);
        return -ENOMEM;
    }
#if defined(_CONFIG_BCM_BPM)
    gbpm_resv_rx_buf( GBPM_PORT_ETH, channel, rxdma->pktDmaRxInfo.numRxBds,
        (rxdma->pktDmaRxInfo.numRxBds * BPM_ENET_ALLOC_TRIG_PCT/100) );
#endif

//    bcm63xx_dump_rxdma(channel, rxdma);
    return 0;
}

/*
 * uninit_rx_channel: un-initialize Rx DMA channel
 */
void uninit_rx_channel(BcmEnet_devctrl *pDevCtrl, int channel)
{
    BcmEnet_RxDma *rxdma;
    volatile DmaRegs *dmaCtrl = get_dmaCtrl( channel );
    int phy_chan = get_phy_chan( channel );

    TRACE(("bcm63xxenet: init_rx_channel\n"));
    BCM_ENET_DEBUG("un-initializing Rx channel %d \n", channel);

    /* setup the RX DMA channel */
    rxdma = pDevCtrl->rxdma[channel];

#if defined(_CONFIG_BCM_FAP)
#if defined(CONFIG_BCM_GMAC)
    bcmPktDma_EthUnInitRxChan(&rxdma->pktDmaRxInfo);
#endif
#else
    uninit_buffers(rxdma);
#endif

    /* Reset the DMA channel */
    dmaCtrl->ctrl_channel_reset = 1 << (phy_chan * 2);
    dmaCtrl->ctrl_channel_reset = 0;

#if !defined(ENET_RX_BDS_IN_PSM)
    /* remove the rx bd ring & rxBdsStdBy */
    if (rxdma->pktDmaRxInfo.rxBdsBase) {
        kfree((void *)rxdma->pktDmaRxInfo.rxBdsBase);
    }
#endif

//    bcm63xx_dump_rxdma(channel, rxdma);
}

#if defined(_CONFIG_BCM_BPM) && defined(_CONFIG_BCM_FAP)
static void enet_bpm_set_tx_drop_thr( BcmEnet_devctrl *pDevCtrl, int chnl )
{
    BcmPktDma_EthTxDma *txdma = pDevCtrl->txdma[chnl];
    int q;
    BCM_ENET_DEBUG("Enet: BPM Set Tx Chan=%d Owner=%d\n", chnl,
        g_Eth_tx_iudma_ownership[chnl]);
    if (g_Eth_tx_iudma_ownership[chnl] == HOST_OWNED)
    {
        for (q=0; q < ENET_TX_EGRESS_QUEUES_MAX; q++)
            txdma->txDropThr[q] = enet_bpm_dma_tx_drop_thr[chnl][q];
    }

    bcmPktDma_EthSetTxChanBpmThresh(txdma,
        (uint16 *) &enet_bpm_dma_tx_drop_thr[chnl]);
}
#endif

/*
 * init_tx_channel: Initialize Tx DMA channel
 */
static int init_tx_channel(BcmEnet_devctrl *pDevCtrl, int channel)
{
    BcmPktDma_EthTxDma *txdma;
    volatile DmaRegs *dmaCtrl = get_dmaCtrl( channel );
    int phy_chan = get_phy_chan( channel );

    TRACE(("bcm63xxenet: init_txdma\n"));
    BCM_ENET_DEBUG("Initializing Tx channel %d \n", channel);

    /* Reset the DMA channel */
    dmaCtrl->ctrl_channel_reset = 1 << ((phy_chan * 2) + 1);
    dmaCtrl->ctrl_channel_reset = 0;

    txdma = pDevCtrl->txdma[channel];
    txdma->txDma = &dmaCtrl->chcfg[(phy_chan * 2) + 1];

    /* allocate and assign tx buffer descriptors */
#if defined(ENET_TX_BDS_IN_PSM)
    if (!txdma->bdsAllocated)
#endif
    {
        /* allocate TX BDs */
        if (bcm63xx_alloc_txdma_bds(channel,pDevCtrl) < 0)
        {
            printk("Allocate Tx BDs Failed ! ch %d \n", channel);
            return -1;
        }
    }

    setup_txdma_channel( channel );

    printk("ETH Init: Ch:%d - %d tx BDs at 0x%x\n", channel, txdma->numTxBds, (unsigned int)txdma->txBds);

    bcmPktDma_EthInitTxChan(txdma->numTxBds, txdma);
#if defined(_CONFIG_BCM_BPM) && defined(_CONFIG_BCM_FAP)
    enet_bpm_set_tx_drop_thr( pDevCtrl, channel );
#endif

//    bcm63xx_dump_txdma(channel, txdma);
    return 0;
}

/*
 * uninit_tx_channel: un-initialize Tx DMA channel
 */
void uninit_tx_channel(BcmEnet_devctrl *pDevCtrl, int channel)
{
    BcmPktDma_EthTxDma *txdma;
    volatile DmaRegs *dmaCtrl = get_dmaCtrl( channel );
    int phy_chan = get_phy_chan( channel );

    TRACE(("bcm63xxenet: uninit_tx_channel\n"));
    BCM_ENET_DEBUG("un-initializing Tx channel %d \n", channel);

    txdma = pDevCtrl->txdma[channel];

#if defined(CONFIG_BCM_GMAC)
    bcmPktDma_EthUnInitTxChan(txdma);
#endif

    /* Reset the DMA channel */
    dmaCtrl->ctrl_channel_reset = 1 << ((phy_chan * 2) + 1);
    dmaCtrl->ctrl_channel_reset = 0;

#if !defined(ENET_TX_BDS_IN_PSM)
    /* remove the tx bd ring */
    if (txdma->txBdsBase) {
        kfree((void *)txdma->txBdsBase);
    }
#endif
//    bcm63xx_dump_txdma(channel, txdma);
}

int enet_add_rxdma_channel(int chan)
{
    BcmEnet_devctrl *pDevCtrl = netdev_priv(vnet_dev[0]);
    BcmEnet_RxDma *rxdma = pDevCtrl->rxdma[chan];

    /* Stop the RXDMA (just a precaution) */
    if (rxdma->pktDmaRxInfo.rxDma->cfg & DMA_ENABLE)
    {
        rxdma->pktDmaRxInfo.rxDma->cfg = DMA_PKT_HALT;
        while(rxdma->pktDmaRxInfo.rxDma->cfg & DMA_ENABLE)
        {
            rxdma->pktDmaRxInfo.rxDma->cfg = DMA_PKT_HALT;
        }
    }

    /* Allocate the BD ring and buffers */
    if (init_rx_channel(pDevCtrl, chan))
    {
        uninit_rx_channel(pDevCtrl, chan);
        return -1;
    }

    /* Enable the interrupts */
    bcmPktDma_BcmHalInterruptEnable(chan, rxdma->rxIrq);

    return 0;
}

static int set_cur_txdma_channels(int num_channels)
{
    int i, j, tmp_channels;
    BcmEnet_devctrl *pDevCtrl = netdev_priv(vnet_dev[0]);

#if !defined(CONFIG_BCM_GMAC) && defined(_CONFIG_BCM_FAP)
#if defined(CONFIG_BCM_PKTDMA_TX_SPLITTING) || defined(CONFIG_BCM963268) ||  defined(CONFIG_BCM96828)
    if (num_channels != ENET_TX_CHANNELS_MAX)
#else
    if (num_channels != 1)
#endif
    {
        BCM_LOG_ERROR(BCM_LOG_ID_ENET, "Invalid number of Tx channels : %u\n",
                      num_channels);
        return -EINVAL;
    }
#endif

    if (cur_txdma_channels == num_channels) {
        BCM_ENET_DEBUG("Not changing current txdma channels"
                       "as it is same as what is given \n");
        return 0;
    }
    if (num_channels > ENET_TX_CHANNELS_MAX) {
        BCM_ENET_DEBUG("Not changing current txdma channels"
                       "as it is greater than max (%d) \n",ENET_TX_CHANNELS_MAX);
        return 0;
    }

    /* Increasing the number of Tx channels */
    if (num_channels > cur_txdma_channels) {
        /* Initialize the new channels */
        for (i = cur_txdma_channels; i < num_channels; i++) {
            if (init_tx_channel(pDevCtrl, i)) {
                for (j = cur_txdma_channels; j < i; j++) {
                    uninit_tx_channel(pDevCtrl, j);
                }
                return -1;
            }
        }

        for (i = cur_txdma_channels; i < num_channels; i++) {
            bcmPktDma_EthTxEnable(pDevCtrl->txdma[i]);
        }

        /* Set the current Tx DMA channels to given num_channels */
        cur_txdma_channels = num_channels;

    } else { /* Decreasing the number of Tx channels */
        for (i = num_channels; i < cur_txdma_channels; i++) {
            bcmPktDma_EthTxDisable(pDevCtrl->txdma[i]);
        }

        /* Remember the cur_txdma_channels as we are changing it now */
        tmp_channels = cur_txdma_channels;

        /* Set the current Tx DMA channels to given num_channels */
        cur_txdma_channels = num_channels;

        /*Un-allocate the BD ring */
        for (i = num_channels; i < tmp_channels; i++) {
            uninit_tx_channel(pDevCtrl, i);
        }
    }

    return 0;
}

int bcmeapi_set_num_rxques(struct ethctl_data *ethctl)
{
    int val = 0;
    if (ethctl->num_channels <= ENET_RX_CHANNELS_MAX) {
        if (ethctl->num_channels < ENET_RX_CHANNELS_MAX) {
            printk("Warning: The switch buffers will fill up "
                    "if the switch configuration is not modified "
                    "to not to send packets on disabled rx dma "
                    "channels!!! \n");
            printk("Continuing with set_rxdma_channels... \n");
        }
        if (set_cur_rxdma_channels(ethctl->num_channels)) {
            printk("Error in setting cur_rxdma_channels \n");
            return -EFAULT;
        }
    } else {
        printk("Max: %d \n", ENET_RX_CHANNELS_MAX);
        val = -EINVAL;
    }
    return val;
}

int bcmeapi_get_num_rxques(struct ethctl_data *ethctl)
{
    ethctl->ret_val = cur_rxdma_channels;
    return 0;
}

int bcmeapi_set_num_txques(struct ethctl_data *ethctl)
{
    int val = 0;
    if (ethctl->num_channels <= ENET_TX_CHANNELS_MAX) {
        if (ethctl->num_channels > 1) {
            printk("Warning: If the DUT does not support "
                    "un-aligned Tx buffers, you should not be "
                    "doing this!!! \n");
            printk("Continuing with set_txdma_channels... \n");
        }
        if (set_cur_txdma_channels(ethctl->num_channels)) {
            printk("Error in setting cur_txdma_channels \n");
            return -EFAULT;
        }
    } else {
        printk("Max: %d \n", ENET_TX_CHANNELS_MAX);
        val = -EINVAL;
    }
    return val;
}

int bcmeapi_get_num_txques(struct ethctl_data *ethctl)
{
    ethctl->ret_val = cur_txdma_channels;
    return 0;
}

int bcmeapi_config_queue(struct ethswctl_data *e)
{
    {
        struct ethswctl_data e2;
        int iudma_ch = e->val;
        int retval = 0;
        int j;

        if(e->port < BP_MAX_SWITCH_PORTS)
        {
            if(TYPE_GET == e->type)
            {
                e2.type = TYPE_GET;
                e2.port = e->port;
                e2.priority = 0;
                retval = bcmeapi_ioctl_ethsw_cosq_port_mapping(&e2);
                if(retval >= 0)
                {
                    printk("eth%d mapped to iuDMA%d\n", e2.port, retval);
                    return(0);
                }
            }
            else if(iudma_ch < ENET_RX_CHANNELS_MAX)
            {   /* TYPE_SET */
                /* The equivalent of "ethswctl -c cosq -p port -q {j} -v {iudma_ch}" */
                /* This routes packets of all priorities on eth 'port' to egress queue 'iudma_ch' */
                e2.port = e->port;
                for(j = 0; j <= MAX_PRIORITY_VALUE; j++)
                {
                    e2.type = TYPE_SET;
                    e2.priority = j;
                    e2.queue = iudma_ch;

                    retval = bcmeapi_ioctl_ethsw_cosq_port_mapping(&e2);
                }
                if(retval == 0)
                {
                    printk("eth%d mapped to iuDMA%d\n", e->port, iudma_ch);
                    return(0);
                }
            }
            else
                printk("Invalid iuDMA channel number %d\n", iudma_ch);
        }
        else
            printk("Invalid Ethernet port number %d\n", e->port);
    }
    return(BCM_E_ERROR);
}

void bcmeapi_dump_queue(struct ethswctl_data *e, BcmEnet_devctrl *pDevCtrl)
{
    {
        BcmPktDma_LocalEthRxDma * rxdma;
        int                       channel;

        for(channel = 0; channel < ENET_RX_CHANNELS_MAX; channel++)
        {
            rxdma = &pDevCtrl->rxdma[channel]->pktDmaRxInfo;
#if defined(CONFIG_BCM_PKTDMA_RX_SPLITTING)
            if(rxdma->rxOwnership != HOST_OWNED) continue;
#endif
            if(!rxdma->rxEnabled) continue;

            printk("\n\nENET RXDMA STATUS Ch%d: HeadIndex: %d TailIndex: %d numRxBds: %d rxAssignedBds: %d rxToss: %u\n",
                    channel, rxdma->rxHeadIndex, rxdma->rxTailIndex,
                    rxdma->numRxBds, rxdma->rxAssignedBds, bcmenet_rxToss);

            printk("     RXDMA CFG Ch%d: cfg: 0x%lx intStat: 0x%lx intMask: 0x%lx\n\n",
                    channel, rxdma->rxDma->cfg,
                    rxdma->rxDma->intStat,
                    rxdma->rxDma->intMask);
        }
    }

#if !defined(_CONFIG_BCM_FAP) || defined(CONFIG_BCM_PKTDMA_TX_SPLITTING)
    {
        BcmPktDma_LocalEthTxDma * txdma;
        int                       channel;

#if defined(CONFIG_BCM_PKTDMA_TX_SPLITTING)
        channel = global.pVnetDev0_g->enetTxChannel;
#else
        for(channel = 0; channel < ENET_TX_CHANNELS_MAX; channel++)
#endif
        {
            txdma = pDevCtrl->txdma[channel];
            if(txdma->txEnabled)
            {

                printk("\nENET TXDMA STATUS Ch%d: HeadIndex: %d TailIndex: %d txFreeBds: %d BDs at: 0x%08x\n",
                        channel, txdma->txHeadIndex,
                        txdma->txTailIndex,
                        txdma->txFreeBds,
                        (unsigned int)&pDevCtrl->txdma[channel]->txBds[0]);

                printk("     TXDMA CFG Ch%d: cfg: 0x%lx intStat: 0x%lx intMask: 0x%lx\n\n\n",
                        channel, txdma->txDma->cfg,
                        txdma->txDma->intStat,
                                    txdma->txDma->intMask);
                            }
                        }
                    }
#endif  /*  !defined(_CONFIG_BCM_FAP) || defined(CONFIG_BCM_PKTDMA_TX_SPLITTING) */
}
int bcmeapi_ioctl_test_config(struct ethswctl_data *e)
{
    if (e->type == TYPE_GET)
    {
        int ret_val = 0;
        if (e->sub_type == SUBTYPE_ISRCFG)
        {
#if defined(RXCHANNEL_PKT_RATE_LIMIT)
            BCM_ENET_DEBUG("Given channel: 0x%02x \n ", e->channel);
            ret_val = rxchannel_isr_enable[e->channel];
#endif
        }
        else if (e->sub_type == SUBTYPE_RXDUMP)
        {
            ret_val = global.dump_enable;
        }

        if (copy_to_user((void*)(&e->ret_val), (void*)&ret_val, sizeof(int)))
        {
            return -EFAULT;
        }
        BCM_ENET_DEBUG("e->ret_val: 0x%02x \n ", e->ret_val);
    }
    else
    {
        if (e->sub_type == SUBTYPE_ISRCFG)
        {
#if defined(RXCHANNEL_PKT_RATE_LIMIT)
            BcmEnet_RxDma *rxdma;
            BcmEnet_devctrl *pDevCtrl = netdev_priv(vnet_dev[0]);
            BcmPktDma_LocalEthRxDma * local_rxdma;

            BCM_ENET_DEBUG("Given channel: 0x%02x \n ", e->channel);
            BCM_ENET_DEBUG("Given val: %d \n ", e->val);
            rxchannel_isr_enable[e->channel] = e->val;

            /* Enable/Disable the interrupts for given RX DMA channel */
            rxdma = pDevCtrl->rxdma[e->channel];
            local_rxdma = &rxdma->pktDmaRxInfo;
            if (e->val)
            {
                bcmPktDma_EthRxEnable(local_rxdma);
                bcmPktDma_BcmHalInterruptEnable(e->channel, rxdma->rxIrq);
            }
            else
            {
                bcmPktDma_BcmHalInterruptDisable(e->channel, rxdma->rxIrq);
                bcmPktDma_EthRxDisable(local_rxdma);
            }
#endif
        }
        else if (e->sub_type == SUBTYPE_RXDUMP)
        {
            global.dump_enable = e->val;
#ifdef BCM_ENET_RX_LOG
    #define PERCENT(a) (((gBgtStats.a)*100)/totalpkts)
            printk("Dumping BudgetStats\n");
            printk("budgetStats_1 %lu budgetStats_2to5 %lu budgetStats_6to10 %lu budgetStats_11to20 %lu budgetStats_21tobelowBudget %lu budgetStats_budget %lu\n", 
                   gBgtStats.budgetStats_1, gBgtStats.budgetStats_2to5, gBgtStats.budgetStats_6to10, 
                   gBgtStats.budgetStats_11to20, gBgtStats.budgetStats_21tobelowBudget, gBgtStats.budgetStats_budget);
            {
                uint32 totalpkts=0;
                totalpkts= gBgtStats.budgetStats_1 + gBgtStats.budgetStats_2to5 + gBgtStats.budgetStats_6to10 + 
                           gBgtStats.budgetStats_11to20 + gBgtStats.budgetStats_21tobelowBudget + gBgtStats.budgetStats_budget;
                if (totalpkts != 0)
                {
                    printk("budgetStatsPer_1 %lu budgetStatsPer_2to5 %lu budgetStatsPer_6to10 %lu budgetStatsPer_11to20 %lu budgetStatsPer_21tobelowBudget %lu budgetStatsPer_budget %lu\n",
                           PERCENT(budgetStats_1), PERCENT(budgetStats_2to5), PERCENT(budgetStats_6to10), PERCENT(budgetStats_11to20), PERCENT(budgetStats_21tobelowBudget),
                           PERCENT(budgetStats_budget));
                }
            }
#endif			
        }
    }

    /* Note : handling of RESETMIB is moved outside of this function into the caller */
    if (e->sub_type == SUBTYPE_RESETSWITCH)
    {
    }
    return 0;
}

#if defined(RXCHANNEL_PKT_RATE_LIMIT)
int bcmeapi_ioctl_rx_pkt_rate_limit_config(struct ethswctl_data *e)
{
    BcmEnet_RxDma *rxdma;
    BcmEnet_devctrl *pDevCtrl = netdev_priv(vnet_dev[0]);

    BCM_ENET_DEBUG("Given channel: %d \n ", e->channel);
    if (e->channel >= ENET_RX_CHANNELS_MAX || e->channel < 0) {
        return -EINVAL;
    }
    if (e->type == TYPE_GET) {
        if (copy_to_user((void*)(&e->ret_val),
            (void*)&rxchannel_rate_limit_enable[e->channel], sizeof(int))) {
            return -EFAULT;
        }
        BCM_ENET_DEBUG("e->ret_val: 0x%02x \n ", e->ret_val);
    } else {
        BCM_ENET_DEBUG("Given rate_enable_cfg: %d \n ", e->val);
        rxdma = pDevCtrl->rxdma[e->channel];
        ENET_RX_LOCK();
        rxchannel_rate_limit_enable[e->channel] = e->val;
        if ((e->val == 0) && (rxchannel_isr_enable[e->channel] == 0)) {
            switch_rx_ring(pDevCtrl, e->channel, 0);
            bcmPktDma_BcmHalInterruptEnable(e->channel, rxdma->rxIrq);
            rxchannel_isr_enable[e->channel] = 1;
        }
        ENET_RX_UNLOCK();
    }

    return 0;
}

int bcmeapi_ioctl_rx_pkt_rate_config(struct ethswctl_data *e)
{
    BCM_ENET_DEBUG("Given channel: 0x%02x \n ", e->channel);
    if (e->type == TYPE_GET) {
        int value = rxchannel_rate_credit[e->channel] * 10;
        if (copy_to_user((void*)(&e->ret_val), (void*)&value, sizeof(int))) {
            return -EFAULT;
        }
        BCM_ENET_DEBUG("e->ret_val: 0x%02x \n ", e->ret_val);
    } else {
        BCM_ENET_DEBUG("Given rate: %d \n ", e->val);
        rxchannel_rate_credit[e->channel] = (e->val/10 > 1)?(e->val/10):1;
    }

    return 0;
}
#endif /* defined(RXCHANNEL_PKT_RATE_LIMIT) */

#if defined(RXCHANNEL_BYTE_RATE_LIMIT)
int bcmeapi_ioctl_rx_rate_config(struct ethswctl_data *e)
{
    BCM_ENET_DEBUG("Given channel: 0x%02x \n ", e->channel);
    if (e->type == TYPE_GET) {
        if (copy_to_user((void*)(&e->ret_val),
            (void*)&channel_rx_rate_credit[e->channel], sizeof(int))) {
            return -EFAULT;
        }
        BCM_ENET_DEBUG("e->ret_val: 0x%02x \n ", e->ret_val);
    } else {
        BCM_ENET_DEBUG("Given rate: %d \n ", e->val);
        channel_rx_rate_credit[e->channel] = e->val;
    }

    return 0;
}

static int bcmeapi_ioctl_rx_rate_limit_config(struct ethswctl_data *e)
{
    BCM_ENET_DEBUG("Given channel: %d \n ", e->channel);
    if (e->type == TYPE_GET) {
        if (copy_to_user((void*)(&e->ret_val),
            (void*)&channel_rx_rate_limit_enable[e->channel], sizeof(int))) {
            return -EFAULT;
        }
        BCM_ENET_DEBUG("e->ret_val: 0x%02x \n ", e->ret_val);
    } else {
        BCM_ENET_DEBUG("Given rate_enable_cfg: %d \n ", e->val);
        channel_rx_rate_limit_enable[e->channel % ENET_RX_CHANNELS_MAX] = e->val;
    }

    return 0;
}
#endif

int bcmeapi_ioctl_ethsw_wrrparam(struct ethswctl_data *e)
{
    int i;
    int total_of_weights = 0;

    if (e->type == TYPE_GET) {
        if (copy_to_user((void*)(&e->max_pkts_per_iter), (void*)&max_pkts,
            sizeof(int))) {
            return -EFAULT;
        }
        if (copy_to_user((void*)(&e->weights), (void*)&weights,
            sizeof(int) * ENET_RX_CHANNELS_MAX)) {
            return -EFAULT;
        }
    } else {
        max_pkts = e->max_pkts_per_iter;
        for(i=0; i<ENET_RX_CHANNELS_MAX; i++) {
#if defined(CONFIG_BCM_GMAC)
            if (i < GMAC_LOG_CHAN)
#endif /* defined(CONFIG_BCM_GMAC) */
            weights[i] = e->weights[i];
        }

        total_of_weights = 0;
        for(i=0; i<cur_rxdma_channels; i++) {
#if defined(CONFIG_BCM_GMAC)
            if (i < GMAC_LOG_CHAN)
#endif /* defined(CONFIG_BCM_GMAC) */
            total_of_weights += weights[i];
        }

        for(i=0; i<cur_rxdma_channels; i++) {
#if defined(CONFIG_BCM_GMAC)
            if (i < GMAC_LOG_CHAN)
#endif /* defined(CONFIG_BCM_GMAC) */
            {
           weight_pkts[i] = (max_pkts/total_of_weights) * weights[i];
           pending_weight_pkts[i] = weight_pkts[i];
           BCM_ENET_DEBUG("weight[%d]_pkts: %d \n", i, weight_pkts[i]);
           pending_channel[i] = i;
            }
        }
        global_channel = channel_ptr = loop_index = 0;
        pending_ch_tbd = cur_rxdma_channels;
    }
    return 0;
}

int bcmeapi_ioctl_ethsw_rxscheduling(struct ethswctl_data *e)
{
    int i;

    if (e->type == TYPE_GET) {
        if (copy_to_user((void*)(&e->scheduling), (void*)&scheduling,
                    sizeof(int))) {
            return -EFAULT;
        }
    } else {
        if (e->scheduling == WRR_SCHEDULING) {
            scheduling = WRR_SCHEDULING;
            for(i=0; i < ENET_RX_CHANNELS_MAX; i++) {
                pending_weight_pkts[i] = weight_pkts[i];
                pending_channel[i] = i;
            }
            /* reset the other scheduling variables */
            global_channel = channel_ptr = loop_index = 0;
            pending_ch_tbd = cur_rxdma_channels;
        } else if (e->scheduling == SP_SCHEDULING) {
            global_channel = cur_rxdma_channels - 1;
            scheduling = SP_SCHEDULING;
        } else {
            return -EFAULT;
        }
    }
    return 0;
}

int bcmeapi_init_dev(struct net_device *dev)
{
#ifdef DYING_GASP_API
    /* Set up dying gasp buffer from packet transmit when we power down */
    dg_skbp = alloc_skb(64, GFP_ATOMIC);
    if (dg_skbp)
    {    
        memset(dg_skbp->data, 0, 64); 
        dg_skbp->len = 64;
        memcpy(dg_skbp->data, dg_ethOam_frame, sizeof(dg_ethOam_frame)); 
    }
#endif

#if defined(RXCHANNEL_PKT_RATE_LIMIT)
    timer_pid = kernel_thread((int(*)(void *))bcm63xx_timer, 0, CLONE_KERNEL);
#endif

#if defined(RXCHANNEL_PKT_RATE_LIMIT)
    if (timer_pid < 0)
        return -ENOMEM;
#endif

    return 0;
}


void bcmeapi_module_init2(void)
{

#if defined(CONFIG_BCM96828) && !defined(CONFIG_EPON_HGU)
    bcmFun_reg(BCM_FUN_ID_ENET_HANDLE, bcm_fun_enet_drv_handler);
#endif
    /* Register ARL Entry clear routine */
    bcmFun_reg(BCM_FUN_IN_ENET_CLEAR_ARL_ENTRY, remove_arl_entry_wrapper);
#if defined(CONFIG_BCM_GMAC)
    bcmFun_reg(BCM_FUN_ID_ENET_GMAC_ACTIVE, ChkGmacActive);
    bcmFun_reg(BCM_FUN_ID_ENET_GMAC_PORT, ChkGmacPort);
#endif

#if defined(_CONFIG_BCM_FAP)
    /* Add code for buffer quick free between enet and xtm - June 2010 */
    bcmPktDma_set_enet_recycle((RecycleFuncP)bcm63xx_enet_recycle);
#endif /* defined(_CONFIG_BCM_FAP) */

    if (extSwInfo.present == 1)
    {
        register_bridge_notifier(&br_notifier);
    }
}

void bcmeapi_enet_module_cleanup(void)
{
#if defined(_CONFIG_BCM_ARL)
    bcm_arl_process_hook_g = NULL;
#endif

#if defined(_CONFIG_BCM_INGQOS)
    iqos_enet_status_hook_g = NULL;
#endif

#if defined(_CONFIG_BCM_BPM)
    gbpm_enet_status_hook_g = NULL;
#if defined(_CONFIG_BCM_FAP)
    gbpm_enet_thresh_hook_g = NULL;
#endif
#endif

#if defined(CONFIG_BCM96368) && defined(_CONFIG_BCM_PKTCMF)
    pktCmfSarPortEnable  = (HOOKV)NULL;
    pktCmfSarPortDisable = (HOOKV)NULL;
#endif


#if defined(RXCHANNEL_PKT_RATE_LIMIT)
    if (timer_pid >= 0) {
      atomic_dec(&timer_lock);
      wait_for_completion(&timer_done);
    }
#endif
}

void bcmeapi_free_irq(BcmEnet_devctrl *pDevCtrl)
{
    free_irq(INTERRUPT_ID_EPHY, pDevCtrl);
#if defined(CONFIG_BCM963268) 
    free_irq(INTERRUPT_ID_GPHY, pDevCtrl);
#endif
#if defined(CONFIG_BCM96828)
    free_irq(INTERRUPT_ID_GPHY0, pDevCtrl);
    free_irq(INTERRUPT_ID_GPHY1, pDevCtrl);
#endif
#if defined(CONFIG_BCM_GMAC) 
    free_irq(INTERRUPT_ID_GMAC, pDevCtrl);
#endif
}

void bcmeapi_add_proc_files(struct net_device *dev, BcmEnet_devctrl *pDevCtrl)
{
    bcmenet_add_proc_files(dev);
    dev->base_addr  = (unsigned int)pDevCtrl->rxdma[0]->pktDmaRxInfo.rxDma;
}

void bcmeapi_get_chip_idrev(unsigned int *chipid, unsigned int *chiprev)
{
    *chipid  = (PERF->RevID & CHIP_ID_MASK) >> CHIP_ID_SHIFT;
    *chiprev = (PERF->RevID & REV_ID_MASK);
}

static void bcm63xx_uninit_txdma_structures(int channel, BcmEnet_devctrl *pDevCtrl)
{
    BcmPktDma_EthTxDma *txdma;
#if !defined(_CONFIG_BCM_FAP)
    int nr_tx_bds = bcmPktDma_Bds_p->host.eth_txbds[channel];
#endif

    txdma = pDevCtrl->txdma[channel];

    /* disable DMA */
    txdma->txEnabled = 0;
    txdma->txDma->cfg = 0;
    (void) bcmPktDma_EthTxDisable(txdma);

#if !defined(_CONFIG_BCM_FAP)
    /* if any, free the tx skbs */
    while (txdma->txFreeBds < nr_tx_bds) {
        txdma->txFreeBds++;
        nbuff_free((void *)txdma->txRecycle[txdma->txHeadIndex++].key);
        if (txdma->txHeadIndex == nr_tx_bds)
            txdma->txHeadIndex = 0;
    }
#endif

    /* free the transmit buffer descriptor ring */
    txdma = pDevCtrl->txdma[channel];
#if !defined(ENET_TX_BDS_IN_PSM)
    /* remove the tx bd ring */
    if (txdma->txBdsBase) {
        kfree((void *)txdma->txBdsBase);
    }
#endif

    /* free the txdma channel structures */
    for (channel = 0; channel < ENET_TX_CHANNELS_MAX; channel++) {
        if (pDevCtrl->txdma[channel]) {
            kfree((void *)(pDevCtrl->txdma[channel]));
        }
   }
}

void bcmeapi_free_queue(BcmEnet_devctrl *pDevCtrl)
{
    int i;

    /* Free the Tx DMA software structures */
    for (i = 0; i < ENET_TX_CHANNELS_MAX; i++) {
        bcm63xx_uninit_txdma_structures(i, pDevCtrl);
    }

    /* Free the Rx DMA software structures and packet buffers*/
    for (i = 0; i < ENET_RX_CHANNELS_MAX; i++) {
        bcm63xx_uninit_rxdma_structures(i, pDevCtrl);
#if defined(_CONFIG_BCM_BPM)
        gbpm_unresv_rx_buf( GBPM_PORT_ETH, i );
#endif
    }

    bcmenet_del_proc_files(pDevCtrl->dev);
}

static void bcm63xx_uninit_rxdma_structures(int channel, BcmEnet_devctrl *pDevCtrl)
{
    BcmEnet_RxDma *rxdma;

    rxdma = pDevCtrl->rxdma[channel];
    rxdma->pktDmaRxInfo.rxDma->cfg = 0;
    (void) bcmPktDma_EthRxDisable(&rxdma->pktDmaRxInfo);

#if !defined(_CONFIG_BCM_FAP) || defined(CONFIG_BCM_PKTDMA_RX_SPLITTING)
#if defined(CONFIG_BCM_PKTDMA_RX_SPLITTING)
    if (rxdma->pktDmaRxInfo.rxOwnership == HOST_OWNED)
#endif
    {
        /* free the IRQ */
        {
            int rxIrq = bcmPktDma_EthSelectRxIrq(channel);

            /* disable the interrupts from device */
            bcmPktDma_BcmHalInterruptDisable(channel, rxIrq);
            free_irq(rxIrq, (BcmEnet_devctrl *)BUILD_CONTEXT(pDevCtrl,channel));

#if defined(CONFIG_BCM_GMAC)
            if ( gmac_info_pg->enabled && (channel == GMAC_LOG_CHAN) )
            {
                rxIrq = INTERRUPT_ID_GMAC_DMA_0;

                /* disable the interrupts from device */
                bcmPktDma_BcmHalInterruptDisable(channel, rxIrq);
                free_irq(rxIrq,
                        (BcmEnet_devctrl *)BUILD_CONTEXT(pDevCtrl,channel));
            }
#endif
        }
    }
#endif

    /* release allocated receive buffer memory */
    uninit_buffers(rxdma);

    /* free the receive buffer descriptor ring */
#if !defined(ENET_RX_BDS_IN_PSM)
    if (rxdma->pktDmaRxInfo.rxBdsBase) {
        kfree((void *)rxdma->pktDmaRxInfo.rxBdsBase);
    }
#endif

    /* free the rxdma channel structures */
    if (pDevCtrl->rxdma[channel]) {
        kfree((void *)(pDevCtrl->rxdma[channel]));
    }
}

static int bcm63xx_init_txdma_structures(int channel, BcmEnet_devctrl *pDevCtrl)
{
    BcmPktDma_EthTxDma *txdma;

    pDevCtrl->txdma[channel] = (BcmPktDma_EthTxDma *) (kzalloc(
                           sizeof(BcmPktDma_EthTxDma), GFP_KERNEL));
    if (pDevCtrl->txdma[channel] == NULL) {
        printk("Unable to allocate memory for tx dma rings \n");
        return -ENXIO;
    }

    BCM_ENET_DEBUG("The txdma is 0x%p \n", pDevCtrl->txdma[channel]);

    txdma = pDevCtrl->txdma[channel];
    txdma->channel = channel;

#if defined(_CONFIG_BCM_FAP)
    txdma->fapIdx = getFapIdxFromEthTxIudma(channel);
    txdma->bdsAllocated = 0;
#endif

#if defined(CONFIG_BCM_PKTDMA_TX_SPLITTING)
    txdma->txOwnership = g_Eth_tx_iudma_ownership[channel];
#endif

#if defined(_CONFIG_BCM_BPM)
#if defined(_CONFIG_BCM_FAP)
   enet_bpm_init_tx_drop_thr( pDevCtrl, channel );
#endif
#endif

    /* init number of Tx BDs in each tx ring */
    txdma->numTxBds = bcmPktDma_EthGetTxBds( txdma, channel );

    BCM_ENET_DEBUG("Enet: txbds=%u \n", txdma->numTxBds);
    return 0;
}

int bcmeapi_init_queue(BcmEnet_devctrl *pDevCtrl)
{
    BcmEnet_RxDma *rxdma;
    volatile DmaRegs *dmaCtrl;
    int phy_chan;
    int i, rc = 0;
#if defined(CONFIG_BCM_PKTDMA_RX_SPLITTING)
    struct ethswctl_data e2;
#endif

    g_pEnetDevCtrl = pDevCtrl;   /* needs to be set before assign_rx_buffers is called */
    /* Get the pointer to switch DMA registers */
    pDevCtrl->dmaCtrl = (DmaRegs *)(SWITCH_DMA_BASE);
#if defined(CONFIG_BCM_GMAC)
    pDevCtrl->gmacDmaCtrl = (DmaRegs *)(GMAC_DMA_BASE);
    BCM_ENET_DEBUG("GMAC: gmacDmaCtrl is 0x%x\n",
        (unsigned int)pDevCtrl->gmacDmaCtrl);
#endif /* defined(CONFIG_BCM_GMAC) */

    /* Initialize the Tx DMA software structures */
    for (i = 0; i < ENET_TX_CHANNELS_MAX; i++) {
        rc = bcm63xx_init_txdma_structures(i, pDevCtrl);
        if (rc < 0)
            return rc;
    }

    /* Initialize the Rx DMA software structures */
    for (i = 0; i < ENET_RX_CHANNELS_MAX; i++) {
        rc = bcm63xx_init_rxdma_structures(i, pDevCtrl);

        if (rc < 0)
            return rc;
    }

    /* allocate and assign tx buffer descriptors */
    for (i=0; i < cur_txdma_channels; ++i)
    {
        rc = init_tx_channel(pDevCtrl, i);
        if (rc < 0)
        {
            return rc;
        }

        /* Enable the Tx channel */
        bcmPktDma_EthTxEnable(pDevCtrl->txdma[i]);
    }

    pending_ch_tbd = cur_rxdma_channels;
    for (i = 0; i < cur_rxdma_channels; i++) {
        channels_mask |= (1 << i);
    }
    pending_channels_mask = channels_mask;
    pDevCtrl->default_txq = 0;

    /* alloc space for the rx buffer descriptors */
    for (i = 0; i < cur_rxdma_channels; i++)
    {
        rxdma = pDevCtrl->rxdma[i];

        rc = init_rx_channel(pDevCtrl, i);
        if (rc < 0)
        {
            return rc;
        }

        bcmPktDma_BcmHalInterruptEnable(i, rxdma->rxIrq);
        bcmPktDma_EthRxEnable(&rxdma->pktDmaRxInfo);
    }

    for (i=0;i<cur_rxdma_channels;i++)
    {
        rxdma = pDevCtrl->rxdma[i];
        dmaCtrl = get_dmaCtrl( i );
        phy_chan = get_phy_chan( i );

#if defined(_CONFIG_BCM_FAP)
#if defined(CONFIG_BCM_PORTS_ON_INT_EXT_SW)
    bcmPktDma_EthInitExtSw(extSwInfo.connected_to_internalPort);
#endif
#endif
    }

#if defined(CONFIG_BCM_PKTDMA_RX_SPLITTING)
    /* Needed to allow iuDMA split override to work properly - Feb 2011 */
    /* The equivalent of "ethswctl -c cosq -q 1 -v 1" */
    /* This associates egress queue 1 on the switch to iuDMA1 */
    e2.type = TYPE_SET;
    e2.queue = 1;
    e2.channel = 1;
    bcmeapi_ioctl_ethsw_cosq_rxchannel_mapping(&e2);
#endif
    /* Workaround for 4ke */
    cache_flush_len(pDevCtrl, sizeof(BcmEnet_devctrl));


    return rc;
}


/*
 *  init_buffers: initialize driver's pools of receive buffers
 */
static int init_buffers(BcmEnet_devctrl *pDevCtrl, int channel)
{
#if !defined(_CONFIG_BCM_BPM)
    const unsigned long BlockSize = (64 * 1024);
    const unsigned long BufsPerBlock = BlockSize / BCM_PKTBUF_SIZE;
    unsigned long AllocAmt;
    unsigned char *pFkBuf;
    int j=0;
#endif
    int i;
    unsigned char *pSkbuff;
    unsigned long BufsToAlloc;
#if defined(RXCHANNEL_PKT_RATE_LIMIT)
    unsigned char *data;
#endif
    BcmEnet_RxDma *rxdma;
    uint32 context = 0;

    RECYCLE_CONTEXT(context)->channel = channel;

    TRACE(("bcm63xxenet: init_buffers\n"));

    /* allocate recieve buffer pool */
    rxdma = pDevCtrl->rxdma[channel];
    /* Local copy of these vars also initialized to zero in bcmPktDma channel init */
    rxdma->pktDmaRxInfo.rxAssignedBds = 0;
    rxdma->pktDmaRxInfo.rxHeadIndex = rxdma->pktDmaRxInfo.rxTailIndex = 0;
    BufsToAlloc = rxdma->pktDmaRxInfo.numRxBds;

#if defined(_CONFIG_BCM_BPM)
    if (enet_bpm_alloc_buf_ring(pDevCtrl, channel, BufsToAlloc) == GBPM_ERROR)
    {
        printk(KERN_NOTICE "Eth: Low memory.\n");

        /* release all allocated receive buffers */
        enet_bpm_free_buf_ring(rxdma, channel);
        return -ENOMEM;
    }
#else
    if( (rxdma->buf_pool = kzalloc(BufsToAlloc * sizeof(uint32_t) + 0x10,
        GFP_ATOMIC)) == NULL )
    {
        printk(KERN_NOTICE "Eth: Low memory.\n");
        return -ENOMEM;
    }

    while( BufsToAlloc ) {
        AllocAmt = (BufsPerBlock < BufsToAlloc) ? BufsPerBlock : BufsToAlloc;
        if( (data = kmalloc(AllocAmt * BCM_PKTBUF_SIZE + 0x10, GFP_ATOMIC)) == NULL )
        {
            /* release all allocated receive buffers */
            printk(KERN_NOTICE CARDNAME": Low memory.\n");
            for (i = 0; i < j; i++) {
                if (rxdma->buf_pool[i]) {
                    kfree(rxdma->buf_pool[i]);
                    rxdma->buf_pool[i] = NULL;
                }
            }
            return -ENOMEM;
        }

        rxdma->buf_pool[j++] = data;
        /* Align data buffers on 16-byte boundary - Apr 2010 */
        data = (unsigned char *) (((UINT32) data + 0x0f) & ~0x0f);
        for (i = 0, pFkBuf = data; i < AllocAmt; i++, pFkBuf += BCM_PKTBUF_SIZE) {
            /* Place a FkBuff_t object at the head of pFkBuf */
            fkb_preinit(pFkBuf, (RecycleFuncP)bcm63xx_enet_recycle, context);
            flush_assign_rx_buffer(pDevCtrl, channel, /* headroom not flushed */
                        PFKBUFF_TO_PDATA(pFkBuf,BCM_PKT_HEADROOM),
                        (uint8_t*)pFkBuf + BCM_PKTBUF_SIZE);
        }
        BufsToAlloc -= AllocAmt;
    }
#endif


 if (!rxdma->skbs_p)
 { /* CAUTION!!! DONOT reallocate SKB pool */
    /*
     * Dynamic allocation of skb logic assumes that all the skb-buffers
     * in 'freeSkbList' belong to the same contiguous address range. So if you do any change
     * to the allocation method below, make sure to rework the dynamic allocation of skb
     * logic. look for kmem_cache_create, kmem_cache_alloc and kmem_cache_free functions 
     * in this file 
    */
    if( (rxdma->skbs_p = kmalloc(
                    (rxdma->pktDmaRxInfo.numRxBds * BCM_SKB_ALIGNED_SIZE) + 0x10,
                    GFP_ATOMIC)) == NULL )
        return -ENOMEM;

    memset(rxdma->skbs_p, 0,
                (rxdma->pktDmaRxInfo.numRxBds * BCM_SKB_ALIGNED_SIZE) + 0x10);

    rxdma->freeSkbList = NULL;

    /* Chain socket skbs */
    for(i = 0, pSkbuff = (unsigned char *)
        (((unsigned long) rxdma->skbs_p + 0x0f) & ~0x0f);
            i < rxdma->pktDmaRxInfo.numRxBds; i++, pSkbuff += BCM_SKB_ALIGNED_SIZE)
    {
        ((struct sk_buff *) pSkbuff)->next_free = rxdma->freeSkbList;
        rxdma->freeSkbList = (struct sk_buff *) pSkbuff;
    }
 }
 rxdma->end_skbs_p = rxdma->skbs_p + (rxdma->pktDmaRxInfo.numRxBds * BCM_SKB_ALIGNED_SIZE) + 0x10;


#if defined(RXCHANNEL_PKT_RATE_LIMIT)
    /* Initialize the StdBy BD Ring */
    {
    if( (data = kmalloc(BCM_PKTBUF_SIZE, GFP_ATOMIC)) == NULL ) {
        /* release all allocated receive buffers */
        printk(KERN_NOTICE CARDNAME": Low memory.\n");
        return -ENOMEM;
    }
    rxdma->StdByBuf = data;
    rxdma->rxBdsStdBy[0].address =
             (uint32)VIRT_TO_PHY(data + BCM_PKT_HEADROOM);
    rxdma->rxBdsStdBy[0].length  = BCM_MAX_PKT_LEN;
    rxdma->rxBdsStdBy[0].status = DMA_OWN | DMA_WRAP;
    }
#endif /* defined(RXCHANNEL_PKT_RATE_LIMIT) */

    return 0;
}

/*
 *  uninit_buffers: un-initialize driver's pools of receive buffers
 */
void uninit_buffers(BcmEnet_RxDma *rxdma)
{
    int i;

#if defined(_CONFIG_BCM_BPM)
    int channel;
    uint32 context=0;
    uint32 rxAddr=0;

    channel  = RECYCLE_CONTEXT(context)->channel;

    /* release all allocated receive buffers */
    for (i = 0; i < rxdma->pktDmaRxInfo.numRxBds; i++)
    {
#if !defined(_CONFIG_BCM_FAP)
        if (bcmPktDma_EthRecvBufGet(&rxdma->pktDmaRxInfo, &rxAddr) == TRUE)
#endif
        {
            if ((uint8 *) rxAddr != NULL)
            {
                gbpm_free_buf((uint32_t *) PDATA_TO_PFKBUFF(rxAddr,BCM_PKT_HEADROOM));
            }
        }
    }

#if defined(_CONFIG_BCM_BPM)
      gbpm_unresv_rx_buf( GBPM_PORT_ETH, channel );
#endif
#else
    /* release all allocated receive buffers */
    for (i = 0; i < rxdma->pktDmaRxInfo.numRxBds; i++) {
        if (rxdma->buf_pool[i]) {
            kfree(rxdma->buf_pool[i]);
            rxdma->buf_pool[i] = NULL;
        }
    }
    kfree(rxdma->buf_pool);
#endif

#if 0   /* CAUTION!!! DONOT free SKB pool */
    kfree(rxdma->skbs_p);
#endif

#if defined(RXCHANNEL_PKT_RATE_LIMIT)
    /* Free the buffer in StdBy Ring */
    kfree(rxdma->StdByBuf);
    rxdma->StdByBuf = NULL;
    /* BDs freed elsewhere - Apr 2010 */
#endif
}

#if 0   /* For debug */
static int bcm63xx_dump_rxdma(int channel, BcmEnet_RxDma *rxdma )
{
    BcmPktDma_EthRxDma *pktDmaRxInfo_p = &rxdma->pktDmaRxInfo;

    printk( "bcm63xx_dump_rxdma channel=%d\n", (int)channel);
    printk( "=======================================\n" );
    printk( "rxdma address = 0x%p\n", rxdma);
    printk( "rxdma->rxIrq = %d\n", rxdma->rxIrq );
    printk( "pktDmaRxInfo_p = 0x%p\n", &rxdma->pktDmaRxInfo);
    printk( "pktDmaRxInfo_p->rxEnabled<0x%p>= %d\n",
        &pktDmaRxInfo_p->rxEnabled, pktDmaRxInfo_p->rxEnabled);
    printk( "pktDmaRxInfo_p->channel = %d\n", pktDmaRxInfo_p->channel );
    printk( "pktDmaRxInfo_p->rxDma = 0x%p\n", pktDmaRxInfo_p->rxDma );
    printk( "pktDmaRxInfo_p->rxBdsBase = 0x%p\n", pktDmaRxInfo_p->rxBdsBase );
    printk( "pktDmaRxInfo_p->rxBds= 0x%p\n", pktDmaRxInfo_p->rxBds);
    printk( "pktDmaRxInfo_p->numRxBds = %d\n", pktDmaRxInfo_p->numRxBds );
    printk( "pktDmaRxInfo_p->rxAssignedBds = %d\n",
        pktDmaRxInfo_p->rxAssignedBds );
    printk( "pktDmaRxInfo_p->rxHeadIndex = %d\n", pktDmaRxInfo_p->rxHeadIndex );
    printk( "pktDmaRxInfo_p->rxTailIndex = %d\n", pktDmaRxInfo_p->rxTailIndex );

#if defined(_CONFIG_BCM_FAP)
    printk( "pktDmaRxInfo_p->fapIdx = %d\n", (int) pktDmaRxInfo_p->fapIdx );
    printk( "rxdma->bdsAllocated = %d\n", rxdma->bdsAllocated );
#endif

#if defined(CONFIG_BCM_PKTDMA_RX_SPLITTING)
    printk( "\npktDmaRxInfo_p->rxOwnership = %d\n", pktDmaRxInfo_p->rxOwnership );
#endif
    return 0;
}

static int bcm63xx_dump_txdma(int channel, BcmPktDma_EthTxDma *txdma )
{
    printk( "bcm63xx_dump_txdma channel=%d\n", (int)channel);
    printk( "=======================================\n" );
    printk( "txdma address = 0x%p\n", txdma);
    printk( "txdma->txEnabled<0x%p>= %d\n", &txdma->txEnabled,
        txdma->txEnabled);
    printk( "txdma->channel = %d\n", txdma->channel );
    printk( "txdma->txDma = 0x%p\n", txdma->txDma );
    printk( "txdma->txBdsBase = 0x%p\n", txdma->txBdsBase );
    printk( "txdma->txBds= 0x%p\n", txdma->txBds);
    printk( "txdma->numTxBds = %d\n", txdma->numTxBds );
    printk( "txdma->txFreeBds = %d\n", txdma->txFreeBds );
    printk( "txdma->txHeadIndex = %d\n", txdma->txHeadIndex );
    printk( "txdma->txTailIndex = %d\n", txdma->txTailIndex );
    printk( "txdma->txRecycle = 0x%p\n", txdma->txRecycle );

#if defined(_CONFIG_BCM_FAP)
    printk( "txdma->fapIdx = %d\n", (int) txdma->fapIdx );
    printk( "txdma->bdsAllocated = %d\n", txdma->bdsAllocated );
#endif

#if defined(CONFIG_BCM_PKTDMA_RX_SPLITTING)
    printk( "\ntxdma->txOwnership = %d\n", txdma->txOwnership );
#endif
    return 0;
}
#endif


/* Note: this may be called from an atomic context */
static int bcm63xx_alloc_rxdma_bds(int channel, BcmEnet_devctrl *pDevCtrl)
{
   BcmEnet_RxDma *rxdma;
   rxdma = pDevCtrl->rxdma[channel];

#if defined(RXCHANNEL_PKT_RATE_LIMIT)
   /* Allocate 1 extra BD for rxBdsStdBy */
   rxdma->pktDmaRxInfo.rxBdsBase = bcmPktDma_EthAllocRxBds(channel, rxdma->pktDmaRxInfo.numRxBds + 1);
#else
   rxdma->pktDmaRxInfo.rxBdsBase = bcmPktDma_EthAllocRxBds(channel, rxdma->pktDmaRxInfo.numRxBds);
#endif
   if ( rxdma->pktDmaRxInfo.rxBdsBase == NULL )
   {
      printk("Unable to allocate memory for Rx Descriptors \n");
      return -ENOMEM;
   }
#if defined(ENET_RX_BDS_IN_PSM)
   rxdma->pktDmaRxInfo.rxBds = rxdma->pktDmaRxInfo.rxBdsBase;
#else
   /* Align BDs to a 16-byte boundary - Apr 2010 */
   rxdma->pktDmaRxInfo.rxBds = (volatile DmaDesc *)(((int)rxdma->pktDmaRxInfo.rxBdsBase + 0xF) & ~0xF);
   rxdma->pktDmaRxInfo.rxBds = (volatile DmaDesc *)CACHE_TO_NONCACHE(rxdma->pktDmaRxInfo.rxBds);
#endif

   /* Local copy of these vars also initialized to zero in bcmPktDma channel init */
   rxdma->pktDmaRxInfo.rxAssignedBds = 0;
   rxdma->pktDmaRxInfo.rxHeadIndex = rxdma->pktDmaRxInfo.rxTailIndex = 0;

#if defined(RXCHANNEL_PKT_RATE_LIMIT)
   /* stand by bd ring with only one BD */
   rxdma->rxBdsStdBy = &rxdma->pktDmaRxInfo.rxBds[rxdma->pktDmaRxInfo.numRxBds];
#endif

#if defined(_CONFIG_BCM_FAP)
#if defined(CONFIG_BCM_PKTDMA_RX_SPLITTING)
    if (rxdma->pktDmaRxInfo.rxOwnership != HOST_OWNED)
#endif
    {
        rxdma->bdsAllocated = 1;
    }
#endif

   return 0;
}

/*
 *	static int bcm63xx_alloc_txdma_bds(int channel, BcmEnet_devctrl *pDevCtrl)
 *  Original Logic Matrix:
 *	(818 && FAP && SPL) && hst: AL
 *	(818 && FAP && SPL) && !hst && PSM: NA
 *	(818 && FAP && SPL) && !hst && !PSM: AL
 *	(818 && FAP && !SPL) && PSM: NA
 *	(818 && FAP && !SPL) && !PSM: AL
 *	818 && !FAP: AL
 *
 *	(!818  && SPL) && hst: AL
 *	(!818  && SPL) && !hst && PSM: NA
 *	(!818  && SPL) && !hst && !PSM: AL
 *	!818  && !SPL && PSM: NA
 *	!818  && !SPL && !PSM: AL
*/
#if defined(DBL_DESC)
    #define DmaDescType DmaDesc16
    #if defined(_CONFIG_BCM_FAP) && defined(CONFIG_BCM_PKTDMA_TX_SPLITTING)
        #define CHECK_HOST_OWNERSHIP
    #endif
#else /* (defined(DBL_DESC)) */
    #define DmaDescType DmaDesc
    #if defined(CONFIG_BCM_PKTDMA_TX_SPLITTING)
        #define CHECK_HOST_OWNERSHIP
    #endif
#endif
static int bcm63xx_alloc_txdma_bds(int channel, BcmEnet_devctrl *pDevCtrl)
{
   BcmPktDma_EthTxDma *txdma;
   int nr_tx_bds;
   bool do_align = TRUE;

   txdma = pDevCtrl->txdma[channel];
   nr_tx_bds = txdma->numTxBds;

   /* BDs allocated in bcmPktDma lib in PSM or in DDR */
   txdma->txBdsBase = bcmPktDma_EthAllocTxBds(channel, nr_tx_bds);
   if ( txdma->txBdsBase == NULL )
   {
      printk("Unable to allocate memory for Tx Descriptors \n");
      return -ENOMEM;
   }

   BCM_ENET_DEBUG("bcm63xx_alloc_txdma_bds txdma->txBdsBase 0x%x",
        (unsigned int)txdma->txBdsBase);

   txdma->txBds = txdma->txBdsBase;
   txdma->txRecycle = (BcmPktDma_txRecycle_t *)((uint32)txdma->txBds + (nr_tx_bds * sizeof(DmaDescType)));

    #if defined(CHECK_HOST_OWNERSHIP)
    if(txdma->txOwnership != HOST_OWNED)
    #endif
    {
    #if defined(ENET_TX_BDS_IN_PSM)
        do_align = FALSE;
    #endif
    }

        /* Align BDs to a 16/32 byte boundary - Apr 2010 */
    if(do_align) {
        txdma->txBds = (volatile void *)(((int)txdma->txBds + 0xF) & ~0xF);
        txdma->txBds = (volatile void *)CACHE_TO_NONCACHE(txdma->txBds);
        txdma->txRecycle = (BcmPktDma_txRecycle_t *)((uint32)txdma->txBds + (nr_tx_bds * sizeof(DmaDescType)));
        txdma->txRecycle = (BcmPktDma_txRecycle_t *)NONCACHE_TO_CACHE(txdma->txRecycle);
    }

   txdma->txFreeBds = nr_tx_bds;
   txdma->txHeadIndex = txdma->txTailIndex = 0;
   nr_tx_bds = txdma->numTxBds;

   /* BDs allocated in bcmPktDma lib in PSM or in DDR */
   memset((char *) txdma->txBds, 0, sizeof(DmaDescType) * nr_tx_bds );

#if defined(_CONFIG_BCM_FAP)
#if defined(CONFIG_BCM_PKTDMA_TX_SPLITTING)
    if (txdma->txOwnership != HOST_OWNED)
#endif
        txdma->bdsAllocated = 1;
#endif

   return 0;
}

#if !defined(_CONFIG_BCM_FAP) || defined(CONFIG_BCM_PKTDMA_RX_SPLITTING)
static FN_HANDLER_RT bcmeapi_enet_isr(int irq, void * pContext);
#endif /* !defined(_CONFIG_BCM_FAP) || defined(CONFIG_BCM_PKTDMA_TX_SPLITTING) */

static int bcm63xx_init_rxdma_structures(int channel, BcmEnet_devctrl *pDevCtrl)
{
    BcmEnet_RxDma *rxdma;

    /* init rx dma channel structures */
    pDevCtrl->rxdma[channel] = (BcmEnet_RxDma *) (kzalloc(
                           sizeof(BcmEnet_RxDma), GFP_KERNEL));
    if (pDevCtrl->rxdma[channel] == NULL) {
        printk("Unable to allocate memory for rx dma rings \n");
        return -ENXIO;
    }
    BCM_ENET_DEBUG("The rxdma is 0x%p \n", pDevCtrl->rxdma[channel]);

    rxdma = pDevCtrl->rxdma[channel];
    rxdma->pktDmaRxInfo.channel = channel;
#if defined(_CONFIG_BCM_FAP)
    rxdma->pktDmaRxInfo.fapIdx = getFapIdxFromEthRxIudma(channel);
    rxdma->bdsAllocated = 0;
#endif

#if defined(CONFIG_BCM_PKTDMA_RX_SPLITTING)
    /* FAP_TBD: Is this needed for 268 BPM or IQ? */
    rxdma->pktDmaRxInfo.rxOwnership = g_Eth_rx_iudma_ownership[channel];
#endif

    /* init number of Rx BDs in each rx ring */
    rxdma->pktDmaRxInfo.numRxBds =
                    bcmPktDma_EthGetRxBds( &rxdma->pktDmaRxInfo, channel );

#if defined(_CONFIG_BCM_INGQOS)
    enet_rx_init_iq_thresh(pDevCtrl, channel);
#endif

#if !defined(_CONFIG_BCM_FAP) || defined(CONFIG_BCM_PKTDMA_RX_SPLITTING)
#if defined(CONFIG_BCM_PKTDMA_RX_SPLITTING)
    if (rxdma->pktDmaRxInfo.rxOwnership == HOST_OWNED)
#endif
    {
        /* request IRQs only once at module init */
        {
            int rxIrq = bcmPktDma_EthSelectRxIrq(channel);

            /* disable the interrupts from device */
            bcmPktDma_BcmHalInterruptDisable(channel, rxIrq);

            /* a Host owned channel */
            BcmHalMapInterrupt(bcmeapi_enet_isr,
                BUILD_CONTEXT(pDevCtrl,channel), rxIrq);

#if defined(CONFIG_BCM_GMAC)
            if ( gmac_info_pg->enabled && (channel == GMAC_LOG_CHAN) )
            {
                rxIrq = INTERRUPT_ID_GMAC_DMA_0;

                /* disable the interrupts from device */
                bcmPktDma_BcmHalInterruptDisable(channel, rxIrq);

                BcmHalMapInterrupt(bcmeapi_enet_isr,
                    BUILD_CONTEXT(pDevCtrl,channel), rxIrq);
            }
#endif
        }
    }
#endif

    return 0;
}

#if !defined(_CONFIG_BCM_FAP) || defined(CONFIG_BCM_PKTDMA_RX_SPLITTING)
/*
 * bcmeapi_enet_isr: Acknowledge interrupt and check if any packets have
 *                  arrived on Rx DMA channel 0..3
 */
static FN_HANDLER_RT bcmeapi_enet_isr(int irq, void * pContext)
{
    /* this code should not run in DQM operation !!! */

    int channel;
    BcmEnet_devctrl *pDevCtrl;

    channel = CONTEXT_TO_CHANNEL((uint32)pContext);
    pDevCtrl = CONTEXT_TO_PDEVCTRL((uint32)pContext);

    /* Only rx channels owned by the Host come through this ISR */
    bcmPktDma_EthClrRxIrq_Iudma(&pDevCtrl->rxdma[channel]->pktDmaRxInfo);

    BCMENET_WAKEUP_RXWORKER(pDevCtrl);

    return BCM_IRQ_HANDLED;
}
#endif /* !defined(_CONFIG_BCM_FAP) || defined(CONFIG_BCM_PKTDMA_TX_SPLITTING) */

#if defined(CONFIG_BCM_GMAC)
/* get GMAC's logical port id */
int enet_gmac_log_port( void )
{
    return PHYSICAL_PORT_TO_LOGICAL_PORT(GMAC_PORT_ID, 0);
}

/* Is the GMAC enabled and the port matches with GMAC's logical port? */
inline int IsGmacPort( int log_port )
{
    if ( gmac_info_pg->enabled && (log_port == enet_gmac_log_port() ) )
        return 1;
    else
        return 0;
}


/* Is the GMAC enabled and the port matches with GMAC's logical port? */
inline int ChkGmacPort( void * ctxt )
{
    return IsGmacPort( *(int *)ctxt );
}

/* Is the GMAC port active? */
inline int ChkGmacActive( void *ctxt )
{
    return IsGmacInfoActive;
}
#endif /* CONFIG_BCM_GMAC */

int bcmeapi_ioctl_kernel_poll(struct ethswctl_data *e)
{
    static int mdk_init_done = 0;

    /* MDK will calls this function for the first time after it completes initialization */
    if (!mdk_init_done) {
        mdk_init_done = 1;
        ethsw_eee_init();
    }

#if defined(CONFIG_BCM_ETH_PWRSAVE)
    ethsw_ephy_auto_power_down_wakeup();
#endif

#if defined(CONFIG_BCM_ETH_PWRSAVE)
    ethsw_ephy_auto_power_down_sleep();
#endif

    /* Check for delayed request to enable EEE */
    ethsw_eee_process_delayed_enable_requests();

#if (CONFIG_BCM_EXT_SWITCH_TYPE == 53115)
    extsw_apd_set_compatibility_mode();
#endif
    return 0;
}

static int bcm63xx_xmit_reclaim(void)
{
    int i;
    pNBuff_t pNBuff;
    BcmEnet_devctrl *pDevCtrl = (BcmEnet_devctrl *)netdev_priv(vnet_dev[0]);
    BcmPktDma_txRecycle_t txRecycle;
    BcmPktDma_txRecycle_t *txRecycle_p;

    /* Obtain exclusive access to transmitter.  This is necessary because
    * we might have more than one stack transmitting at once. */
    ENET_TX_LOCK();
    for (i = 0; i < cur_txdma_channels; i++)
    {
        while ((txRecycle_p = bcmPktDma_EthFreeXmitBufGet(pDevCtrl->txdma[i], &txRecycle)) != NULL)
        {
           pNBuff = (pNBuff_t)txRecycle_p->key;

           BCM_ENET_RX_DEBUG("bcmPktDma_EthFreeXmitBufGet TRUE! (reclaim) key 0x%x\n", (int)pNBuff);
           if (pNBuff != PNBUFF_NULL) {
               ENET_TX_UNLOCK();
               nbuff_free(pNBuff);
               ENET_TX_LOCK();
           }
        }   /* end while(...) */
    }   /* end for(...) */
    ENET_TX_UNLOCK();

    return 0;
}

/* Forward declarations */
void __ethsw_get_txrx_imp_port_pkts(void);

void bcmeapi_enet_poll_timer(void)
{
    /* Collect CPU/IMP Port RX/TX packets every poll period */
    __ethsw_get_txrx_imp_port_pkts();

    bcm63xx_xmit_reclaim();

#if !defined(SUPPORT_SWMDK)
#if defined(_CONFIG_BCM_FAP)
#if defined (_CONFIG_BCM_XTMCFG)
    /* Add code for buffer quick free between enet and xtm - June 2010 */
    if(xtm_fkb_recycle_hook == NULL)
        xtm_fkb_recycle_hook = bcmPktDma_get_xtm_fkb_recycle();
#endif /* _CONFIG_BCM_XTMCFG */
#if defined(CONFIG_BCM963268)
    if(xtm_skb_recycle_hook == NULL)
        xtm_skb_recycle_hook = bcmPktDma_get_xtm_skb_recycle();
#endif
#endif
#endif
}

void bcmeapi_aelink_handler(int linkstatus)
{
}

void bcmeapi_set_mac_speed(int port, int speed)
{
}

#if defined(_CONFIG_BCM_FAP)
void bcmeapi_EthSetPhyRate(int port, int enable, uint32_t bps, int isWanPort)
{
    if(enable)
    {
        int unit = LOGICAL_PORT_TO_UNIT_NUMBER(port);
        unsigned long maxRate = pVnetDev0_g->EnetInfo[unit].sw.portMaxRate[LOGICAL_PORT_TO_PHYSICAL_PORT(port)];
        unsigned long phyconn = pVnetDev0_g->EnetInfo[unit].sw.phyconn[LOGICAL_PORT_TO_PHYSICAL_PORT(port)];
        uint32_t kbps;

        if ( (0 == maxRate) || (maxRate > bps) )
        {
            maxRate = bps;
        }
        kbps = ((maxRate / 1000) * 99) / 100;

        switch(maxRate)
        {
            case SPEED_1000MBIT:
                bcmPktDma_EthSetPhyRate(port, 0, kbps, isWanPort);
                break;
            default:
                bcmPktDma_EthSetPhyRate(port, 1, kbps, isWanPort);
                break;
        }
        if ( phyconn == PHY_CONN_TYPE_PLC )
        {
            bcmPktDma_EthSetPauseEn(port, 1);
        }
    }
    else
    {
        bcmPktDma_EthSetPhyRate(port, 0, 0, isWanPort);
    }
}
#endif

#if defined(_CONFIG_BCM_PKTCMF)
void bcmEnet_pktCmfEthResetStats( uint32_t sw_port )
{
    bcmFun_t *pktCmfEthResetStatsHook;

    pktCmfEthResetStatsHook = bcmFun_get(BCM_FUN_ID_CMF_ETH_RESET_STATS);

    if (pktCmfEthResetStatsHook)
    {
        pktCmfEthResetStatsHook( (void *) &sw_port);
    }
}

void bcmEnet_pktCmfEthGetStats( uint32_t vport,
        uint32_t *rxDropped_p, uint32_t *txDropped_p )
{
    bcmFun_t *pktCmfEthGetStatsHook;
    PktCmfStatsParam_t statsParam;

    *rxDropped_p = 0;
    *txDropped_p = 0;

    pktCmfEthGetStatsHook = bcmFun_get(BCM_FUN_ID_CMF_ETH_GET_STATS);

    if (pktCmfEthGetStatsHook)
    {
        statsParam.vport = vport;
        statsParam.rxDropped_p = rxDropped_p;
        statsParam.txDropped_p = txDropped_p;

        pktCmfEthGetStatsHook( (void *) &statsParam );
    }
}
#endif

void bcmeapi_reset_mib_cnt(uint32_t sw_port)
{
#if defined(_CONFIG_BCM_PKTCMF)
	bcmEnet_pktCmfEthResetStats(sw_port);
#else
	bcmPktDma_EthResetStats(sw_port);
#endif
}

int bcmeapi_link_might_changed(void)
{
    int link_changed = 0;
    
    if(ephy_int_lock != ephy_int_cnt)
    {   
        link_changed = ETHSW_LINK_MIGHT_CHANGED;
        ephy_int_lock = ephy_int_cnt;
    }

    return link_changed;
}

int  bcmeapi_open_dev(BcmEnet_devctrl *pDevCtrl, struct net_device *dev)
{
    int channel = 0;
    BcmEnet_RxDma *rxdma;
    BcmPktDma_EthTxDma *txdma;

    ENET_RX_LOCK();
    pDevCtrl->dmaCtrl->controller_cfg |= DMA_MASTER_EN;

    /*  Enable the Rx DMA channels and their interrupts  */
    for (channel = 0; channel < cur_rxdma_channels; channel++) {
        rxdma = pDevCtrl->rxdma[channel];
#if defined(RXCHANNEL_PKT_RATE_LIMIT)
        rxchannel_isr_enable[channel] = 1;
#endif
        bcmPktDma_EthRxEnable(&rxdma->pktDmaRxInfo);
        bcmPktDma_BcmHalInterruptEnable(channel, rxdma->rxIrq);
    }
    ENET_RX_UNLOCK();

    ENET_TX_LOCK();
    /*  Enable the Tx DMA channels  */
    for (channel = 0; channel < cur_txdma_channels; channel++) {
        txdma = pDevCtrl->txdma[channel];
        bcmPktDma_EthTxEnable(txdma);
        txdma->txEnabled = 1;
    }
    ENET_TX_UNLOCK();

#if defined(_CONFIG_BCM_FAP)
#if defined(CONFIG_BCM963268) || defined(CONFIG_BCM96828)
    {
        struct ethswctl_data e2;

        /* Needed to allow iuDMA split override to work properly - Feb 2011 */
        /* Set the Switch Control and QoS registers later than init for the 63268/6828 */

        /* The equivalent of "ethswctl -c cosqsched -v BCM_COSQ_COMBO -q 2 -x 1 -y 1 -z 1 -w 1" */
        /* This assigns equal weight to each of the 4 egress queues */
        /* This means the rx splitting feature cannot co-exist with h/w QOS */
        e2.type = TYPE_SET;
        e2.queue = 2;   /* mode */
        e2.scheduling = BCM_COSQ_COMBO;
        e2.weights[0] = e2.weights[1] = e2.weights[2] = e2.weights[3] = 1;
        bcmeapi_ioctl_ethsw_cosq_sched(&e2);

        /* The equivalent of "ethswctl -c cosq -q 1 -v 1" */
        /* This associates egress queue 1 on the switch to iuDMA1 */
        e2.type = TYPE_SET;
        e2.queue = 1;
        e2.channel = 1;
        bcmeapi_ioctl_ethsw_cosq_rxchannel_mapping(&e2);

    }
#endif
#endif
    return 0; /* success */
}

#if defined(RXCHANNEL_PKT_RATE_LIMIT)
/*
 * bcm63xx_timer: 100ms timer for updating rx rate control credits
 */
static void bcm63xx_timer(unsigned long arg)
{
    struct net_device *dev = vnet_dev[0];
    BcmEnet_devctrl *priv = (BcmEnet_devctrl *)netdev_priv(dev);
    BcmEnet_RxDma *rxdma;
    unsigned int elapsed_msecs;
    int channel;
    struct sched_param param;

    /* */
    daemonize("bcmsw_timer");
    param.sched_priority = BCM_RTPRIO_DATA;
    sched_setscheduler(current, SCHED_RR, &param);
    set_user_nice(current, 0);

    /* */
    while (atomic_read(&timer_lock) > 0)
    {
        for (channel = 0; channel < cur_rxdma_channels; channel++) {
            ENET_RX_LOCK();
            if (rxchannel_rate_limit_enable[channel]) {
                elapsed_msecs = jiffies_to_msecs(jiffies -
                        last_pkt_jiffies[channel]);
                if (elapsed_msecs >= 99) {
                    rxdma = priv->rxdma[channel];
                    BCM_ENET_DEBUG("pkts_from_last_jiffies = %d \n",
                            rx_pkts_from_last_jiffies[channel]);
                    rx_pkts_from_last_jiffies[channel] = 0;
                    last_pkt_jiffies[channel] = jiffies;
                    if (rxchannel_isr_enable[channel] == 0) {
                        BCM_ENET_DEBUG("Enabling DMA Channel & Interrupt \n");
                        switch_rx_ring(priv, channel, 0);
                        bcmPktDma_BcmHalInterruptEnable(channel, rxdma->rxIrq);
                        rxchannel_isr_enable[channel] = 1;
                    }
                }
            }
            ENET_RX_UNLOCK();
        }

        /*  Sleep for HZ/10 jiffies (100ms)  */
        set_current_state(TASK_INTERRUPTIBLE);
        schedule_timeout(HZ/10);
    }

    complete_and_exit(&timer_done, 0);
    printk("bcm63xx_timer: thread exits!\n");
}
#endif /* defined(RXCHANNEL_PKT_RATE_LIMIT) */

void bcmeapi_EthGetStats(int log_port, uint32 *rxDropped, uint32 *txDropped)
{
#if defined(_CONFIG_BCM_PKTCMF)
    bcmEnet_pktCmfEthGetStats((uint32_t)log_port, (uint32_t*)rxDropped, (uint32_t*)txDropped);
#else
    bcmPktDma_EthGetStats(log_port, rxDropped, txDropped);
#endif
}

void bcmeapi_del_dev_intr(BcmEnet_devctrl *pDevCtrl)
{
    int channel = 0;
    BcmEnet_RxDma *rxdma;
    BcmPktDma_EthTxDma *txdma;

    ENET_RX_LOCK();
    for (channel = 0; channel < cur_rxdma_channels; channel++) {
        rxdma = pDevCtrl->rxdma[channel];
        bcmPktDma_BcmHalInterruptDisable(channel, rxdma->rxIrq);
        bcmPktDma_EthRxDisable(&rxdma->pktDmaRxInfo);
    }
    ENET_RX_UNLOCK();

    ENET_TX_LOCK();
    for (channel = 0; channel < cur_txdma_channels; channel++) {

        txdma = pDevCtrl->txdma[channel];
        txdma->txEnabled = 0;
        bcmPktDma_EthTxDisable(txdma);
    }
    ENET_TX_UNLOCK();
}

#if defined(_CONFIG_BCM_BPM)
/*
 * Assumptions:-
 * 1. Align data buffers on 16-byte boundary - Apr 2010
 */

/* Dumps the BPM status for Eth channels */
static void enet_bpm_status(void)
{
    int chnl;
    BcmEnet_devctrl *pDevCtrl = (BcmEnet_devctrl *)netdev_priv(vnet_dev[0]);

    for (chnl = 0; chnl < cur_rxdma_channels; chnl++)
    {
        BcmPktDma_EthRxDma *rxdma;
        rxdma = &pDevCtrl->rxdma[chnl]->pktDmaRxInfo;

#if defined(_CONFIG_BCM_FAP)
        if (g_Eth_rx_iudma_ownership[chnl] != HOST_OWNED)
            continue;
#endif

        printk("[HOST] ENET %4d %10u %10u %5u %4u %4u\n",
                chnl, (uint32_t) rxdma->alloc,
                (uint32_t) rxdma->free,
                (uint32_t) rxdma->rxAssignedBds,
                (uint32_t) rxdma->allocTrig,
                (uint32_t) rxdma->bulkAlloc );
    }
}


/* Allocates the buffer ring for an Eth RX channel */
static int enet_bpm_alloc_buf_ring(BcmEnet_devctrl *pDevCtrl,
        int channel, uint32 num)
{
    unsigned char *pFkBuf, *pData;
    uint32 context = 0;
    uint32 buf_ix;

    RECYCLE_CONTEXT(context)->channel = channel;

    for (buf_ix=0; buf_ix < num; buf_ix++)
    {
        if ( (pFkBuf = (uint8_t *) gbpm_alloc_buf()) == NULL )
            return GBPM_ERROR;

        pData = PFKBUFF_TO_PDATA(pFkBuf,BCM_PKT_HEADROOM);

        /* Place a FkBuff_t object at the head of pFkBuf */
        fkb_preinit(pFkBuf, (RecycleFuncP)bcm63xx_enet_recycle, context);

        cache_flush_region(pData, (uint8_t*)pFkBuf + BCM_PKTBUF_SIZE);
        bcmPktDma_EthFreeRecvBuf(&pDevCtrl->rxdma[channel]->pktDmaRxInfo, pData);
    }

    return GBPM_SUCCESS;
}


/* Frees the buffer ring for an Eth RX channel */
static void enet_bpm_free_buf_ring(BcmEnet_RxDma *rxdma, int channel)
{
    uninit_buffers(rxdma);
}


static void enet_rx_set_bpm_alloc_trig( BcmEnet_devctrl *pDevCtrl, int chnl )
{
    BcmPktDma_EthRxDma *rxdma = &pDevCtrl->rxdma[chnl]->pktDmaRxInfo;
    uint32  allocTrig = rxdma->numRxBds * BPM_ENET_ALLOC_TRIG_PCT/100;

        BCM_ENET_DEBUG( "Enet: Chan=%d BPM Rx allocTrig=%d bulkAlloc=%d\n",
        chnl, (int) allocTrig, BPM_ENET_BULK_ALLOC_COUNT );

    bcmPktDma_EthSetRxChanBpmThresh(rxdma,
        allocTrig, BPM_ENET_BULK_ALLOC_COUNT);
}


#if defined(_CONFIG_BCM_FAP)
/* Dumps the TxDMA drop thresh for eth channels */
static void enet_bpm_dma_dump_tx_drop_thr(void)
{
    int chnl;
    BcmEnet_devctrl *pDevCtrl = (BcmEnet_devctrl *)netdev_priv(vnet_dev[0]);

    for (chnl = 0; chnl < cur_txdma_channels; chnl++)
    {
        BcmPktDma_EthTxDma *txdma = pDevCtrl->txdma[chnl];
        int q;

        if (g_Eth_tx_iudma_ownership[chnl] != HOST_OWNED)
             continue;

        for ( q=0; q < ENET_TX_EGRESS_QUEUES_MAX; q++ )
            printk("[HOST] ENET %4u %4u %10u %10u\n",
               chnl, q,
               (uint32_t) txdma->txDropThr[q],
               (uint32_t) txdma->txDropThrPkts[q]);
    }
}

/* print the BPM TxQ Drop Thresh */
static void enet_bpm_dump_tx_drop_thr(void)
{
    enet_bpm_dma_dump_tx_drop_thr();
}


/* init ENET TxQ drop thresholds */
static void enet_bpm_init_tx_drop_thr(BcmEnet_devctrl *pDevCtrl, int chnl)
{
    BcmPktDma_EthTxDma *txdma = pDevCtrl->txdma[chnl];
    int nr_tx_bds;

    nr_tx_bds = bcmPktDma_EthGetTxBds( txdma, chnl );
    BCM_ASSERT(nr_tx_bds > 0);
    enet_bpm_dma_tx_drop_thr[chnl][0] =
                    (nr_tx_bds * ENET_BPM_PCT_TXQ0_DROP_THRESH)/100;
    enet_bpm_dma_tx_drop_thr[chnl][1] =
                    (nr_tx_bds * ENET_BPM_PCT_TXQ1_DROP_THRESH)/100;
    enet_bpm_dma_tx_drop_thr[chnl][2] =
                    (nr_tx_bds * ENET_BPM_PCT_TXQ2_DROP_THRESH)/100;
    enet_bpm_dma_tx_drop_thr[chnl][3] =
                    (nr_tx_bds * ENET_BPM_PCT_TXQ3_DROP_THRESH)/100;

    BCM_ENET_DEBUG("Enet: BPM DMA Init Tx Drop Thresh: chnl=%u txbds=%u thr[0]=%u thr[1]=%u thr[2]=%u thr[3]=%u\n",
                chnl, nr_tx_bds,
                enet_bpm_dma_tx_drop_thr[chnl][0],
                enet_bpm_dma_tx_drop_thr[chnl][1],
                enet_bpm_dma_tx_drop_thr[chnl][2],
                enet_bpm_dma_tx_drop_thr[chnl][3]);
}


#endif


#endif

void bcmeapi_module_init(void)
{
    int idx;

#if defined(_CONFIG_BCM_INGQOS)
    iqos_enet_status_hook_g = enet_iq_status;
#endif

#if defined(_CONFIG_BCM_BPM)
    gbpm_enet_status_hook_g = enet_bpm_status;
#if defined(_CONFIG_BCM_FAP)
    gbpm_enet_thresh_hook_g = enet_bpm_dump_tx_drop_thr;
#endif
#endif

    /* Initialize the static global array */
    for (idx = 0; idx < ENET_RX_CHANNELS_MAX; ++idx)
    {
        pending_channel[idx] = idx;
    }
}

