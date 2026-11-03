// SPDX-License-Identifier: GPL-2.0-only
/* Minimal modern DSA port of the TP-Link AR8236 driver. */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/mdio.h>
#include <linux/module.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/phylink.h>
#include <net/dsa.h>

#include "ar8236.h"

#define AR8236_MIB_DESC(_s, _o, _n) { .size = (_s), .offset = (_o), .name = (_n) }

static const struct ar8236_mib_desc ar8236_mibs[39] = {
	AR8236_MIB_DESC(1, AR8236_STATS_RXBROAD, "RxBroad"),
	AR8236_MIB_DESC(1, AR8236_STATS_RXPAUSE, "RxPause"),
	AR8236_MIB_DESC(1, AR8236_STATS_RXMULTI, "RxMulti"),
	AR8236_MIB_DESC(1, AR8236_STATS_RXFCSERR, "RxFcsErr"),
	AR8236_MIB_DESC(1, AR8236_STATS_RXALIGNERR, "RxAlignErr"),
	AR8236_MIB_DESC(1, AR8236_STATS_RXRUNT, "RxRunt"),
	AR8236_MIB_DESC(1, AR8236_STATS_RXFRAGMENT, "RxFragment"),
	AR8236_MIB_DESC(1, AR8236_STATS_RX64BYTE, "Rx64Byte"),
	AR8236_MIB_DESC(1, AR8236_STATS_RX128BYTE, "Rx128Byte"),
	AR8236_MIB_DESC(1, AR8236_STATS_RX256BYTE, "Rx256Byte"),
	AR8236_MIB_DESC(1, AR8236_STATS_RX512BYTE, "Rx512Byte"),
	AR8236_MIB_DESC(1, AR8236_STATS_RX1024BYTE, "Rx1024Byte"),
	AR8236_MIB_DESC(1, AR8236_STATS_RX1518BYTE, "Rx1518Byte"),
	AR8236_MIB_DESC(1, AR8236_STATS_RXMAXBYTE, "RxMaxByte"),
	AR8236_MIB_DESC(1, AR8236_STATS_RXTOOLONG, "RxTooLong"),
	AR8236_MIB_DESC(2, AR8236_STATS_RXGOODBYTE, "RxGoodByte"),
	AR8236_MIB_DESC(2, AR8236_STATS_RXBADBYTE, "RxBadByte"),
	AR8236_MIB_DESC(1, AR8236_STATS_RXOVERFLOW, "RxOverFlow"),
	AR8236_MIB_DESC(1, AR8236_STATS_FILTERED, "Filtered"),
	AR8236_MIB_DESC(1, AR8236_STATS_TXBROAD, "TxBroad"),
	AR8236_MIB_DESC(1, AR8236_STATS_TXPAUSE, "TxPause"),
	AR8236_MIB_DESC(1, AR8236_STATS_TXMULTI, "TxMulti"),
	AR8236_MIB_DESC(1, AR8236_STATS_TXUNDERRUN, "TxUnderRun"),
	AR8236_MIB_DESC(1, AR8236_STATS_TX64BYTE, "Tx64Byte"),
	AR8236_MIB_DESC(1, AR8236_STATS_TX128BYTE, "Tx128Byte"),
	AR8236_MIB_DESC(1, AR8236_STATS_TX256BYTE, "Tx256Byte"),
	AR8236_MIB_DESC(1, AR8236_STATS_TX512BYTE, "Tx512Byte"),
	AR8236_MIB_DESC(1, AR8236_STATS_TX1024BYTE, "Tx1024Byte"),
	AR8236_MIB_DESC(1, AR8236_STATS_TX1518BYTE, "Tx1518Byte"),
	AR8236_MIB_DESC(1, AR8236_STATS_TXMAXBYTE, "TxMaxByte"),
	AR8236_MIB_DESC(1, AR8236_STATS_TXOVERSIZE, "TxOverSize"),
	AR8236_MIB_DESC(2, AR8236_STATS_TXBYTE, "TxByte"),
	AR8236_MIB_DESC(1, AR8236_STATS_TXCOLLISION, "TxCollision"),
	AR8236_MIB_DESC(1, AR8236_STATS_TXABORTCOL, "TxAbortCol"),
	AR8236_MIB_DESC(1, AR8236_STATS_TXMULTICOL, "TxMultiCol"),
	AR8236_MIB_DESC(1, AR8236_STATS_TXSINGLECOL, "TxSingleCol"),
	AR8236_MIB_DESC(1, AR8236_STATS_TXEXCDEFER, "TxExcDefer"),
	AR8236_MIB_DESC(1, AR8236_STATS_TXDEFER, "TxDefer"),
	AR8236_MIB_DESC(1, AR8236_STATS_TXLATECOL, "TxLateCol"),
};

static int ar8236_reg_read(struct ar8236_priv *priv, u32 reg_addr)
{
	int word = (reg_addr & ~3) >> 1;
	int lo, hi;

	if (mdiobus_write_nested(priv->bus, 0x18, 0, (word >> 8) & 0x1ff))
		return -EIO;
	lo = mdiobus_read_nested(priv->bus, 0x10 | ((word >> 5) & 7), word & 0x1f);
	if (lo < 0)
		return lo;
	hi = mdiobus_read_nested(priv->bus, 0x10 | (((word + 1) >> 5) & 7), (word + 1) & 0x1f);
	if (hi < 0)
		return hi;
	return (hi << 16) | lo;
}

static int ar8236_reg_write(struct ar8236_priv *priv, u32 reg_addr, u32 value)
{
	int word = (reg_addr & ~3) >> 1;
	int ret;

	ret = mdiobus_write_nested(priv->bus, 0x18, 0, (word >> 8) & 0x1ff);
	if (ret)
		return ret;
	ret = mdiobus_write_nested(priv->bus, 0x10 | (((word + 1) >> 5) & 7), (word + 1) & 0x1f, value >> 16);
	if (ret)
		return ret;
	return mdiobus_write_nested(priv->bus, 0x10 | ((word >> 5) & 7), word & 0x1f, value & 0xffff);
}

static int ar8236_phy_read_raw(struct ar8236_priv *priv, int phy, int regnum)
{
	return mdiobus_read_nested(priv->bus, priv->sw_addr + phy, regnum);
}

static int ar8236_phy_write_raw(struct ar8236_priv *priv, int phy,
				int regnum, u16 val)
{
	return mdiobus_write_nested(priv->bus, priv->sw_addr + phy, regnum, val);
}

static int ar8236_phy_read(struct dsa_switch *ds, int port, int regnum)
{
	struct ar8236_priv *priv = ds->priv;
	int phy = port ? port - 1 : 0;

	return ar8236_phy_read_raw(priv, phy, regnum);
}

static int ar8236_phy_write(struct dsa_switch *ds, int port, int regnum, u16 val)
{
	struct ar8236_priv *priv = ds->priv;
	int phy = port ? port - 1 : 0;

	return ar8236_phy_write_raw(priv, phy, regnum, val);
}

static int ar8236_phy_poll_reset(struct dsa_switch *ds, int phy)
{
	struct ar8236_priv *priv = ds->priv;
	int ret, retries = 12;
	while (retries--) {
		msleep(50);
		ret = ar8236_phy_read_raw(priv, phy, MII_BMCR);
		if (ret < 0)
			return ret;
		if (!(ret & BMCR_RESET))
			return 0;
	}
	return -ETIMEDOUT;
}

static void ar8236_phylink_mac_config(struct phylink_config *config,
				       unsigned int mode,
				       const struct phylink_link_state *state)
{
}

static void ar8236_phylink_mac_link_down(struct phylink_config *config,
					 unsigned int mode,
					 phy_interface_t interface)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct ar8236_priv *priv = dp->ds->priv;
	int val;

	val = ar8236_reg_read(priv, AR8236_REG_PORT_STATUS(dp->index));
	if (val >= 0)
		ar8236_reg_write(priv, AR8236_REG_PORT_STATUS(dp->index),
				 val & ~(AR8236_PORT_STATUS_LINK_UP |
					  AR8236_PORT_STATUS_TXMAC |
					  AR8236_PORT_STATUS_RXMAC));
}

static void ar8236_phylink_mac_link_up(struct phylink_config *config,
					 struct phy_device *phy,
					 unsigned int mode,
					 phy_interface_t interface,
					 int speed, int duplex,
					 bool tx_pause, bool rx_pause)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct ar8236_priv *priv = dp->ds->priv;
	int val;

	val = ar8236_reg_read(priv, AR8236_REG_PORT_STATUS(dp->index));
	if (val < 0)
		return;

	val |= AR8236_PORT_STATUS_LINK_UP |
	       AR8236_PORT_STATUS_TXMAC |
	       AR8236_PORT_STATUS_RXMAC;
	val &= ~(AR8236_PORT_STATUS_TXFLOW |
		 AR8236_PORT_STATUS_RXFLOW |
		 AR8236_PORT_STATUS_TX_HALF_FLOW |
		 AR8236_PORT_STATUS_LINK_FLOW_EN);
	if (duplex == DUPLEX_FULL)
		val |= AR8236_PORT_STATUS_DUPLEX;
	if (speed == SPEED_100)
		val = (val & ~AR8236_PORT_STATUS_SPEED) |
		      AR8236_PORT_SPEED_100M;
	else if (speed == SPEED_10)
		val = (val & ~AR8236_PORT_STATUS_SPEED) |
		      AR8236_PORT_SPEED_10M;
	if (tx_pause)
		val |= AR8236_PORT_STATUS_TXFLOW;
	if (rx_pause)
		val |= AR8236_PORT_STATUS_RXFLOW;
	if (tx_pause || rx_pause)
		val |= AR8236_PORT_STATUS_LINK_FLOW_EN;

	ar8236_reg_write(priv, AR8236_REG_PORT_STATUS(dp->index), val);
}

static const struct phylink_mac_ops ar8236_phylink_mac_ops = {
	.mac_config = ar8236_phylink_mac_config,
	.mac_link_down = ar8236_phylink_mac_link_down,
	.mac_link_up = ar8236_phylink_mac_link_up,
};

static int ar8236_port_enable(struct dsa_switch *ds, int port, struct phy_device *phy)
{
	struct ar8236_priv *priv = ds->priv;
	int val, phy_id, bmcr;

	if (port < 0 || port >= AR8236_PORTS)
		return -EINVAL;
	val = ar8236_reg_read(priv, AR8236_REG_PORT_CTRL(port));
	if (val < 0)
		return val;
	val |= 4 << AR8236_PORT_CTRL_STATE_S;
	val = ar8236_reg_write(priv, AR8236_REG_PORT_CTRL(port), val);
	if (val || dsa_is_cpu_port(ds, port))
		return val;

	phy_id = port - AR8236_CPU_PORT - 1;
	bmcr = mdiobus_read_nested(priv->bus, priv->sw_addr + phy_id, MII_BMCR);
	if (bmcr < 0)
		return bmcr;
	bmcr &= ~BMCR_PDOWN;
	bmcr |= BMCR_ANENABLE | BMCR_RESET;
	ar8236_phy_write_raw(priv, phy_id, MII_ADVERTISE,
			      ADVERTISE_ALL | ADVERTISE_PAUSE_CAP |
			      ADVERTISE_PAUSE_ASYM);
	val = ar8236_phy_write_raw(priv, phy_id, MII_BMCR, bmcr);
	if (val)
		return val;
	return ar8236_phy_poll_reset(ds, phy_id);
}

static void ar8236_port_disable(struct dsa_switch *ds, int port)
{
	struct ar8236_priv *priv = ds->priv;
	int val;

	if (port < 0 || port >= AR8236_PORTS)
		return;
	val = ar8236_reg_read(priv, AR8236_REG_PORT_CTRL(port));
	if (val >= 0)
		ar8236_reg_write(priv, AR8236_REG_PORT_CTRL(port), val & ~AR8236_PORT_CTRL_STATE);
}

static int ar8236_setup(struct dsa_switch *ds)
{
	struct ar8236_priv *priv = ds->priv;
	unsigned int user_mask = dsa_user_ports(ds);
	int val, i;

	dev_info(priv->dev, "Setup driver for AR8236\n");
	ar8236_reg_write(priv, AR8236_REG_CTRL, AR8236_CTRL_RESET);
	for (i = 0; i < 100; i++) {
		udelay(10);
		val = ar8236_reg_read(priv, AR8236_REG_CTRL);
		if (val >= 0 && !(val & AR8236_CTRL_RESET))
			break;
	}
	if (i == 100)
		return -ETIMEDOUT;

	for (i = 0; i < 5; i++) {
		ar8236_phy_write_raw(priv, i, MII_ADVERTISE,
				      ADVERTISE_ALL | ADVERTISE_PAUSE_CAP |
				      ADVERTISE_PAUSE_ASYM);
		ar8236_phy_write_raw(priv, i, MII_BMCR,
				      BMCR_RESET | BMCR_ANENABLE);
		ar8236_phy_poll_reset(ds, i);
	}
	ar8236_reg_write(priv, 0x2c, 0x7e3f003f);
	ar8236_reg_write(priv, 0x4, 0x500);
	val = ar8236_reg_read(priv, AR8236_REG_MIB_FUNC);
	ar8236_reg_write(priv, AR8236_REG_MIB_FUNC, val | AR8236_MIB_EN);
	val = ar8236_reg_read(priv, AR8236_REG_CPU_PORT);
	ar8236_reg_write(priv, AR8236_REG_CPU_PORT, val | AR8236_CPU_PORT_EN);
	ar8236_reg_write(priv, AR8236_REG_FLOOD_MASK,
		BIT(0) << AR8236_REG_FLOOD_MASK_IGMP_DP_S |
		BIT(0) << AR8236_REG_FLOOD_MASK_BC_DP_S |
		BIT(0) << AR8236_REG_FLOOD_MASK_MC_DP_S |
		BIT(0) << AR8236_REG_FLOOD_MASK_UC_DP_S);

	for (i = 0; i < AR8236_PORTS; i++) {
		val = ar8236_reg_read(priv, AR8236_REG_PORT_CTRL(i));
		ar8236_reg_write(priv, AR8236_REG_PORT_CTRL(i), val | AR8236_PORT_CTRL_LEARN | (4 << AR8236_PORT_CTRL_STATE_S));
		if (i == AR8236_CPU_PORT) {
			val = ar8236_reg_read(priv, AR8236_REG_PORT_CTRL(i));
			ar8236_reg_write(priv, AR8236_REG_PORT_CTRL(i), val | AR8236_PORT_CTRL_HEADER);
			val = ar8236_reg_read(priv, AR8236_REG_PORT_VLAN2(i));
			ar8236_reg_write(priv, AR8236_REG_PORT_VLAN2(i), val | (user_mask << AR8236_PORT_VLAN2_MEMBER_S));
			ar8236_reg_write(priv, AR8236_REG_PORT_STATUS(i), AR8236_PORT_STATUS_LINK_UP | AR8236_PORT_SPEED_100M | AR8236_PORT_STATUS_TXMAC | AR8236_PORT_STATUS_RXMAC | AR8236_PORT_STATUS_DUPLEX);
		} else {
			val = ar8236_reg_read(priv, AR8236_REG_PORT_VLAN2(i));
			ar8236_reg_write(priv, AR8236_REG_PORT_VLAN2(i), val | BIT(AR8236_CPU_PORT) << AR8236_PORT_VLAN2_MEMBER_S);
			ar8236_reg_write(priv, AR8236_REG_PORT_STATUS(i), AR8236_PORT_STATUS_LINK_AUTO | AR8236_PORT_STATUS_LINK_FLOW_EN | AR8236_PORT_STATUS_TX_HALF_FLOW);
		}
	}
	return 0;
}

static enum dsa_tag_protocol ar8236_get_tag_protocol(struct dsa_switch *ds, int port, enum dsa_tag_protocol mprot)
{
	return DSA_TAG_PROTO_QCA;
}

static void ar8236_phylink_get_caps(struct dsa_switch *ds, int port, struct phylink_config *config)
{
	config->mac_capabilities = MAC_10 | MAC_100 | MAC_ASYM_PAUSE | MAC_SYM_PAUSE;
	if (dsa_is_cpu_port(ds, port))
		__set_bit(PHY_INTERFACE_MODE_RGMII, config->supported_interfaces);
	else {
		__set_bit(PHY_INTERFACE_MODE_INTERNAL, config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_GMII, config->supported_interfaces);
	}
}

static const struct dsa_switch_ops ar8236_switch_ops = {
	.get_tag_protocol = ar8236_get_tag_protocol,
	.setup = ar8236_setup,
	.phy_read = ar8236_phy_read,
	.phy_write = ar8236_phy_write,
	.port_enable = ar8236_port_enable,
	.port_disable = ar8236_port_disable,
	.phylink_get_caps = ar8236_phylink_get_caps,
};

static int ar8236_probe(struct mdio_device *mdiodev)
{
	struct ar8236_priv *priv;
	struct dsa_switch *ds;
	int id1, id2, id;

	id1 = mdiobus_read_nested(mdiodev->bus, mdiodev->addr, MII_PHYSID1);
	id2 = mdiobus_read_nested(mdiodev->bus, mdiodev->addr, MII_PHYSID2);
	if (id1 < 0 || id2 < 0)
		return -ENODEV;
	id = (id1 << 16) | id2;
	if (id != AR8236_PHY_ID)
		return -ENODEV;

	priv = devm_kzalloc(&mdiodev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->dev = &mdiodev->dev;
	priv->bus = mdiodev->bus;
	priv->sw_addr = mdiodev->addr;

	ds = devm_kzalloc(&mdiodev->dev, sizeof(*ds), GFP_KERNEL);
	if (!ds)
		return -ENOMEM;
	ds->dev = &mdiodev->dev;
	ds->num_ports = AR8236_PORTS;
	ds->priv = priv;
	ds->ops = &ar8236_switch_ops;
	ds->phylink_mac_ops = &ar8236_phylink_mac_ops;
	priv->ds = ds;
	dev_set_drvdata(&mdiodev->dev, ds);
	dev_info(&mdiodev->dev, "Atheros AR8236 switch ID 0x%08x\n", id);
	return dsa_register_switch(ds);
}

static void ar8236_remove(struct mdio_device *mdiodev)
{
	struct dsa_switch *ds = dev_get_drvdata(&mdiodev->dev);
	if (ds)
		dsa_unregister_switch(ds);
}

static const struct of_device_id ar8236_of_match[] = {
	{ .compatible = "qca,ar8236" },
	{ },
};
MODULE_DEVICE_TABLE(of, ar8236_of_match);

static struct mdio_driver ar8236_mdio_driver = {
	.probe = ar8236_probe,
	.remove = ar8236_remove,
	.mdiodrv.driver = {
		.name = "ar8236",
		.of_match_table = ar8236_of_match,
	},
};
mdio_module_driver(ar8236_mdio_driver);

MODULE_DESCRIPTION("Atheros AR8236 DSA switch driver");
MODULE_LICENSE("GPL");
