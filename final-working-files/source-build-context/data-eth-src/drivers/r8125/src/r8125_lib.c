/*
################################################################################
#
# r8168 is the Linux device driver released for Realtek Gigabit Ethernet
# controllers with PCI-Express interface.
#
# Copyright(c) 2022 Realtek Semiconductor Corp. All rights reserved.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, see <http://www.gnu.org/licenses/>.
#
# Author:
# Realtek NIC software team <nicfae@realtek.com>
# No. 2, Innovation Road II, Hsinchu Science Park, Hsinchu 300, Taiwan
#
################################################################################
*/

/************************************************************************************
 *  This product is covered by one or more of the following patents:
 *  US6,570,884, US6,115,776, and US6,327,625.
 ***********************************************************************************/

#include <linux/pci.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include "r8125.h"
#include "r8125_lib.h"

static void
rtl8125_map_to_asic(struct rtl8125_private *tp,
                    struct rtl8125_ring *ring,
                    struct RxDesc *desc,
                    dma_addr_t mapping,
                    u32 rx_buf_sz,
                    const u32 cur_rx)
{
        ring->bufs[cur_rx].dma_addr = mapping;
        if (tp->InitRxDescType == RX_DESC_RING_TYPE_3)
                ((struct RxDescV3 *)desc)->addr = cpu_to_le64(mapping);
        else
                desc->addr = cpu_to_le64(mapping);
        wmb();
        rtl8125_mark_to_asic(tp, desc, rx_buf_sz);
}

static void
rtl8125_lib_tx_fill(struct rtl8125_ring *ring)
{
        struct TxDesc *descs = ring->desc_addr;
        u32 i;

        for (i = 0; i < ring->ring_size; i++) {
                struct TxDesc *desc = &descs[i];

                desc->addr = cpu_to_le64(ring->bufs[i].dma_addr);

                if (i == (ring->ring_size - 1))
                        desc->opts1 = cpu_to_le32(RingEnd);
        }
}

static inline void
rtl8125_mark_as_last_descriptor_8125(struct RxDescV3 *descv3)
{
        descv3->RxDescNormalDDWord4.opts1 |= cpu_to_le32(RingEnd);
}

static inline void
rtl8125_mark_as_last_descriptor(struct rtl8125_private *tp,
                                struct RxDesc *desc)
{
        if (tp->InitRxDescType == RX_DESC_RING_TYPE_3)
                rtl8125_mark_as_last_descriptor_8125((struct RxDescV3 *)desc);
        else
                desc->opts1 |= cpu_to_le32(RingEnd);
}

static void
rtl8125_lib_rx_fill(struct rtl8125_ring *ring)
{
        struct rtl8125_private *tp = ring->private;
        struct RxDesc *desc;
        u32 i;

        for (i = 0; i < ring->ring_size; i++) {
                desc = rtl8125_get_rxdesc(tp, ring->desc_addr, i);
                rtl8125_map_to_asic(tp, ring, desc,
                                    ring->bufs[i].dma_addr, ring->buff_size, i);
        }

        rtl8125_mark_as_last_descriptor(tp,
                                        rtl8125_get_rxdesc(tp, ring->desc_addr, ring->ring_size - 1));
}

struct rtl8125_ring *rtl8125_get_tx_ring(struct rtl8125_private *tp)
{
        int i;

        WARN_ON_ONCE(tp->num_tx_rings < 1);

        for (i = tp->num_tx_rings; i < tp->HwSuppNumTxQueues; i++) {
                struct rtl8125_ring *ring = &tp->lib_tx_ring[i];
                if (!ring->allocated) {
                        ring->allocated = true;
                        return ring;
                }
        }

        return NULL;
}

struct rtl8125_ring *rtl8125_get_rx_ring(struct rtl8125_private *tp)
{
        int i;

        WARN_ON_ONCE(tp->num_rx_rings < 1);

        for (i = tp->num_rx_rings; i < tp->HwSuppNumRxQueues; i++) {
                struct rtl8125_ring *ring = &tp->lib_rx_ring[i];
                if (!ring->allocated) {
                        ring->allocated = true;
                        return ring;
                }
        }

        return NULL;
}

static void rtl8125_put_ring(struct rtl8125_ring *ring)
{
        if (!ring)
                return;

        ring->allocated = false;
}

static void rtl8125_init_rx_ring(struct rtl8125_ring *ring)
{
        struct rtl8125_private *tp = ring->private;
        u16 rdsar_reg;

        if (!ring->allocated)
                return;

        rtl8125_lib_rx_fill(ring);

        rdsar_reg = RDSAR_Q1_LOW_8125 + (ring->queue_num - 1) * 8;
        RTL_W32(tp, rdsar_reg, ((u64)ring->desc_daddr & DMA_BIT_MASK(32)));
        RTL_W32(tp, rdsar_reg + 4, ((u64)ring->desc_daddr >> 32));
}

static void rtl8125_init_tx_ring(struct rtl8125_ring *ring)
{
        struct rtl8125_private *tp = ring->private;
        u16 tdsar_reg;

        if (!ring->allocated)
                return;

        rtl8125_lib_tx_fill(ring);

        tdsar_reg = TNPDS_Q1_LOW_8125 + (ring->queue_num - 1) * 8;
        RTL_W32(tp, tdsar_reg, ((u64)ring->desc_daddr & DMA_BIT_MASK(32)));
        RTL_W32(tp, tdsar_reg + 4, ((u64)ring->desc_daddr >> 32));
}

static void rtl8125_free_ring_mem(struct rtl8125_ring *ring)
{
        unsigned i;
        struct rtl8125_private *tp = ring->private;
        struct pci_dev *pdev = tp->pci_dev;

        if (ring->desc_addr) {
                dma_free_coherent(&pdev->dev, ring->desc_size,
                                  ring->desc_addr, ring->desc_daddr);

                ring->desc_addr = NULL;
        }

        if (ring->flags & RTL8125_CONTIG_BUFS) {
                struct rtl8125_buf *rtl_buf = &ring->bufs[0];
                dma_free_coherent(
                        &pdev->dev,
                        ring->ring_size * ring->buff_size,
                        rtl_buf->addr,
                        rtl_buf->dma_addr);
        } else {
                for (i=0; i<ring->ring_size ; i++) {
                        struct rtl8125_buf *rtl_buf = &ring->bufs[i];
                        if (rtl_buf->addr) {
                                dma_free_coherent(
                                        &pdev->dev,
                                        rtl_buf->size,
                                        rtl_buf->addr,
                                        rtl_buf->dma_addr);

                                rtl_buf->addr = NULL;
                        }
                }
        }

        if (ring->bufs) {
                kfree(ring->bufs);
                ring->bufs = 0;
        }
}

static int rtl8125_alloc_ring_mem(struct rtl8125_ring *ring)
{
        int i;
        struct rtl8125_private *tp = ring->private;
        struct pci_dev *pdev = tp->pci_dev;

        ring->bufs = kzalloc(sizeof(struct rtl8125_buf) * ring->ring_size, GFP_KERNEL);
        if (!ring->bufs)
                return -ENOMEM;

        if (ring->mem_ops == NULL) {
                /* Use dma_alloc_coherent() and dma_free_coherent() below */
                if (ring->direction == RTL8125_CH_DIR_TX)
                        ring->desc_size = ring->ring_size * sizeof(struct TxDesc);
                else if (ring->direction == RTL8125_CH_DIR_RX)
                        ring->desc_size = ring->ring_size * tp->RxDescLength;

                ring->desc_addr = dma_alloc_coherent(
                                          &pdev->dev,
                                          ring->desc_size,
                                          &ring->desc_daddr,
                                          GFP_KERNEL);
                if (!ring->desc_addr)
                        goto error_out;

                memset(ring->desc_addr, 0x0, ring->desc_size);

                if (ring->flags & RTL8125_CONTIG_BUFS) {
                        struct rtl8125_buf *rtl_buf = &ring->bufs[0];

                        rtl_buf->size = ring->buff_size;
                        rtl_buf->addr = dma_alloc_coherent(
                                                &pdev->dev,
                                                ring->ring_size * ring->buff_size,
                                                &rtl_buf->dma_addr,
                                                GFP_KERNEL);
                        if (!rtl_buf->addr)
                                goto error_out;

                        for (i = 1; i < ring->ring_size; i++) {
                                struct rtl8125_buf *rtl_buf = &ring->bufs[i];
                                struct rtl8125_buf *rtl_buf_prev = &ring->bufs[i-1];
                                rtl_buf->size = ring->buff_size;
                                rtl_buf->addr = rtl_buf_prev->addr + ring->buff_size;
                                rtl_buf->dma_addr = rtl_buf_prev->dma_addr + ring->buff_size;
                        }

                } else {
                        for (i = 0; i < ring->ring_size; i++) {
                                struct rtl8125_buf *rtl_buf = &ring->bufs[i];

                                rtl_buf->size = ring->buff_size;
                                rtl_buf->addr = dma_alloc_coherent(
                                                        &pdev->dev,
                                                        rtl_buf->size,
                                                        &rtl_buf->dma_addr,
                                                        GFP_KERNEL);
                                if (!rtl_buf->addr)
                                        goto error_out;

                                memset(rtl_buf->addr, 0x0, rtl_buf->size);
                        }
                }
        }
#if 0
        /* Validate parameters */
        /* Allocate descs */
        mem_ops->alloc_descs(...);

        /* Allocate buffers */
        if (R8125B_CONTIG_BUFS) {
                mem_ops->alloc_buffs(...);
        } else {
                /* Call mem_ops->alloc_buffs(...) for each descriptor. */
        }
#endif

        return 0;

error_out:

        rtl8125_free_ring_mem(ring);

        return -ENOMEM;
}


struct rtl8125_ring *rtl8125_request_ring(struct net_device *ndev,
                unsigned int ring_size, unsigned int buff_size,
                enum rtl8125_channel_dir direction, unsigned int flags,
                struct rtl8125_mem_ops *mem_ops)
{
        struct rtl8125_private *tp = netdev_priv(ndev);
        struct rtl8125_ring * ring = 0;
        bool locked = true;

        if (direction == RTL8125_CH_DIR_TX) {
                ring = rtl8125_get_tx_ring(tp);
                if (!ring)
                        goto error_out;
        } else if (direction == RTL8125_CH_DIR_RX) {
                ring = rtl8125_get_rx_ring(tp);
                if (!ring)
                        goto error_out;
        } else
                goto error_out;

        ring->ring_size = ring_size;
        ring->buff_size = buff_size;
        ring->mem_ops = mem_ops;
        ring->flags = flags;

        if (rtl8125_alloc_ring_mem(ring))
                goto error_out;

        /* initialize descriptors to point to buffers allocated */
        if (!rtnl_trylock())
                locked = false;

        if (direction == RTL8125_CH_DIR_TX)
                rtl8125_init_tx_ring(ring);
        else if (direction == RTL8125_CH_DIR_RX)
                rtl8125_init_rx_ring(ring);

        if (locked)
                rtnl_unlock();

        return ring;

error_out:
        rtl8125_free_ring_mem(ring);
        rtl8125_put_ring(ring);

        return NULL;
}
EXPORT_SYMBOL(rtl8125_request_ring);

static int rtl8125_all_ring_released(struct rtl8125_private *tp)
{
        int i;
        int released = 0;

        for (i = tp->num_tx_rings; i < tp->HwSuppNumTxQueues; i++) {
                struct rtl8125_ring *ring = &tp->lib_tx_ring[i];
                if (ring->allocated)
                        goto exit;
        }

        for (i = tp->num_rx_rings; i < tp->HwSuppNumRxQueues; i++) {
                struct rtl8125_ring *ring = &tp->lib_rx_ring[i];
                if (ring->allocated)
                        goto exit;
        }

        released = 1;

exit:
        return released;
}

void rtl8125_release_ring(struct rtl8125_ring *ring)
{
        struct rtl8125_private *tp;
        bool locked = true;

        if (!ring)
                return;

        tp = ring->private;

        rtl8125_free_ring_mem(ring);
        rtl8125_put_ring(ring);
        if (rtl8125_all_ring_released(tp)) {
                struct net_device *dev = tp->dev;
                if (!rtnl_trylock())
                        locked = false;

                if (netif_running(dev)) {
                        rtl8125_close(dev);
                        rtl8125_open(dev);
                } else
                        rtl8125_enable_hw_linkchg_interrupt(tp);

                if (locked)
                        rtnl_unlock();
        }
}
EXPORT_SYMBOL(rtl8125_release_ring);

int rtl8125_enable_ring(struct rtl8125_ring *ring)
{
        struct rtl8125_private *tp;
        struct net_device *dev;
        bool locked = true;

        if (!ring)
                return -EINVAL;

        if (!(ring->direction == RTL8125_CH_DIR_TX || ring->direction == RTL8125_CH_DIR_RX))
                return -EINVAL;

        if (!rtnl_trylock())
                locked = false;

        tp = ring->private;
        dev = tp->dev;

        /* Start the ring if needed */
        netif_tx_disable(dev);
        _rtl8125_wait_for_quiescence(dev);
        rtl8125_hw_reset(dev);
        rtl8125_tx_clear(tp);
        rtl8125_rx_clear(tp);
        rtl8125_init_ring(dev);

        ring->enabled = true;

        rtl8125_hw_config(dev);
        rtl8125_hw_start(dev);

#ifdef CONFIG_R8125_NAPI
        rtl8125_enable_napi(tp);
#endif//CONFIG_R8125_NAPI

        netif_tx_start_all_queues(dev);

        if (locked)
                rtnl_unlock();

        return 0;
}
EXPORT_SYMBOL(rtl8125_enable_ring);

void rtl8125_disable_ring(struct rtl8125_ring *ring)
{
        struct rtl8125_private *tp;
        struct net_device *dev;
        bool locked = true;

        /* Stop the ring if possible. IPA do not want to receive or transmit
        packets beyond this point.
        */

        if (!ring)
                return;

        if (!(ring->direction == RTL8125_CH_DIR_TX || ring->direction == RTL8125_CH_DIR_RX))
                return;

        tp = ring->private;
        dev = tp->dev;

        if (!rtnl_trylock())
                locked = false;

        rtl8125_hw_reset(dev);
        //rtl8125_tx_clear(tp);
        //rtl8125_rx_clear(tp);
        //rtl8125_init_ring(dev);

        ring->enabled = false;

        //rtl8125_hw_config(dev);
        //rtl8125_hw_start(dev);

        if (locked)
                rtnl_unlock();
}
EXPORT_SYMBOL(rtl8125_disable_ring);

int rtl8125_request_event(struct rtl8125_ring *ring, unsigned long flags,
                          dma_addr_t addr, u64 data)
{
        struct rtl8125_private *tp;
        struct pci_dev *pdev;
        u32 message_id;
        bool locked = true;

        if (!ring)
                return -EINVAL;

        if (!(ring->direction == RTL8125_CH_DIR_TX || ring->direction == RTL8125_CH_DIR_RX))
                return -EINVAL;

        if (ring->event.allocated)
                return -EEXIST;

        if (ring->direction == RTL8125_CH_DIR_TX)
                message_id = (ring->queue_num == 0 ? 16 : 18);
        else
                message_id = ring->queue_num;

        tp = ring->private;
        pdev = tp->pci_dev;

        if (flags & MSIX_event_type) {
                /* Update MSI-X table entry with @addr and @data */
                /* Initialize any MSI-X/interrupt related register in HW */
                u16 reg = message_id * 0x10;

                if (!rtnl_trylock())
                        locked = false;

                ring->event.addr = rtl8125_eri_read(tp, reg, 4, ERIAR_MSIX);
                ring->event.addr |= (u64)rtl8125_eri_read(tp, reg + 4, 4, ERIAR_MSIX) << 32;
                ring->event.data = rtl8125_eri_read(tp, reg + 8, 4, ERIAR_MSIX);
                ring->event.data |= (u64)rtl8125_eri_read(tp, reg + 8, 4, ERIAR_MSIX) << 32;

                rtl8125_eri_write(tp, reg, 4, (u64)addr & DMA_BIT_MASK(32), ERIAR_MSIX);
                rtl8125_eri_write(tp, reg + 4, 4, (u64)addr >> 32, ERIAR_MSIX);
                rtl8125_eri_write(tp, reg + 8, 4, data, ERIAR_MSIX);
                rtl8125_eri_write(tp, reg + 12, 4, data >> 32, ERIAR_MSIX);

                if (locked)
                        rtnl_unlock();

                ring->event.message_id = message_id;
                ring->event.allocated = 1;
        }

        return 0;
}
EXPORT_SYMBOL(rtl8125_request_event);

void rtl8125_release_event(struct rtl8125_ring *ring)
{
        struct rtl8125_private *tp;
        dma_addr_t addr;
        u64 data;
        u16 reg;
        bool locked = true;

        /* Reverse request_event() */
        if (!ring->event.allocated)
                return;

        if (!(ring->direction == RTL8125_CH_DIR_TX || ring->direction == RTL8125_CH_DIR_RX))
                return;

        if (!ring->event.allocated)
                return;

        tp = ring->private;

        reg = ring->event.message_id * 0x10;

        addr = ring->event.addr;
        data = ring->event.data;

        if (!rtnl_trylock())
                locked = false;

        rtl8125_eri_write(tp, reg, 4, (u64)addr & DMA_BIT_MASK(32), ERIAR_MSIX);
        rtl8125_eri_write(tp, reg + 4, 4, (u64)addr >> 32, ERIAR_MSIX);
        rtl8125_eri_write(tp, reg + 8, 4, data, ERIAR_MSIX);
        rtl8125_eri_write(tp, reg + 12, 4, data >> 32, ERIAR_MSIX);

        if (locked)
                rtnl_unlock();

        ring->event.allocated = 0;

        return;
}
EXPORT_SYMBOL(rtl8125_release_event);

int rtl8125_enable_event(struct rtl8125_ring *ring)
{
        struct rtl8125_private *tp = ring->private;
        bool locked = true;

        if (!ring->event.allocated)
                return -EINVAL;

        if (!rtnl_trylock())
                locked = false;

        /* Set interrupt moderation timer */
        rtl8125_set_ring_intr_mod(ring, ring->event.delay);

        /* Enable interrupt */
        rtl8125_enable_hw_interrupt_v2(tp, ring->event.message_id);

        if (locked)
                rtnl_unlock();

        ring->event.enabled = 1;

        return 0;
}
EXPORT_SYMBOL(rtl8125_enable_event);

int rtl8125_disable_event(struct rtl8125_ring *ring)
{
        struct rtl8125_private *tp = ring->private;
        bool locked = true;

        if (!ring->event.allocated)
                return -EINVAL;

        if (!rtnl_trylock())
                locked = false;

        /* Disable interrupt */
        rtl8125_disable_hw_interrupt_v2(tp, ring->event.message_id);

        if (locked)
                rtnl_unlock();

        ring->event.enabled = 0;

        return 0;
}
EXPORT_SYMBOL(rtl8125_disable_event);

int rtl8125_set_ring_intr_mod(struct rtl8125_ring *ring, int delay)
{
        struct rtl8125_private *tp = ring->private;

        if (!ring->event.allocated)
                return -EFAULT;

        ring->event.delay = delay;

        /* Set interrupt moderation timer */
        rtl8125_hw_set_timer_int_8125(tp, ring->event.message_id, ring->event.delay);

        return 0;
}
EXPORT_SYMBOL(rtl8125_set_ring_intr_mod);

int rtl8125_rss_redirect(struct net_device *ndev,
                         unsigned long flags,
                         struct rtl8125_ring *ring)
{
        struct rtl8125_private *tp = ring->private;
        int i;

        /* Disable RSS if needed */
        /* Update RSS hash table to set all entries point to ring->queue */
        /* Set additional flags as needed. Ex. hash_type */
        /* Enable RSS */

        for (i = 0; i < rtl8125_rss_indir_tbl_entries(tp); i++)
                tp->rss_indir_tbl[i] = ring->queue_num;

        _rtl8125_config_rss(tp);

        return 0;
}
EXPORT_SYMBOL(rtl8125_rss_redirect);

int rtl8125_rss_reset(struct net_device *ndev)
{
        struct rtl8125_private *tp = netdev_priv(ndev);

        /* Disable RSS */
        /* Reset RSS hash table */
        /* Enable RSS if that is the default config for driver */

        rtl8125_init_rss(tp);
        _rtl8125_config_rss(tp);

        return 0;
}
EXPORT_SYMBOL(rtl8125_rss_reset);

struct net_device *rtl8125_get_netdev(struct device *dev)
{
        struct pci_dev *pdev = to_pci_dev(dev);

        /* Get device private data from @dev */
        /* Retrieve struct net_device * from device private data */

        return pci_get_drvdata(pdev);
}
EXPORT_SYMBOL(rtl8125_get_netdev);

int rtl8125_receive_skb(struct net_device *net_dev, struct sk_buff *skb, bool napi)
{
        /* Update interface stats - rx_packets, rx_bytes */
        skb->protocol = eth_type_trans(skb, net_dev);
        return napi ? netif_receive_skb(skb) : netif_rx(skb);
}
EXPORT_SYMBOL(rtl8125_receive_skb);

int rtl8125_register_notifier(struct net_device *net_dev,
                              struct notifier_block *nb)
{
        struct rtl8125_private *tp = netdev_priv(net_dev);

        return atomic_notifier_chain_register(&tp->lib_nh, nb);
}
EXPORT_SYMBOL(rtl8125_register_notifier);

int rtl8125_unregister_notifier(struct net_device *net_dev,
                                struct notifier_block *nb)
{
        struct rtl8125_private *tp = netdev_priv(net_dev);

        return atomic_notifier_chain_unregister(&tp->lib_nh, nb);
}
EXPORT_SYMBOL(rtl8125_unregister_notifier);

void rtl8125_lib_reset_prepare(struct rtl8125_private *tp)
{
        atomic_notifier_call_chain(&tp->lib_nh,
                                   RTL8125_NOTIFY_RESET_PREPARE, NULL);
}
EXPORT_SYMBOL(rtl8125_lib_reset_prepare);

void rtl8125_lib_reset_complete(struct rtl8125_private *tp)
{
        atomic_notifier_call_chain(&tp->lib_nh,
                                   RTL8125_NOTIFY_RESET_COMPLETE, NULL);
}
EXPORT_SYMBOL(rtl8125_lib_reset_complete);

#define rtl8125_statistics rtl8125_counters
int rtl8125_lib_get_stats(struct net_device *ndev, struct rtl8125_statistics *stats)
{
        struct rtl8125_private *tp = netdev_priv(ndev);
        struct rtl8125_counters *counters;
        dma_addr_t paddr;
        int rc = -1;

        if (!stats)
                goto out;

        counters = tp->tally_vaddr;
        paddr = tp->tally_paddr;
        if (!counters)
                goto out;

        rc = rtl8125_dump_tally_counter(tp, paddr);
        if (rc < 0)
                goto out;

        *stats = *counters;

out:
        return rc;
}
EXPORT_SYMBOL(rtl8125_lib_get_stats);

int rtl8125_lib_save_regs(struct net_device *ndev, struct rtl8125_regs_save *stats)
{
        struct rtl8125_private *tp = netdev_priv(ndev);
        int i, max;

        //macio
        max = R8125_MAC_REGS_SIZE;
        for (i = 0; i < max; i++)
                stats->mac_io[i] = RTL_R8(tp, i);

        //pcie_phy
        max = R8125_EPHY_REGS_SIZE/2;
        for (i = 0; i < max; i++)
                stats->pcie_phy[i] = rtl8125_ephy_read(tp, i);

        //eth_phy
        max = R8125_PHY_REGS_SIZE/2;
        rtl8125_mdio_write(tp, 0x1f, 0x0000);
        for (i = 0; i < max; i++)
                stats->eth_phy[i] = rtl8125_mdio_read(tp, i);

        //eri
        max = R8125_ERI_REGS_SIZE/4;
        for (i = 0; i < max; i++)
                stats->eri_reg[i] = rtl8125_eri_read(tp, i, 4, ERIAR_ExGMAC);

        //pci_reg
        max = R8125_PCI_REGS_SIZE/4;
        for (i = 0; i < max; i++)
                pci_read_config_dword(tp->pci_dev, i, &stats->pci_reg[i]);

        //tx sw/hw pointer
        max = R8125_MAX_TX_QUEUES;
        for (i = 0; i < R8125_MAX_TX_QUEUES; i++) {
                stats->sw_tail_ptr_reg[i] = RTL_R16(tp, tp->tx_ring[i].sw_tail_ptr_reg);
                stats->hw_clo_ptr_reg[i] = RTL_R16(tp, tp->tx_ring[i].hw_clo_ptr_reg);
        }

        //sw0_tail_ptr and next_hwq0_clo_ptr
        stats->sw0_tail_ptr = RTL_R16(tp, SW_TAIL_PTR0_8125);
        stats->next_hwq0_clo_ptr = RTL_R16(tp, HW_CLO_PTR0_8125);
        stats->sw1_tail_ptr = RTL_R16(tp, SW_TAIL_PTR0_8125 + 4);
        stats->next_hwq1_clo_ptr = RTL_R16(tp, HW_CLO_PTR0_8125 + 4);

        //int_miti
        stats->int_miti_rxq0 = RTL_R8(tp, INT_MITI_V2_0_RX);
        stats->int_miti_txq0 = RTL_R8(tp, INT_MITI_V2_0_TX);
        stats->int_miti_rxq1 = RTL_R8(tp, INT_MITI_V2_1_RX);
        stats->int_miti_txq1 = RTL_R8(tp, INT_MITI_V2_1_TX);

        //imr/isr
        stats->imr_new = RTL_R32(tp, IMR0_8125);
        stats->isr_new = RTL_R32(tp, ISR0_8125);

        //tdu/rdu
        stats->tdu_status = RTL_R8(tp, TDU_STA_8125);
        stats->rdu_status = RTL_R16(tp, RDU_STA_8125);

        //tc mode
        stats->tc_mode = RTL_R16(tp, TX_NEW_CTRL);

        //pla_tx_q0_idle_credit
        stats->pla_tx_q0_idle_credit = RTL_R32(tp, PLA_TXQ0_IDLE_CREDIT);
        stats->pla_tx_q1_idle_credit = RTL_R32(tp, PLA_TXQ1_IDLE_CREDIT);

        //txq1_dsc_st_addr
        stats->txq1_dsc_st_addr_0 = RTL_R32(tp, TNPDS_Q1_LOW_8125);
        stats->txq1_dsc_st_addr_2 = RTL_R32(tp, TNPDS_Q1_LOW_8125 + 4);

        //rxq1_dsc_st_addr
        stats->rxq1_dsc_st_addr_0 = RTL_R32(tp, RDSAR_Q1_LOW_8125);
        stats->rxq1_dsc_st_addr_2 = RTL_R32(tp, RDSAR_Q1_LOW_8125 + 4);

        //rss
        stats->rss_ctrl = RTL_R32(tp, RSS_CTRL_8125);
        for (i = 0; i < RTL8125_RSS_KEY_SIZE; i++)
                stats->rss_key[i] = RTL_R8(tp, RSS_KEY_8125 + i);

        for (i = 0; i < RTL8125_MAX_INDIRECTION_TABLE_ENTRIES; i++)
                stats->rss_i_table[i] = RTL_R8(tp, RSS_INDIRECTION_TBL_8125_V2 + i);

        stats->rss_queue_num_sel_r = RTL_R16(tp, Q_NUM_CTRL_8125);

        return 0;
}
EXPORT_SYMBOL(rtl8125_lib_save_regs);

void rtl8125_init_lib_ring(struct rtl8125_private *tp)
{
        int i;

        for (i = tp->num_tx_rings; i < tp->HwSuppNumTxQueues; i++) {
                struct rtl8125_ring *ring = &tp->lib_tx_ring[i];

                if (!ring->allocated)
                        continue;

                if (ring->event.enabled)
                        rtl8125_enable_event(ring);

                rtl8125_init_tx_ring(ring);
        }

        for (i = tp->num_rx_rings; i < tp->HwSuppNumRxQueues; i++) {
                struct rtl8125_ring *ring = &tp->lib_rx_ring[i];

                if (!ring->allocated)
                        continue;

                if (ring->event.enabled)
                        rtl8125_enable_event(ring);

                rtl8125_init_rx_ring(ring);
        }
}

/*
int rtl8125_lib_printf_macio_regs(struct net_device *ndev, struct rtl8125_regs_save *stats)
{
        struct rtl8125_private *tp = netdev_priv(ndev);
        int i;

        //00
        for(i=0; i<6; i++)
                printk("mac_id[6] = 0x%x\n", stats->mac_reg.mac_id[i]);
        printk("reg_06 = 0x%x\n", stats->mac_reg.reg_06);
        for(i=0; i<8; i++)
                printk("mar[8] = 0x%x\n", stats->mac_reg.mar[i]);
        //10
        printk("dtccr = 0x%llx\n", stats->mac_reg.dtccr);
        printk("ledsel0 = 0x%x\n", stats->mac_reg.ledsel0);
        printk("legreg = 0x%x\n", stats->mac_reg.legreg);
        printk("tctr3 = 0x%x\n", stats->mac_reg.tctr3);
        //20
        printk("txq0_desc_addr = 0x%llx\n", stats->mac_reg.txq0_desc_addr);
        printk("reg_28 = 0x%llx\n", stats->mac_reg.reg_28);
        //30
        printk("rit = 0x%x\n", stats->mac_reg.rit);
        printk("ritc = 0x%x\n", stats->mac_reg.ritc);
        printk("reg_34 = 0x%x\n", stats->mac_reg.reg_34);
        printk("cr = 0x%x\n", stats->mac_reg.cr);
        printk("imr0 = 0x%x\n", stats->mac_reg.imr0);
        printk("isr0 = 0x%x\n", stats->mac_reg.isr0);
        //40
        printk("tcr = 0x%x\n", stats->mac_reg.tcr);
        printk("rcr = 0x%x\n", stats->mac_reg.rcr);
        printk("tctr0 = 0x%x\n", stats->mac_reg.tctr0);
        printk("tctr1 = 0x%x\n", stats->mac_reg.tctr1);
        //50
        printk("cr93c46 = 0x%x\n", stats->mac_reg.cr93c46);
        printk("config0 = 0x%x\n", stats->mac_reg.config0);
        printk("config1 = 0x%x\n", stats->mac_reg.config1);
        printk("config2 = 0x%x\n", stats->mac_reg.config2);
        printk("config3 = 0x%x\n", stats->mac_reg.config3);
        printk("config4 = 0x%x\n", stats->mac_reg.config4);
        printk("config5 = 0x%x\n", stats->mac_reg.config5);
        printk("tdfnr = 0x%x\n", stats->mac_reg.tdfnr);
        printk("timer_int0 = 0x%x\n", stats->mac_reg.timer_int0);
        printk("timer_int1 = 0x%x\n", stats->mac_reg.timer_int1);
        //60
        printk("gphy_mdcmdio = 0x%x\n", stats->mac_reg.gphy_mdcmdio);
        printk("csidr = 0x%x\n", stats->mac_reg.csidr);
        printk("csiar = 0x%x\n", stats->mac_reg.csiar);
        printk("phy_status = 0x%x\n", stats->mac_reg.phy_status);
        printk("config6 = 0x%x\n", stats->mac_reg.config6);
        printk("pmch = 0x%x\n", stats->mac_reg.pmch);
        //70
        printk("eridr = 0x%x\n", stats->mac_reg.eridr);
        printk("eriar = 0x%x\n", stats->mac_reg.eriar);
        printk("config7 = 0x%x\n", stats->mac_reg.config7);
        printk("reg_7a = 0x%x\n", stats->mac_reg.reg_7a);
        printk("ephy_rxerr_cnt = 0x%x\n", stats->mac_reg.ephy_rxerr_cnt);
        //80
        printk("ephy_mdcmdio = 0x%x\n", stats->mac_reg.ephy_mdcmdio);
        printk("ledsel2 = 0x%x\n", stats->mac_reg.ledsel2);
        printk("ledsel1 = 0x%x\n", stats->mac_reg.ledsel1);
        printk("tctr2 = 0x%x\n", stats->mac_reg.tctr2);
        printk("timer_int2 = 0x%x\n", stats->mac_reg.timer_int2);
        //90
        printk("tppoll0 = 0x%x\n", stats->mac_reg.tppoll0);
        printk("reg_91 = 0x%x\n", stats->mac_reg.reg_91);
        printk("reg_92 = 0x%x\n", stats->mac_reg.reg_92);
        printk("led_feature = 0x%x\n", stats->mac_reg.led_feature);
        printk("ledsel3 = 0x%x\n", stats->mac_reg.ledsel3);
        printk("eee_led_config = 0x%x\n", stats->mac_reg.eee_led_config);
        printk("reg_9a = 0x%x\n", stats->mac_reg.reg_9a);
        printk("reg_9c = 0x%x\n", stats->mac_reg.reg_9c);
        //a0
        printk("reg_a0 = 0x%x\n", stats->mac_reg.reg_a0);
        printk("reg_a4 = 0x%x\n", stats->mac_reg.reg_a4);
        printk("reg_a8 = 0x%x\n", stats->mac_reg.reg_a8);
        printk("reg_ac = 0x%x\n", stats->mac_reg.reg_ac);
        //b0
        printk("patch_dbg = 0x%x\n", stats->mac_reg.patch_dbg);
        printk("reg_b4 = 0x%x\n", stats->mac_reg.reg_b4);
        printk("gphy_ocp = 0x%x\n", stats->mac_reg.gphy_ocp);
        printk("reg_bc = 0x%x\n", stats->mac_reg.reg_bc);
        //c0
        printk("reg_c0 = 0x%x\n", stats->mac_reg.reg_c0);
        printk("reg_c4 = 0x%x\n", stats->mac_reg.reg_c4);
        printk("reg_c8 = 0x%x\n", stats->mac_reg.reg_c8);
        printk("otp_cmd = 0x%x\n", stats->mac_reg.otp_cmd);
        printk("otp_pg_config = 0x%x\n", stats->mac_reg.otp_pg_config);
        //d0
        printk("phy_pwr = 0x%x\n", stats->mac_reg.phy_pwr);
        printk("twsi_ctrl = 0x%x\n", stats->mac_reg.twsi_ctrl);
        printk("oob_ctrl = 0x%x\n", stats->mac_reg.oob_ctrl);
        printk("mac_dbgo = 0x%x\n", stats->mac_reg.mac_dbgo);
        printk("mac_dbg = 0x%x\n", stats->mac_reg.mac_dbg);
        printk("reg_d8 = 0x%x\n", stats->mac_reg.reg_d8);
        printk("rms = 0x%x\n", stats->mac_reg.rms);
        printk("efuse_data = 0x%x\n", stats->mac_reg.efuse_data);
        //e0
        printk("cpcr = 0x%x\n", stats->mac_reg.cpcr);
        printk("reg_e2 = 0x%x\n", stats->mac_reg.reg_e2);
        printk("rxq0_desc_addr = 0x%llx\n", stats->mac_reg.rxq0_desc_addr);
        printk("reg_ec = 0x%x\n", stats->mac_reg.reg_ec);
        printk("tx10midle_cnt = 0x%x\n", stats->mac_reg.tx10midle_cnt);
        //f0
        printk("misc0 = 0x%x\n", stats->mac_reg.misc0);
        printk("misc1 = 0x%x\n", stats->mac_reg.misc1);
        printk("timer_int3 = 0x%x\n", stats->mac_reg.timer_int3);
        printk("cmac_ib = 0x%x\n", stats->mac_reg.cmac_ib);
        printk("reg_fc = 0x%x\n", stats->mac_reg.reg_fc);
        printk("sw_rst = 0x%x\n", stats->mac_reg.sw_rst);

        return 0;
}
*/
