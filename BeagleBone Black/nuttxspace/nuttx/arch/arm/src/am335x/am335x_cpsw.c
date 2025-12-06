/****************************************************************************
 * arch/arm/src/am335x/am335x_cpsw.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>
#include <arch/irq.h>
#include <errno.h>          /* For ETIMEDOUT */
#include <nuttx/net/netdev.h>
#include <nuttx/kmalloc.h>  /* For kmm_zalloc */
#include <syslog.h>         /* For ninfo */
#include <string.h>         /* For memcpy */
#include <nuttx/arch.h>     /* For up_invalidate_dcache, up_clean_dcache */
#include <nuttx/irq.h>      /* For irq_attach, up_enable_irq */
#include <nuttx/net/ip.h>   /* For ipv4_input */
#include <nuttx/wqueue.h>   /* For work_queue */

#include "arm_internal.h"
#include "am335x_cpsw.h"
#include "am335x_pinmux.h"
#include "am335x_gpio.h"
#include "hardware/am335x_pinmux.h"
#include "hardware/am335x_memorymap.h"
#include "hardware/am335x_cpsw.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* CRITICAL FIX for NuttX Flat Builds:
 * If Address Environments are not enabled, up_addrenv_va_to_pa is not compiled.
 * On AM335x Flat Build, Virtual Address == Physical Address for RAM.
 */
#ifndef up_addrenv_va_to_pa
#  define up_addrenv_va_to_pa(va) ((uintptr_t)(va))
#endif

/* AM335x CPSW Interrupt Definitions (TRM Section 6) */
#ifndef AM335X_IRQ_C0_RX_THRESH
#  define AM335X_IRQ_C0_RX_THRESH  40
#endif
#ifndef AM335X_IRQ_C0_RX_PULSE
#  define AM335X_IRQ_C0_RX_PULSE   41
#endif
#ifndef AM335X_IRQ_C0_TX_PULSE
#  define AM335X_IRQ_C0_TX_PULSE   42
#endif
#ifndef AM335X_IRQ_C0_MISC_PULSE
#  define AM335X_IRQ_C0_MISC_PULSE 43
#endif

/* CRITICAL SELECTION:
 * Use PULSE interrupts (41/42) for standard driver operation.
 * THRESHOLD interrupts (40) are for coalescing and usually require
 * specific threshold register configuration to fire at all.
 */
#define AM335X_IRQ_CPSW_RX_INT  AM335X_IRQ_C0_RX_PULSE
#define AM335X_IRQ_CPSW_TX_INT  AM335X_IRQ_C0_TX_PULSE

#define AM335X_MAC_ID0_LO     (AM335X_CTRL_BASE + 0x630)
#define AM335X_MAC_ID0_HI     (AM335X_CTRL_BASE + 0x634)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct am335x_cpsw_s
{
  struct net_driver_s dev;
  uint32_t phy_id;
  uint8_t phy_addr;

  /* DMA Buffer Descriptors */
  struct am335x_cpdma_bd_s *tx_descs;
  struct am335x_cpdma_bd_s *rx_descs;
  uint8_t                  *rx_bufs[CPSW_RX_DESCRIPTORS];
  volatile int              tx_head;
  volatile int              tx_tail;
  volatile int              rx_curr;
  
  /* Work Queue for Polling */
  struct work_s             poll_work;
};

/****************************************************************************
 * Private Functions (Forward Declarations)
 ****************************************************************************/

static int am335x_cpsw_ifup(FAR struct net_driver_s *dev);
static int am335x_cpsw_ifdown(FAR struct net_driver_s *dev);
static int am335x_cpsw_transmit(FAR struct net_driver_s *dev);
static int am335x_cpsw_interrupt(int irq, void *context, void *arg);
static void am335x_cpsw_polltask(FAR void *arg);

/* Forward declaration for MII registers (usually in a header) */
#ifndef MII_ANAR
#  define MII_ANAR 0x04
#endif
#ifndef MII_BMSR
#  define MII_BMSR 0x01
#endif
#ifndef MII_ADVERTISE
#  define MII_ADVERTISE 0x04
#endif

/****************************************************************************
 * Private Functions (Implementations)
 ****************************************************************************/

static void am335x_get_mac(FAR struct net_driver_s *dev)
{
     uint32_t mac_lo = getreg32(AM335X_MAC_ID0_LO);
     uint32_t mac_hi = getreg32(AM335X_MAC_ID0_HI);

     dev->d_mac.ether.ether_addr_octet[0] = (mac_hi >> 8) & 0xFF;
     dev->d_mac.ether.ether_addr_octet[1] = (mac_hi >> 0) & 0xFF;
     
     dev->d_mac.ether.ether_addr_octet[2] = (mac_lo >> 24) & 0xFF;
     dev->d_mac.ether.ether_addr_octet[3] = (mac_lo >> 16) & 0xFF;
     dev->d_mac.ether.ether_addr_octet[4] = (mac_lo >> 8)  & 0xFF;
     dev->d_mac.ether.ether_addr_octet[5] = (mac_lo >> 0)  & 0xFF;
}

static void am335x_cpsw_pinmux(void)
{
   /* MII Transmit Lines */

   putreg32(0x00, AM335X_CTRL_BASE + MII1_TX_EN_OFFSET);
   putreg32(0x00, AM335X_CTRL_BASE + MII1_TXD3_OFFSET);
   putreg32(0x00, AM335X_CTRL_BASE + MII1_TXD2_OFFSET);
   putreg32(0x00, AM335X_CTRL_BASE + MII1_TXD1_OFFSET);
   putreg32(0x00, AM335X_CTRL_BASE + MII1_TXD0_OFFSET);

   /* MII Receive/Clock Lines */

   putreg32(0x20, AM335X_CTRL_BASE + MII1_COL_OFFSET);
   putreg32(0x20, AM335X_CTRL_BASE + MII1_CRS_OFFSET);
   putreg32(0x20, AM335X_CTRL_BASE + MII1_RX_ER_OFFSET);
   putreg32(0x20, AM335X_CTRL_BASE + MII1_RX_DV_OFFSET);
   putreg32(0x20, AM335X_CTRL_BASE + MII1_TXCLK_OFFSET);
   putreg32(0x20, AM335X_CTRL_BASE + MII1_RXCLK_OFFSET);
   putreg32(0x20, AM335X_CTRL_BASE + MII1_RXD3_OFFSET);
   putreg32(0x20, AM335X_CTRL_BASE + MII1_RXD2_OFFSET);
   putreg32(0x20, AM335X_CTRL_BASE + MII1_RXD1_OFFSET);
   putreg32(0x20, AM335X_CTRL_BASE + MII1_RXD0_OFFSET);

   /* MDIO Interface */

   putreg32(0x30, AM335X_CTRL_BASE + MDIO_DATA_OFFSET);
   putreg32(0x10, AM335X_CTRL_BASE + MDIO_CLK_OFFSET);
}

static void am335x_set_gmii_mode(void)
{
     uint32_t regval = getreg32(AM335X_GMII_SEL);
     
     regval &= ~(0x03); /* 00 = MII mode for Port 1 */
     
     putreg32(regval, AM335X_GMII_SEL);
}

static void am335x_phy_reset(void)
{
     am335x_gpio_config(GPIO_OUTPUT | GPIO_OUTPUT_ZERO | GPIO_PORT1 | GPIO_PIN28);
     up_mdelay(20);

     am335x_gpio_config(GPIO_OUTPUT | GPIO_OUTPUT_ONE | GPIO_PORT1 | GPIO_PIN28);
     up_mdelay(100); 
}

static void am335x_cpsw_clk_enable(void)
{
   putreg32(0x02, AM335X_CM_PER_CPSW_CLK);
   putreg32(0x02, AM335X_CM_PER_CPGMAC0_CLKCTRL);
}

static void am335x_mdio_init(void)
{
     putreg32(0x400400FF, AM335X_MDIO_CTRL);
}

static int am335x_mdio_wait_for_user_access(void)
{
   volatile int timeout = 10000;
   uint32_t reg;

   while (timeout > 0)
     {
       reg = getreg32(AM335X_MDIO_USERACCESS0);
       
       if ((reg & MDIO_USERACCESS_GO) == 0)
         {
           return OK;
         }

       up_udelay(10);
       timeout--;
     }

   return -ETIMEDOUT;
}

static uint16_t am335x_mdio_read(uint8_t phyaddr, uint8_t regaddr)
{
   uint32_t cmd;
   uint32_t regval;

   if (am335x_mdio_wait_for_user_access() != OK)
     {
       return 0xFFFF;
     }

   cmd = MDIO_USERACCESS_GO |
         MDIO_USERACCESS_READ |
         ((regaddr & 0x1F) << MDIO_USERACCESS_REGADR_SHIFT) |
         ((phyaddr & 0x1F) << MDIO_USERACCESS_PHYADR_SHIFT);

   putreg32(cmd, AM335X_MDIO_USERACCESS0);

   if (am335x_mdio_wait_for_user_access() != OK)
     {
       return 0xFFFF;
     }

   regval = getreg32(AM335X_MDIO_USERACCESS0);
   
   if ((regval & MDIO_USERACCESS_ACK) == 0)
     {
       return 0xFFFF;
     }

   return (uint16_t)(regval & MDIO_USERACCESS_DATA_MASK);
}

static void am335x_mdio_write(uint8_t phyaddr, uint8_t regaddr, uint16_t value)
{
   uint32_t cmd;

   if (am335x_mdio_wait_for_user_access() != OK)
     {
       return;
     }

   cmd = MDIO_USERACCESS_GO |
         MDIO_USERACCESS_WRITE |
         ((regaddr & 0x1F) << MDIO_USERACCESS_REGADR_SHIFT) |
         ((phyaddr & 0x1F) << MDIO_USERACCESS_PHYADR_SHIFT) |
         (value & MDIO_USERACCESS_DATA_MASK);

   putreg32(cmd, AM335X_MDIO_USERACCESS0);

   am335x_mdio_wait_for_user_access();
}

/****************************************************************************
 * Name: am335x_cpsw_bd_init
 *
 * Description:
 * Allocates and initializes the TX and RX buffer descriptor rings.
 * Sets up the RX ring to be owned by hardware immediately.
 ****************************************************************************/
static int am335x_cpsw_bd_init(struct am335x_cpsw_s *priv)
{
   int i;
   struct am335x_cpdma_bd_s *bd;
   uint32_t phys_addr;

   /* ------------------------------------------------------------------
    * 1. Allocate TX Descriptors
    * ------------------------------------------------------------------ */
   priv->tx_descs = (struct am335x_cpdma_bd_s *)kmm_memalign(32, 
                    sizeof(struct am335x_cpdma_bd_s) * CPSW_TX_DESCRIPTORS);

   if (!priv->tx_descs) return -ENOMEM;

   for (i = 0; i < CPSW_TX_DESCRIPTORS; i++)
     {
       bd = &priv->tx_descs[i];
       
       phys_addr = (uint32_t)up_addrenv_va_to_pa((void *)&priv->tx_descs[(i + 1) % CPSW_TX_DESCRIPTORS]);
       
       bd->next = phys_addr;
       bd->buffer = 0;
       bd->buflen = 0;
       bd->mode = 0;
     }

   priv->tx_head = 0;
   priv->tx_tail = 0;

   /* ------------------------------------------------------------------
    * 2. Allocate RX Descriptors & Buffers
    * ------------------------------------------------------------------ */
   priv->rx_descs = (struct am335x_cpdma_bd_s *)kmm_memalign(32, 
                    sizeof(struct am335x_cpdma_bd_s) * CPSW_RX_DESCRIPTORS);

   if (!priv->rx_descs) 
     {
       kmm_free(priv->tx_descs);
       return -ENOMEM;
     }

   for (i = 0; i < CPSW_RX_DESCRIPTORS; i++)
     {
       bd = &priv->rx_descs[i];

       uint8_t *buffer = kmm_malloc(CPSW_BUF_SIZE);
       if (!buffer) 
         {
           return -ENOMEM;
         }

       phys_addr = (uint32_t)up_addrenv_va_to_pa((void *)&priv->rx_descs[(i + 1) % CPSW_RX_DESCRIPTORS]);
       bd->next = phys_addr;

       bd->buffer = (uint32_t)up_addrenv_va_to_pa((void *)buffer);
       priv->rx_bufs[i] = buffer; 

       bd->buflen = CPSW_BUF_SIZE & 0x7FF;

       bd->mode = CPDMA_BD_OWNER; 
     }

   /* Flush Data Cache for all Descriptors and Buffers */
   up_clean_dcache((uintptr_t)priv->tx_descs, 
                  (uintptr_t)priv->tx_descs + (sizeof(struct am335x_cpdma_bd_s) * CPSW_TX_DESCRIPTORS));
   up_clean_dcache((uintptr_t)priv->rx_descs, 
                  (uintptr_t)priv->rx_descs + (sizeof(struct am335x_cpdma_bd_s) * CPSW_RX_DESCRIPTORS));

   /* ------------------------------------------------------------------
    * 3. Start RX Engine
    * ------------------------------------------------------------------ */
   phys_addr = (uint32_t)up_addrenv_va_to_pa((void *)&priv->rx_descs[0]);
   putreg32(phys_addr, AM335X_CPSW_BASE + AM335X_CPSW_CPDMA_RX_HDP_0_OFFSET);

   priv->rx_curr = 0;

   return OK;
}

/****************************************************************************
 * Name: am335x_cpsw_transmit
 *
 * Description:
 * Called when the network stack has a packet to send (dev->d_buf).
 ****************************************************************************/
static int am335x_cpsw_transmit(FAR struct net_driver_s *dev)
{
   FAR struct am335x_cpsw_s *priv = (FAR struct am335x_cpsw_s *)dev->d_private;
   struct am335x_cpdma_bd_s *bd;
   uint32_t phys_addr;
   int head = priv->tx_head;

   bd = &priv->tx_descs[head];

   up_invalidate_dcache((uintptr_t)bd, (uintptr_t)bd + sizeof(struct am335x_cpdma_bd_s));

   if (bd->mode & CPDMA_BD_OWNER)
     {
       return -EBUSY;
     }

   phys_addr = (uint32_t)up_addrenv_va_to_pa((void *)priv->dev.d_buf);
   bd->buffer = phys_addr;
   bd->buflen = priv->dev.d_len & 0x7FF;

   bd->mode = CPDMA_BD_SOP | CPDMA_BD_EOP | CPDMA_BD_OWNER | (priv->dev.d_len & 0x7FF);

   up_clean_dcache((uintptr_t)priv->dev.d_buf, (uintptr_t)priv->dev.d_buf + priv->dev.d_len);
   up_clean_dcache((uintptr_t)bd, (uintptr_t)bd + sizeof(struct am335x_cpdma_bd_s));

   if (getreg32(AM335X_CPSW_BASE + AM335X_CPSW_CPDMA_TX_HDP_0_OFFSET) == 0)
     {
       putreg32((uint32_t)up_addrenv_va_to_pa((void *)bd), AM335X_CPSW_BASE + AM335X_CPSW_CPDMA_TX_HDP_0_OFFSET);
     }

   priv->tx_head = (head + 1) % CPSW_TX_DESCRIPTORS;

   return OK;
}

/****************************************************************************
 * Name: am335x_cpsw_interrupt
 *
 * Description:
 * This function processes the RX/TX descriptor events. It is called by the
 * interrupt handler OR the manual polling task.
 ****************************************************************************/
static int am335x_cpsw_interrupt(int irq, void *context, void *arg)
{
   struct am335x_cpsw_s *priv = (struct am335x_cpsw_s *)arg;
   uint32_t stat;
   
   /* In polling mode, we read the status register directly.
    * In interrupt mode, the interrupt logic would typically clear this.
    * Since we are in a Work Queue context, we clear it manually after reading.
    */
   stat = getreg32(AM335X_CPSW_BASE + AM335X_CPSW_CPDMA_INTSTAT);

   if (stat & 0x02) /* RX Event (Manual check for 0x2=RX_THRESH/RX_PULSE) */
     {
       struct am335x_cpdma_bd_s *rx_bd;
       int curr = priv->rx_curr;
       
       rx_bd = &priv->rx_descs[curr];
       
       up_invalidate_dcache((uintptr_t)rx_bd, (uintptr_t)rx_bd + sizeof(struct am335x_cpdma_bd_s));

       while ((rx_bd->mode & CPDMA_BD_OWNER) == 0)
         {
           uint16_t pktlen = rx_bd->mode & CPDMA_BD_PKTLEN_MASK;

           up_invalidate_dcache((uintptr_t)priv->rx_bufs[curr], 
                                (uintptr_t)priv->rx_bufs[curr] + pktlen);

           priv->dev.d_buf = priv->rx_bufs[curr];
           priv->dev.d_len = pktlen;
           
#ifdef CONFIG_NET_IPv4
           ipv4_input(&priv->dev); 
#else
# error "IPv4 must be enabled for CPSW driver"
#endif

           /* Acknowledge completion of this descriptor */
           putreg32((uint32_t)up_addrenv_va_to_pa((void *)rx_bd), AM335X_CPSW_BASE + CPSW_CPDMA_RX_CP_0);

           /* Re-arm descriptor for hardware */
           rx_bd->buflen = CPSW_BUF_SIZE & 0x7FF;
           rx_bd->mode = CPDMA_BD_OWNER;

           up_clean_dcache((uintptr_t)rx_bd, (uintptr_t)rx_bd + sizeof(struct am335x_cpdma_bd_s));

           curr = (curr + 1) % CPSW_RX_DESCRIPTORS;
           rx_bd = &priv->rx_descs[curr];
           up_invalidate_dcache((uintptr_t)rx_bd, (uintptr_t)rx_bd + sizeof(struct am335x_cpdma_bd_s));
         }

       priv->rx_curr = curr;
       
       /* In polling mode, we don't need to write to EOI */
     }

   if (stat & 0x04) /* TX Event (Manual check for 0x4=TX_PULSE) */
     {
       int tail = priv->tx_tail;
       struct am335x_cpdma_bd_s *tx_bd = &priv->tx_descs[tail];
       
       up_invalidate_dcache((uintptr_t)tx_bd, (uintptr_t)tx_bd + sizeof(struct am335x_cpdma_bd_s));

       while (tail != priv->tx_head && (tx_bd->mode & CPDMA_BD_OWNER) == 0)
         {
           /* Acknowledge completion of this descriptor */
           putreg32((uint32_t)up_addrenv_va_to_pa((void *)tx_bd), AM335X_CPSW_BASE + CPSW_CPDMA_TX_CP_0);

           tail = (tail + 1) % CPSW_TX_DESCRIPTORS;
           tx_bd = &priv->tx_descs[tail];
           up_invalidate_dcache((uintptr_t)tx_bd, (uintptr_t)tx_bd + sizeof(struct am335x_cpdma_bd_s));
         }

       priv->tx_tail = tail;

       /* In polling mode, we don't need to write to EOI */
     }
     
   /* CRITICAL: Acknowledge the status register to clear the polled event. */
   if (stat)
     {
        putreg32(stat, AM335X_CPSW_BASE + AM335X_CPSW_CPDMA_INTSTAT);
     }


   return OK;
}

/****************************************************************************
 * Name: am335x_cpsw_polltask
 *
 * Description:
 * Work queue task to periodically poll the CPDMA status register.
 ****************************************************************************/
static void am335x_cpsw_polltask(FAR void *arg)
{
  ninfo("polltask: running\n");
  struct am335x_cpsw_s *priv = (struct am335x_cpsw_s *)arg;
  uint32_t stat;
  static int poll_count = 0; /* Add a counter for periodic printing */

  /* Check for pending RX or TX events */
  stat = getreg32(AM335X_CPSW_BASE + AM335X_CPSW_CPDMA_INTSTAT);
  
  if (stat & 0x06) /* Check if RX (0x02) or TX (0x04) event is pending */
    {
       /* Manually call the interrupt handler to process packets */
       am335x_cpsw_interrupt(0, NULL, (void *)priv);
    }

  /* Diagnostic printing every ~5 seconds */
  if (++poll_count >= 30)
    {
      uint32_t good_frames = getreg32(AM335X_CPSW_BASE + AM335X_CPSW_STATS_RXGOODFRAMES);
      ninfo("POLL: INTSTAT=0x%08x, RXGOODFRAMES=%u\n", stat, good_frames);
      poll_count = 0;
    }

  /* Schedule the task again if the interface is still up */
  if ((priv->dev.d_flags & IFF_UP) != 0)
    {
      work_queue(LPWORK, &priv->poll_work, am335x_cpsw_polltask, priv, CPSW_POLL_TICKS);
    }
}


/****************************************************************************
 * Name: am335x_cpsw_ifup
 *
 * Description:
 * Bring up the Ethernet interface. Implements the d_ifup callback.
 ****************************************************************************/
static int am335x_cpsw_ifup(FAR struct net_driver_s *dev)
{
   FAR struct am335x_cpsw_s *priv = (FAR struct am335x_cpsw_s *)dev->d_private;
   volatile int timeout;
   uint32_t rx_head_phys;
   uint16_t bmsr;
   uint16_t anar; /* Auto-Negotiation Advertisement Register */
   uint32_t mac_hi, mac_lo;

   ninfo("Bringing up eth0\n");

   /* ------------------------------------------------------------------
    * Step A: Reset CPSW Subsystem
    * ------------------------------------------------------------------ */
   putreg32(1, AM335X_CPSW_BASE + AM335X_CPSW_SS_SOFT_RESET_OFFSET);
   timeout = 1000;
   while ((getreg32(AM335X_CPSW_BASE + AM335X_CPSW_SS_SOFT_RESET_OFFSET) & 1) != 0)
     {
       if (--timeout == 0)
         {
            nerr("ERROR: CPSW Reset Timed Out\n");
            return -ETIMEDOUT;
         }
       up_mdelay(1);
     }

   /* CRITICAL FIX: Explicit CPDMA Soft Reset */
   /* Write 1 to Bit 0 (SOFT_RESET) in TX/RX control registers */
   putreg32(1, AM335X_CPSW_BASE + AM335X_CPSW_CPDMA_TX_CONTROL_OFFSET);
   putreg32(1, AM335X_CPSW_BASE + AM335X_CPSW_CPDMA_RX_CONTROL_OFFSET);
   up_udelay(100); /* Small delay for reset pulse */
   
   /* Clear the reset bits (reset is complete when 0 is written back) */
   putreg32(0, AM335X_CPSW_BASE + AM335X_CPSW_CPDMA_TX_CONTROL_OFFSET);
   putreg32(0, AM335X_CPSW_BASE + AM335X_CPSW_CPDMA_RX_CONTROL_OFFSET);
   up_udelay(100);


   /* ------------------------------------------------------------------
    * Step B: Configure SLIVER1 (Port 1) -- Set to MII Full Duplex
    * ------------------------------------------------------------------ */
   /* 0x21 = MII Enable (Bit 5) | Full Duplex (Bit 0) */
   putreg32(0x21, AM335X_CPSW_BASE + AM335X_CPSW_SL_MACCONTROL_OFFSET);

   /* ------------------------------------------------------------------
    * Step C1: Host Port P0 Control (CRITICAL RX DATA PATH FIX)
    * ------------------------------------------------------------------ */
    /* Enable Port 0: PORT_EN (Bit 0) */
   putreg32(1, AM335X_CPSW_BASE + AM335X_CPSW_PORT_P0_CONTROL_OFFSET);


   /* ------------------------------------------------------------------
    * Step D: CPDMA Initialization 
    * ------------------------------------------------------------------ */
   putreg32(0, AM335X_CPSW_BASE + AM335X_CPSW_CPDMA_TX_HDP_0_OFFSET); 
   
   rx_head_phys = (uint32_t)up_addrenv_va_to_pa((void *)priv->rx_descs);
   putreg32(rx_head_phys, AM335X_CPSW_BASE + AM335X_CPSW_CPDMA_RX_HDP_0_OFFSET);

   /* 1. Set CPDMA Interrupt Masks (per channel) -- NOTE: These are NOT required for polling mode.
    * Leaving them cleared/disabled is safer. */
   putreg32(0, AM335X_CPSW_BASE + AM335X_CPSW_CPDMA_TX_INTMASK_SET);
   putreg32(0, AM335X_CPSW_BASE + AM335X_CPSW_CPDMA_RX_INTMASK_SET);
   
   /* 2. Enable Wrapper Interrupts (Aggregation) -- NOTE: These are NOT required for polling mode.
    * Leaving them cleared/disabled is safer. */
   putreg32(0, AM335X_CPSW_BASE + AM335X_CPSW_C0_MISC_EN_OFFSET); 
   putreg32(0, AM335X_CPSW_BASE + AM335X_CPSW_C0_RX_EN_OFFSET);
   putreg32(0, AM335X_CPSW_BASE + AM335X_CPSW_C0_TX_EN_OFFSET);

   /* 3. Enable CPDMA (must be done AFTER setting descriptors) */
   putreg32(1, AM335X_CPSW_BASE + AM335X_CPSW_CPDMA_TX_CONTROL_OFFSET);
   putreg32(1, AM335X_CPSW_BASE + AM335X_CPSW_CPDMA_RX_CONTROL_OFFSET);

   /* ------------------------------------------------------------------
    * Step E: Enable Statistics
    * ------------------------------------------------------------------ */
   putreg32(0x7, AM335X_CPSW_BASE + AM335X_CPSW_SS_STAT_PORT_EN);

   /* ------------------------------------------------------------------
    * Step F: PHY Configuration
    * ------------------------------------------------------------------ */
   
   /* 1. Configure Auto-Negotiation Advertisement (100FD, 100HD, 10FD, 10HD) 
    * 0x01E1 = 0001 1110 0001
    */
   anar = 0x01E1; 
   am335x_mdio_write(priv->phy_addr, MII_ANAR, anar);
   
   /* 2. Start PHY Auto-Negotiation */
   /* 0x1200 = Auto-Negotiate Enable (Bit 12) | Restart (Bit 9) */
   am335x_mdio_write(priv->phy_addr, MII_BMCR, 0x1200);
   
   ninfo("CPSW Initialized. Waiting for Link...\n");

   /* CRITICAL: Wait for Link Up */
   timeout = 5000; /* 5 seconds */
   while (timeout > 0)
     {
       /* Reading BMSR twice is often necessary to get current status */
       bmsr = am335x_mdio_read(priv->phy_addr, MII_BMSR);
       bmsr = am335x_mdio_read(priv->phy_addr, MII_BMSR);

       /* Check Link Status (Bit 2) */
       if (bmsr != 0xFFFF && (bmsr & (1 << 2))) 
         {
           ninfo("Link Up! BMSR: %04x\n", bmsr);
           netdev_carrier_on(&priv->dev);
           
           /* CRITICAL FIX: Enable CPSW master control (VLAN awareness DISABLED) */
           putreg32(1, AM335X_CPSW_BASE + AM335X_CPSW_CONTROL_OFFSET);

           /* CRITICAL FIX: Restore MDIO controller state after CPSW master enable */
           am335x_mdio_init();
           
           /* CRITICAL FINAL STEP: Start the Polling Task */
           ninfo("Scheduling polltask on LPWORK queue\n");
           work_queue(LPWORK, &priv->poll_work, am335x_cpsw_polltask, priv, CPSW_POLL_TICKS);

           return OK;
         }
       up_mdelay(1);
       timeout--;
     }

   nerr("ERROR: Link Up Timed Out! BMSR: %04x\n", bmsr);
   return -ETIMEDOUT;
}

/****************************************************************************
 * Name: am335x_cpsw_ifdown
 *
 * Description:
 * Bring down the Ethernet interface. Implements the d_ifdown callback.
 ****************************************************************************/
static int am335x_cpsw_ifdown(FAR struct net_driver_s *dev)
{
   ninfo("ifdown: bringing down interface\n");
   FAR struct am335x_cpsw_s *priv = (FAR struct am335x_cpsw_s *)dev->d_private;
   
   /* Cancel the polling work queue */
   work_cancel(LPWORK, &priv->poll_work);
   
   /* Reset the CPDMA and CPSW subsystem */
   putreg32(0, AM335X_CPSW_BASE + AM335X_CPSW_CPDMA_TX_CONTROL_OFFSET);
   putreg32(0, AM335X_CPSW_BASE + AM335X_CPSW_CPDMA_RX_CONTROL_OFFSET);
   putreg32(0, AM335X_CPSW_BASE + AM335X_CPSW_CONTROL_OFFSET);
   
   return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int am335x_cpsw_initialize(int devno)
{
   struct am335x_cpsw_s *priv;
   uint16_t phy_id1, phy_id2;
   int i;
   uint32_t alive;

   _info("CPSW Initializing\n");

   priv = (struct am335x_cpsw_s *)kmm_zalloc(sizeof(struct am335x_cpsw_s));
   if (!priv)
     {
       _err("Failed to allocate driver state\n");
       return -ENOMEM;
     }

   am335x_cpsw_clk_enable();
   _info("CPSW Clock Enabled\n");

   am335x_cpsw_pinmux();
   _info("CPSW Pinmux Complete\n");
   
   am335x_set_gmii_mode();
   _info("GMII Mode Set\n");
   
   am335x_phy_reset();
   _info("PHY Reset Complete\n");
   
   am335x_mdio_init();
   _info("MDIO Initialized\n");
   
   up_mdelay(10);
   alive = getreg32(AM335X_MDIO_ALIVE);
   _info("MDIO_ALIVE: %08x\n", alive);

   /* Find PHY Address */

   for (i = 0; i < 32; i++)
     {
       if ((alive >> i) & 1)
         {
           priv->phy_addr = i;
           phy_id1 = am335x_mdio_read(priv->phy_addr, MII_PHYID1);
           phy_id2 = am335x_mdio_read(priv->phy_addr, MII_PHYID2);
           if (phy_id1 != 0xffff && phy_id1 != 0x0000)
             {
               priv->phy_id = (phy_id1 << 16) | phy_id2;
               _info("PHY found at address %d, ID: %08x\n", i, priv->phy_id);
               break;
             }
         }
     }

   if (i == 32)
     {
       _err("No PHY found\n");
       kmm_free(priv);
       return -ENODEV;
     }

   /* Get MAC Address */

   am335x_get_mac(&priv->dev);
   ninfo("MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
          priv->dev.d_mac.ether.ether_addr_octet[0],
          priv->dev.d_mac.ether.ether_addr_octet[1],
          priv->dev.d_mac.ether.ether_addr_octet[2],
          priv->dev.d_mac.ether.ether_addr_octet[3],
          priv->dev.d_mac.ether.ether_addr_octet[4],
          priv->dev.d_mac.ether.ether_addr_octet[5]);

   /* Initialize Buffer Descriptors */
   if (am335x_cpsw_bd_init(priv) != OK)
     {
       _err("Failed to initialize BDs\n");
       kmm_free(priv);
       return -ENOMEM;
     }

   /* Register the network device */

   priv->dev.d_ifup    = am335x_cpsw_ifup;
   priv->dev.d_ifdown  = am335x_cpsw_ifdown;
   priv->dev.d_txavail = am335x_cpsw_transmit;
   priv->dev.d_ioctl   = NULL;
   priv->dev.d_private = priv;

   netdev_register(&priv->dev, NET_LL_ETHERNET);

   /* CRITICAL CHANGE: Interrupt path disabled. */
   /* We must ensure any previous attempts to enable IRQs are cancelled */
   up_disable_irq(AM335X_IRQ_CPSW_RX_INT);
   up_disable_irq(AM335X_IRQ_CPSW_TX_INT);

   _info("CPSW Driver Registered (Polling Mode)\n");

   return OK;
}
