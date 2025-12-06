/*
 * arch/arm/src/am335x/hardware/am335x_cpsw.h
 *
 * Corrected definitions for AM335x CPSW registers and values.
 */

#ifndef __ARCH_ARM_SRC_AM335X_HARDWARE_AM335X_CPSW_H
#define __ARCH_ARM_SRC_AM335X_HARDWARE_AM335X_CPSW_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Control Module Registers */
#define AM335X_CTRL_BASE      0x44E10000
#define AM335X_GMII_SEL       (AM335X_CTRL_BASE + 0x650)

/* PRCM Registers */
#define AM335X_CM_PER_BASE      0x44E00000
#define AM335X_CM_PER_CPSW_CLK  (AM335X_CM_PER_BASE + 0x144)
#define AM335X_CM_PER_CPGMAC0_CLKCTRL (AM335X_CM_PER_BASE + 0x14)

/* CPSW and MDIO Base Addresses */
#define AM335X_CPSW_BASE          0x4A100000
#define AM335X_MDIO_BASE          0x4A101000

/* MDIO Register Offsets (from MDIO_BASE) */
#define AM335X_MDIO_VER           (AM335X_MDIO_BASE + 0x00)
#define AM335X_MDIO_CTRL          (AM335X_MDIO_BASE + 0x04)
#define AM335X_MDIO_ALIVE         (AM335X_MDIO_BASE + 0x08)
#define AM335X_MDIO_LINK          (AM335X_MDIO_BASE + 0x0C)
#define AM335X_MDIO_LINKINTRAW    (AM335X_MDIO_BASE + 0x10)
#define AM335X_MDIO_LINKINTMASKED (AM335X_MDIO_BASE + 0x14)
#define AM335X_MDIO_USERACCESS0   (AM335X_MDIO_BASE + 0x80)
#define AM335X_MDIO_USERPHYSEL0   (AM335X_MDIO_BASE + 0x84)

/* MDIO Control Register Bitfields */
#define MDIO_CONTROL_ENABLE       (1 << 30)

/* MDIO_USERACCESS0 Bit Fields */
#define MDIO_USERACCESS_GO             (1 << 31)
#define MDIO_USERACCESS_WRITE          (1 << 30) /* 0=Read, 1=Write */
#define MDIO_USERACCESS_READ           (0)
#define MDIO_USERACCESS_ACK            (1 << 29) /* 1=Acknowledge received */
#define MDIO_USERACCESS_REGADR_SHIFT   (21)
#define MDIO_USERACCESS_PHYADR_SHIFT   (16)
#define MDIO_USERACCESS_DATA_MASK      (0xFFFF)

/* Standard MII PHY Registers */
#define MII_BMCR                  0x00
#define MII_BMSR                  0x01
#define MII_PHYID1                0x02
#define MII_PHYID2                0x03
#define MII_ANAR                  0x04
#define MII_ADVERTISE             0x04

/* EOI Codes (Write these to EOI_VECTOR to ack interrupts) */
/* CRITICAL FIX: Must be the actual IRQ number attached in am335x_cpsw.c */
#define CPSW_EOI_RX_PULSE         41
#define CPSW_EOI_TX_PULSE         42

/* CPSW Main Control Register */
#define AM335X_CPSW_CONTROL_OFFSET       0x04

/* CPSW Subsystem Register Offsets (from CPSW_BASE) */
/* CRITICAL FIX: 0x00 is IDVER, 0x08 is SOFT_RESET */
#define AM335X_CPSW_SS_SOFT_RESET_OFFSET 0x08
#define AM335X_CPSW_SS_STAT_PORT_EN      0x0C
#define AM335X_CPSW_TS_CFG_OFFSET        0x011C

/* CRITICAL FIX: Correct Wrapper Offsets (Base 0x1200) */
#define AM335X_CPSW_C0_RX_EN_OFFSET      0x1214
#define AM335X_CPSW_C0_TX_EN_OFFSET      0x1218
/* CPSW Wrapper Channel 0 Misc Enable Register */
#define AM335X_CPSW_C0_MISC_EN_OFFSET 0x1210

/* CPSW Sliver Register Offsets (from CPSW_BASE) */
/* Correct: SLIVER 1 (Port 1) MAC CONTROL is at 0xD84 */
#define AM335X_CPSW_SL_MACCONTROL_OFFSET 0x0D84

/* CPSW ALE Register Offsets (from CPSW_BASE) */
#define AM335X_CPSW_ALE_CONTROL_OFFSET   0x0D08
#define AM335X_CPSW_ALE_PORTCTL0         0x0D40
#define AM335X_CPSW_ALE_PORTCTL1         0x0D44
#define AM335X_CPSW_ALE_UNKNOWN_VLAN_OFFSET 0x0DBC
#define ALE_TBLW_OFFSET                  0x0D50 /* Table Word 2 */
#define ALE_TBLW1_OFFSET                 0x0D54 /* Table Word 1 */
#define ALE_TBLW0_OFFSET                 0x0D58 /* Table Word 0 */
#define ALE_CTL_OFFSET                   0x0D5C /* Table Control */

/* CRITICAL FIX: Host Port (P0) Control Register */
#define AM335X_CPSW_PORT_P0_CONTROL_OFFSET 0x0100 

/* CPSW Port Register Offsets (from CPSW_BASE) */
#define AM335X_CPSW_PORT_P0_TX_PRI_MAP_OFFSET 0x0118

/* CPSW CPDMA Register Offsets (from CPSW_BASE) */
#define AM335X_CPSW_CPDMA_TX_CONTROL_OFFSET 0x0804
#define AM335X_CPSW_CPDMA_RX_CONTROL_OFFSET 0x0814
#define AM335X_CPSW_CPDMA_INTSTAT         0x08A4
#define AM335X_CPSW_CPDMA_TX_INTMASK_SET  0x0888
#define AM335X_CPSW_CPDMA_RX_INTMASK_SET  0x08A8
#define AM335X_CPSW_CPDMA_TX_HDP_0_OFFSET 0x0A00
#define AM335X_CPSW_CPDMA_RX_HDP_0_OFFSET 0x0A20
#define CPSW_CPDMA_RX_CP_0                0x0A24 /* RX Completion Pointer */
#define CPSW_CPDMA_TX_CP_0                0x0A04 /* TX Completion Pointer */
#define CPSW_CPDMA_EOI_VECTOR             0x0A28 /* EOI Vector */

/* CPSW Statistics Register Offsets (from CPSW_BASE) */
#define AM335X_CPSW_STATS_RXGOODFRAMES    0x0900

#endif /* __ARCH_ARM_SRC_AM335X_HARDWARE_AM335X_CPSW_H */
