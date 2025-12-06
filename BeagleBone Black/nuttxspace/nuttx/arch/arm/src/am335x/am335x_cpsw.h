/****************************************************************************
 * arch/arm/src/am335x/am335x_cpsw.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_AM335X_AM335X_CPSW_H
#define __ARCH_ARM_SRC_AM335X_AM335X_CPSW_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/list.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Pin Muxing Offsets (from Control Module Base 0x44E10000) */

#define MDIO_DATA_OFFSET    0x948
#define MDIO_CLK_OFFSET     0x94C

#define MII1_TX_EN_OFFSET   0x914
#define MII1_TXD3_OFFSET    0x91C
#define MII1_TXD2_OFFSET    0x920
#define MII1_TXD1_OFFSET    0x924
#define MII1_TXD0_OFFSET    0x928

#define MII1_COL_OFFSET     0x908
#define MII1_CRS_OFFSET     0x90C
#define MII1_RX_ER_OFFSET   0x910
#define MII1_RX_DV_OFFSET   0x918
#define MII1_TXCLK_OFFSET   0x92C
#define MII1_RXCLK_OFFSET   0x930
#define MII1_RXD3_OFFSET    0x934
#define MII1_RXD2_OFFSET    0x938
#define MII1_RXD1_OFFSET    0x93C
#define MII1_RXD0_OFFSET    0x940

/* Standard MII PHY Registers (from IEEE 802.3) */

#define MII_BMCR                  0x00 /* Basic Mode Control Register */
#define MII_BMSR                  0x01 /* Basic Mode Status Register */
#define MII_PHYID1                0x02 /* PHY Identifier Register 1 */
#define MII_PHYID2                0x03 /* PHY Identifier Register 2 */
#define MII_ANAR                  0x04 /* Auto-Negotiation Advertisement Register */
#define MII_LPA                   0x05 /* Link Partner Ability Register */
#define MII_ANER                  0x06 /* Auto-Negotiation Expansion Register */

/* CPDMA Buffer Descriptor (must be 16 bytes) */
struct am335x_cpdma_bd_s
{
  volatile uint32_t next;      /* Word 0: Physical Address of next BD */
  volatile uint32_t buffer;    /* Word 1: Physical Address of data buffer */
  volatile uint32_t buflen;    /* Word 2: Buffer Length (Low 11 bits) | Offset (High 16) */
  volatile uint32_t mode;      /* Word 3: Packet Length + Flags */
};

/* Word 3 Flag Definitions */
#define CPDMA_BD_SOP          (1 << 31) /* Start of Packet */
#define CPDMA_BD_EOP          (1 << 30) /* End of Packet */
#define CPDMA_BD_OWNER        (1 << 29) /* 1=Hardware, 0=Software */
#define CPDMA_BD_EOQ          (1 << 28) /* End of Queue (Hardware sets this) */
#define CPDMA_BD_TD_COMPLETE  (1 << 27) /* Teardown Complete */
#define CPDMA_BD_PASS_CRC     (1 << 26) /* Pass CRC */

#define CPDMA_BD_PKTLEN_MASK  0x000007FF



/* Polling interval: 10ms for responsiveness */
#define CPSW_POLL_INTERVAL_MS 10
#define CPSW_POLL_TICKS       (CPSW_POLL_INTERVAL_MS * 1000 / CONFIG_USEC_PER_TICK)

/* Configuration */
#define CPSW_TX_DESCRIPTORS   128
#define CPSW_RX_DESCRIPTORS   128
#define CPSW_BUF_SIZE         1536 /* Max Ethernet Frame + Alignment padding */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int am335x_cpsw_initialize(int devno);

#endif /* __ARCH_ARM_SRC_AM335X_AM335X_CPSW_H */