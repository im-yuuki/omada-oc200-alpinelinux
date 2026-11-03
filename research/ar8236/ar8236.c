#include <linux/version.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/if_bridge.h>
#include <linux/jiffies.h>
#include <linux/list.h>
#include <linux/mdio.h>
#include <linux/module.h>
#include <linux/of_mdio.h>
#include <linux/netdevice.h>
#include <linux/gpio/consumer.h>
#include <linux/phy.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <net/dsa.h>
#include <net/switchdev.h>
#include "ar8236.h"

#define AR8236_MIB_DESC(_s , _o, _n)	\
	{			\
		.size = (_s),	\
		.offset = (_o),	\
		.name = (_n),	\
	}

static struct ar8236_priv *ar8236_priv_date = NULL;
static struct proc_dir_entry *ar8236_proc_dir = NULL;

const struct ar8236_mib_desc ar8236_mibs[39] = {
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

static int ar8236_reg_read(struct ar8236_priv *ps, uint32_t reg_addr)
{
	int reg_word_addr;
	int phy_addr, reg_val;
	unsigned short phy_val;
	unsigned short tmp_val;
	unsigned char  phy_reg;

	/* change reg_addr to 16-bit word address, 32-bit aligned */
	reg_word_addr = (reg_addr & 0xfffffffc) >> 1;

	/* configure register high address */
	phy_addr = 0x18;
	phy_reg = 0x0;
	phy_val = (unsigned short) ((reg_word_addr >> 8) & 0x1ff);  /* bit16-8 of reg address */
	mdiobus_write_nested(ps->bus, phy_addr, phy_reg, phy_val);

	/*
	 * For some registers such as MIBs, since it is read/clear, we should
	 * read the lower 16-bit register then the higher one
	 */

	/* read register in lower address */
	phy_addr = 0x10 | ((reg_word_addr >> 5) & 0x7); /* bit7-5 of reg address */
	phy_reg = (unsigned char) (reg_word_addr & 0x1f);   /* bit4-0 of reg address */
	phy_val = mdiobus_read_nested(ps->bus, phy_addr, phy_reg);

	/* read register in higher address */
	reg_word_addr++;
	phy_addr = 0x10 | ((reg_word_addr >> 5) & 0x7); /* bit7-5 of reg address */
	phy_reg = (unsigned char) (reg_word_addr & 0x1f);   /* bit4-0 of reg address */
	tmp_val = mdiobus_read_nested(ps->bus, phy_addr, phy_reg);
	reg_val = (tmp_val << 16 | phy_val);

	return reg_val;
}

static int ar8236_reg_write(struct ar8236_priv *ps, uint32_t reg_addr, uint32_t reg_val)
{
	int reg_word_addr;
	int phy_addr;
	unsigned short phy_val;
	unsigned char phy_reg;

	/* change reg_addr to 16-bit word address, 32-bit aligned */
	reg_word_addr = (reg_addr & 0xfffffffc) >> 1;

	/* configure register high address */
	phy_addr = 0x18;
	phy_reg = 0x0;
	phy_val = (unsigned short) ((reg_word_addr >> 8) & 0x1ff);  /* bit16-8 of reg address */
	mdiobus_write_nested(ps->bus, phy_addr, phy_reg, phy_val);

	/*
	 * For some registers such as ARL and VLAN, since they include BUSY bit
	 * in lower address, we should write the higher 16-bit register then the
	 * lower one
	 */

	/* read register in higher address */
	reg_word_addr++;
	phy_addr = 0x10 | ((reg_word_addr >> 5) & 0x7); /* bit7-5 of reg address */
	phy_reg = (unsigned char) (reg_word_addr & 0x1f);   /* bit4-0 of reg address */
	phy_val = (unsigned short) ((reg_val >> 16) & 0xffff);
	mdiobus_write_nested(ps->bus, phy_addr, phy_reg, phy_val);

	/* write register in lower address */
	reg_word_addr--;
	phy_addr = 0x10 | ((reg_word_addr >> 5) & 0x7); /* bit7-5 of reg address */
	phy_reg = (unsigned char) (reg_word_addr & 0x1f);   /* bit4-0 of reg address */
	phy_val = (unsigned short) (reg_val & 0xffff);
	mdiobus_write_nested(ps->bus, phy_addr, phy_reg, phy_val);

	return 0;
}

static int ar8236_phy_read(struct dsa_switch *ds, int port, int reg)
{
	struct ar8236_priv *priv = ds_to_priv(ds);

	return mdiobus_read_nested(priv->bus, priv->sw_addr + port, reg);
}

static int ar8236_phy_write(struct dsa_switch *ds, int port, int reg, u16 val)
{
	struct ar8236_priv *priv = ds_to_priv(ds);

	return mdiobus_write_nested(priv->bus, priv->sw_addr + port, reg, val);
}

static int ar8236_port_enable(struct dsa_switch *ds, int port,
		  struct phy_device *phy)
{
	int val = 0;
	struct ar8236_priv *priv = ds_to_priv(ds);

	/* Port 0-5 is valid */
	if ((port < 0) || (port > 5))
		return -1;

	val = ar8236_reg_read(priv, AR8236_REG_PORT_CTRL(port));
	val |= (4 << AR8236_PORT_CTRL_STATE_S);
	ar8236_reg_write(priv, AR8236_REG_PORT_CTRL(port), val);

	/* not cpu port, also enable phy of this port */
	if (port != AR8236_CPU_PORT)
	{
		int phy_id = port -1; /* cpu-port0, phy0-port1, phy1-port2, etc ... */
		int phy_val;

		phy_val = ar8236_phy_read(ds, phy_id, MII_PHYSID1);
		phy_val &= ~BMCR_PDOWN; /* 0 is power up */
		ar8236_phy_write(ds, phy_id, MII_BMCR, BMCR_RESET | BMCR_ANENABLE);
	}

	return 0;
}

static void ar8236_port_disable(struct dsa_switch *ds, int port,
		   struct phy_device *phy)
{
	int val = 0;
	struct ar8236_priv *priv = ds_to_priv(ds);

	/* Port 0-5 is valid */
	if ((port < 0) || (port > 5))
		return;

	val = ar8236_reg_read(priv, AR8236_REG_PORT_CTRL(port));
	val &= ~AR8236_PORT_CTRL_STATE;
	ar8236_reg_write(priv, AR8236_REG_PORT_CTRL(port), val);

	/* not cpu port, also disable phy of this port */
	if (port != AR8236_CPU_PORT)
	{
		int phy_id = port -1; /* cpu-port0, phy0-port1, phy1-port2, etc ... */
		int phy_val;

		phy_val = ar8236_phy_read(ds, phy_id, MII_PHYSID1);
		phy_val |= BMCR_PDOWN; /* 1 is power down */
		ar8236_phy_write(ds, phy_id, MII_BMCR, BMCR_RESET | BMCR_ANENABLE);
	}

	return;
}

static const char *ar8236_get_name(struct mii_bus *bus, int sw_addr)
{
	int phy_id;
	int phy_reg;

	phy_reg = mdiobus_read_nested(bus, sw_addr, MII_PHYSID1);
	if (phy_reg < 0)
		return NULL;
	
	phy_id = (phy_reg & 0xffff) << 16;
	
	phy_reg = mdiobus_read_nested(bus, sw_addr, MII_PHYSID2);
	if (phy_reg < 0)
		return NULL;

	phy_id |= (phy_reg & 0xffff);

	dev_info(&bus->dev, "Attach switch ID 0x%08x\n", phy_id);
	if (phy_id == AR8236_PHY_ID)
		return "Atheros AR8236";

	/* not our switch */
	return NULL;
}

static int ar8236_set_addr(struct dsa_switch *ds, u8 *addr)
{
	return 0;
}

/* inspired by phy_poll_reset in drivers/net/phy/phy_device.c */
static int ar8236_phy_poll_reset(struct dsa_switch *ds, int phy)
{
	/* Poll until the reset bit clears (50ms per retry == 0.6 sec) */
	unsigned int retries = 12;
	int ret;

	do {
		msleep(50);
		ret = ar8236_phy_read(ds, phy, MII_BMCR);
		if (ret < 0)
			return ret;
	} while (ret & BMCR_RESET && --retries);
	if (ret & BMCR_RESET)
	{
		struct ar8236_priv *priv = ds_to_priv(ds);
		dev_err(&priv->bus->dev, "phy poll reset error for phy%d\n", phy);
		return -ETIMEDOUT;
	}

	msleep(1);
	return 0;
}

static int ar8236_setup(struct dsa_switch *ds)
{
	int val = 0;
	int i = 0;
	struct ar8236_priv *priv = ds_to_priv(ds);

	priv->ds = ds;

	dev_info(&priv->bus->dev, "Setup driver for AR8236\n");

	/* Reset the switch before initialization */
	ar8236_reg_write(priv, AR8236_REG_CTRL, AR8236_CTRL_RESET);
	do {
		udelay(10);
		val = ar8236_reg_read(priv, AR8236_REG_CTRL);
	} while (val & AR8236_CTRL_RESET);

	/* phy initialize */
	for (i = 0; i < 5; i++) {
		ar8236_phy_write(ds, i, MII_ADVERTISE,
			ADVERTISE_ALL | ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM);

		ar8236_phy_write(ds, i, MII_BMCR, BMCR_RESET | BMCR_ANENABLE);
		ar8236_phy_poll_reset(ds, i);
	}
	
	/* config MII PHY mode */
	ar8236_reg_write(priv, 0x2c, 0x7e3f003f);
	ar8236_reg_write(priv, 0x4, 0x500);
	
	/* Enable MIB counters */
	val = ar8236_reg_read(priv, AR8236_REG_MIB_FUNC);
	val |= AR8236_MIB_EN;
	ar8236_reg_write(priv, AR8236_REG_MIB_FUNC, val);

	/* Enable CPU Port */
	val = ar8236_reg_read(priv, AR8236_REG_CPU_PORT);
	val |= AR8236_CPU_PORT_EN;
	ar8236_reg_write(priv, AR8236_REG_CPU_PORT, val);

	/* Forward all unknown frames to CPU port for Linux processing */
	ar8236_reg_write(priv, AR8236_REG_FLOOD_MASK,
		    BIT(0) << AR8236_REG_FLOOD_MASK_IGMP_DP_S |
		    BIT(0) << AR8236_REG_FLOOD_MASK_BC_DP_S |
		    BIT(0) << AR8236_REG_FLOOD_MASK_MC_DP_S |
		    BIT(0) << AR8236_REG_FLOOD_MASK_UC_DP_S);

	for (i = 0; i < 6; i++) {
		/* Enable port learning and tx */
		val = ar8236_reg_read(priv, AR8236_REG_PORT_CTRL(i));
		val |= AR8236_PORT_CTRL_LEARN | (4 << AR8236_PORT_CTRL_STATE_S);
		ar8236_reg_write(priv, AR8236_REG_PORT_CTRL(i), val);
		
		if (i == AR8236_CPU_PORT)
		{
			/* Enable QCA header mode on the cpu port */
			val = ar8236_reg_read(priv, AR8236_REG_PORT_CTRL(i));
			val |= AR8236_PORT_CTRL_HEADER;
			ar8236_reg_write(priv, AR8236_REG_PORT_CTRL(i), val);

			/* CPU port gets connected to all user ports of the switch */
			val = ar8236_reg_read(priv, AR8236_REG_PORT_VLAN2(i));
			val |= (ds->enabled_port_mask << AR8236_PORT_VLAN2_MEMBER_S);
			ar8236_reg_write(priv, AR8236_REG_PORT_VLAN2(i), val);
			
			/* CPU port is forced link-up, full-duplex and 100MB speed */
			ar8236_reg_write(priv, AR8236_REG_PORT_STATUS(i),
				AR8236_PORT_STATUS_LINK_UP |
				AR8236_PORT_SPEED_100M |
				AR8236_PORT_STATUS_TXMAC |
				AR8236_PORT_STATUS_RXMAC |
				AR8236_PORT_STATUS_DUPLEX);
		}
		else
		{
			/* Invividual user ports get connected to CPU port only */
			val = ar8236_reg_read(priv, AR8236_REG_PORT_VLAN2(i));
			val |= (BIT(AR8236_CPU_PORT) << AR8236_PORT_VLAN2_MEMBER_S);
			ar8236_reg_write(priv, AR8236_REG_PORT_VLAN2(i), val);

			ar8236_reg_write(priv, AR8236_REG_PORT_STATUS(i),
				AR8236_PORT_STATUS_LINK_AUTO | AR8236_PORT_STATUS_LINK_FLOW_EN | AR8236_PORT_STATUS_TX_HALF_FLOW);
		}
	}

	ar8236_priv_date = priv;

	return 0;
}

static const char *ar8236_drv_probe(struct device *dsa_dev,
				       struct device *host_dev, int sw_addr,
				       void **_priv)
{
	struct mii_bus *bus = dsa_host_dev_to_mii_bus(host_dev);
	struct ar8236_priv *priv;
	const char *name;

	name = ar8236_get_name(bus, sw_addr);
	if (name) {
		dev_info(&bus->dev, "%s sw_addr is %d\n", name, sw_addr);
		priv = devm_kzalloc(dsa_dev, sizeof(*priv), GFP_KERNEL);
		if (!priv)
			return NULL;
		*_priv = priv;
		priv->bus = bus;
		priv->sw_addr = sw_addr;

		priv->mib_stats = kzalloc(AR8236_PORTS * ARRAY_SIZE(ar8236_mibs) * sizeof(*priv->mib_stats), GFP_KERNEL);
		if (!priv->mib_stats)
			return NULL;
	}

	return name;
}

static struct dsa_switch_driver ar8236_switch_ops = {
	.tag_protocol	= DSA_TAG_PROTO_QCA,
	.probe		= ar8236_drv_probe,
	.setup		= ar8236_setup,
	.set_addr	= ar8236_set_addr,
	.phy_read	= ar8236_phy_read,
	.phy_write	= ar8236_phy_write,
	.port_enable	= ar8236_port_enable,
	.port_disable	= ar8236_port_disable,
};

static int ar8236_proc_stats_show(struct seq_file *m, void *v, int clear)
{
	unsigned int base;
	static struct ar8236_priv *priv;
	u64 *mib_stats;
	int port, index;

	priv = ar8236_priv_date;

	for (port = 0; port < AR8236_PORTS; port ++) {
		base = 0x20000 + 0x100 * port; /* mib base * mib length per port */
		mib_stats = &priv->mib_stats[port * ARRAY_SIZE(ar8236_mibs)];

		for (index = 0; index < ARRAY_SIZE(ar8236_mibs); index++) {
			const struct ar8236_mib_desc *mib;
			u64 t;

			mib = &ar8236_mibs[index];
			t = (u32)ar8236_reg_read(priv, base + mib->offset);
			if (mib->size == 2) {
				u64 hi;

				hi = (u32)ar8236_reg_read(priv, base + mib->offset + 4);
				t |= hi << 32;
			}

			if (clear)
				mib_stats[index] = 0;
			else
				mib_stats[index] += t;
		}
	}

	/* show it */
	/* header */
	seq_printf(m, "%16s ", "port");
	for (port = 0; port < AR8236_PORTS; port ++) {
		seq_printf(m, "%20llu", (u64)port);
	}
	seq_printf(m, "\n\n");

	/* context */
	for (index = 0; index < ARRAY_SIZE(ar8236_mibs); index++) {
		const struct ar8236_mib_desc *mib;

		mib = &ar8236_mibs[index];
		seq_printf(m, "%16s ", mib->name);
		for (port = 0; port < AR8236_PORTS; port ++) {
			mib_stats = &priv->mib_stats[port * ARRAY_SIZE(ar8236_mibs)];
			seq_printf(m, "%20llu", mib_stats[index]);
		}
		seq_printf(m, "\n");
	}
	return 0;
}

static int ar8236_proc_stats_show_clear(struct seq_file *m, void *v)
{
	return ar8236_proc_stats_show(m, v, 1);
}

static int ar8236_proc_stats_show_noclear(struct seq_file *m, void *v)
{
	return ar8236_proc_stats_show(m, v, 0);
}
static int ar8236_proc_stats_clear_open(struct inode *inode, struct file *file)
{
	return single_open(file, ar8236_proc_stats_show_clear, NULL);
}

static int ar8236_proc_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, ar8236_proc_stats_show_noclear, NULL);
}

static const struct file_operations ar8236_proc_stats_clear = {
	.open		= ar8236_proc_stats_clear_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.owner		= THIS_MODULE,
};

static const struct file_operations ar8236_proc_stats = {
	.open		= ar8236_proc_stats_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.owner		= THIS_MODULE,
};

static int ar8236_proc_fdb_show(struct seq_file *m, void *v)
{
	static struct ar8236_priv *priv;
	struct ar8236_arl_entry *a;
	u32 val0, val1, val2;
	int timeout = 20;
	int i, j;

	priv = ar8236_priv_date;

	ar8236_reg_write(priv, AR8236_REG_ATU_FUNC1, 0x0);
	ar8236_reg_write(priv, AR8236_REG_ATU_FUNC2, 0x0);
	ar8236_reg_write(priv, AR8236_REG_ATU_FUNC0, AR8236_ATU_BUSY|AR8236_ATU_OP_GET_NEXT);

	for(i = 0; i < AR8236_ARL_ENTRY; ++i) {
		timeout = 20;
		while ((ar8236_reg_read(priv, AR8236_REG_ATU_FUNC0) & AR8236_ATU_BUSY) && --timeout)
			udelay(10);

		if (timeout <= 0)
		{
			dev_info(&priv->bus->dev, "atu get entry timeout\n");
			return -1;
		}

		a = &priv->arl_table[i];

		val0 = ar8236_reg_read(priv, AR8236_REG_ATU_FUNC0);
		val1 = ar8236_reg_read(priv, AR8236_REG_ATU_FUNC1);
		val2 = ar8236_reg_read(priv, AR8236_REG_ATU_FUNC2);

		if ((val1 == 0) && (val2 == 0))
		{
			/* mean search finish */
			break;
		}

		a->port = (val2 & AR8236_ATU_PORTS) >> AR8236_ATU_PORTS_S;
		a->status = (val2 & AR8236_ATU_STATUS) >> AR8236_ATU_STATUS_S;
		a->mac[0] = (val0 & AR8236_ATU_ADDR5) >> AR8236_ATU_ADDR5_S;
		a->mac[1] = (val0 & AR8236_ATU_ADDR4) >> AR8236_ATU_ADDR4_S;
		a->mac[2] = (val1 & AR8236_ATU_ADDR3) >> AR8236_ATU_ADDR3_S;
		a->mac[3] = (val1 & AR8236_ATU_ADDR2) >> AR8236_ATU_ADDR2_S;
		a->mac[4] = (val1 & AR8236_ATU_ADDR1) >> AR8236_ATU_ADDR1_S;
		a->mac[5] = (val1 & AR8236_ATU_ADDR0) >> AR8236_ATU_ADDR0_S;

		/* avoid duplicates
		 * ARL table can include multiple valid entries
		 * per MAC, just with differing status codes

		for (j = 0; j < i; ++j) {
			a1 = &priv->arl_table[j];
			if (a->port == a1->port && !memcmp(a->mac, a1->mac, sizeof(a->mac)))
				goto duplicate;
		}*/

		/* get next one */
		val0 = ar8236_reg_read(priv, AR8236_REG_ATU_FUNC0);
		val0 |= AR8236_ATU_BUSY|AR8236_ATU_OP_GET_NEXT;
		ar8236_reg_write(priv, AR8236_REG_ATU_FUNC0, val0);
	}

	seq_printf(m, "%6s  %17s  %12s  %7s\n", "index", "mac_address", "dest_ports", "status");
	for (j = 0; j < i; ++j) {
		a = &priv->arl_table[j];

		seq_printf(m, "%6d  %02x:%02x:%02x:%02x:%02x:%02x  %12d  %7d\n", j, 
					a->mac[5], a->mac[4], a->mac[3], a->mac[2], a->mac[1], a->mac[0],
					a->port, a->status);
	}

	return 0;
}

static int ar8236_proc_fdb_open(struct inode *inode, struct file *file)
{
	return single_open(file, ar8236_proc_fdb_show, NULL);
}

static const struct file_operations ar8236_proc_fdb = {
	.open		= ar8236_proc_fdb_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.owner		= THIS_MODULE,
};

static int ar8236_proc_reg_read_show(struct seq_file *m, void *v)
{
	seq_printf(m, "use 'echo reg_addr > reg_read' to read register\n");
	return 0;
}

static int ar8236_proc_reg_read_open(struct inode *inode, struct file *file)
{
	return single_open(file, ar8236_proc_reg_read_show, NULL);
}
static ssize_t ar8236_proc_reg_read_write(struct file *f, const char __user *buf, size_t count, loff_t *ppos)
{
	uint32_t reg_addr;
	char addr_buf[64] = {0};
	static struct ar8236_priv *priv;

	priv = ar8236_priv_date;
	
	if (copy_from_user(addr_buf, buf, (count > 64)?64:count))
		return -EFAULT;

	reg_addr = simple_strtoul(addr_buf, NULL, 16);
	dev_info(&priv->bus->dev, "register 0x%X value = 0x%08X\n", reg_addr, ar8236_reg_read(priv, reg_addr));

	return count;
}

static const struct file_operations ar8236_proc_reg_read = {
	.open		= ar8236_proc_reg_read_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= ar8236_proc_reg_read_write,
	.owner		= THIS_MODULE,
};

static int ar8236_proc_reg_write_show(struct seq_file *m, void *v)
{
	seq_printf(m, "use 'echo reg_addr reg_value > reg_write' to write register\n");
	return 0;
}

static int ar8236_proc_reg_write_open(struct inode *inode, struct file *file)
{
	return single_open(file, ar8236_proc_reg_write_show, NULL);
}

static ssize_t ar8236_proc_reg_write_write(struct file *f, const char __user *buf, size_t count, loff_t *ppos)
{
	uint32_t reg_addr, reg_val;
	char addr_buf[64] = {0};
	char *p = NULL;
	static struct ar8236_priv *priv;

	priv = ar8236_priv_date;
	
	if (copy_from_user(addr_buf, buf, (count > 64)?64:count))
		return -EFAULT;

	p = addr_buf;
	while (*p != '\0')
	{
		if (*p == ' ')
		{
			*p = '\0';
			p++;
			break;
		}

		p++;
	}

	if (*p == '\0')
		return -EFAULT;
		
	reg_addr = simple_strtoul(addr_buf, NULL, 16);
	reg_val = simple_strtoul(p, NULL, 16);

	ar8236_reg_write(priv, reg_addr, reg_val);
	
	dev_info(&priv->bus->dev, "register 0x%X value = 0x%08X\n", reg_addr, ar8236_reg_read(priv, reg_addr));

	return count;
}

static const struct file_operations ar8236_proc_reg_write = {
	.open		= ar8236_proc_reg_write_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= ar8236_proc_reg_write_write,
	.owner		= THIS_MODULE,
};

#ifdef CONFIG_PROC_FS
static int ar8236_proc_init(void)
{
	struct proc_dir_entry *entry;

	ar8236_proc_dir = proc_mkdir("ar8236", NULL);
	if (!ar8236_proc_dir)
		goto err_dir;

	entry = proc_create("stats", 0, ar8236_proc_dir, &ar8236_proc_stats);
	if (!entry)
		goto err_stats;

	entry = proc_create("stats_clear", 0, ar8236_proc_dir, &ar8236_proc_stats_clear);
	if (!entry)
		goto err_stats_clear;

	entry = proc_create("fdb", 0, ar8236_proc_dir, &ar8236_proc_fdb);
	if (!entry)
		goto err_fdb;

	entry = proc_create("reg_read", 0644, ar8236_proc_dir, &ar8236_proc_reg_read);
	if (!entry)
		goto err_reg_read;

	entry = proc_create("reg_write", 0644, ar8236_proc_dir, &ar8236_proc_reg_write);
	if (!entry)
		goto err_reg_write;
	
	return 0;

err_reg_write:
	remove_proc_entry("reg_read", ar8236_proc_dir);
err_reg_read:
	remove_proc_entry("fdb", ar8236_proc_dir);
err_fdb:
	remove_proc_entry("stats_clear", ar8236_proc_dir);
err_stats_clear:
	remove_proc_entry("stats", ar8236_proc_dir);
err_stats:
	remove_proc_entry("ar8236", NULL);
err_dir:
	return -ENOMEM;
}
static void ar8236_proc_cleanup(void)
{
	remove_proc_entry("reg_write", ar8236_proc_dir);
	remove_proc_entry("reg_read", ar8236_proc_dir);
	remove_proc_entry("stats_rd_noclear", ar8236_proc_dir);
	remove_proc_entry("stats_rd_clear", ar8236_proc_dir);
	remove_proc_entry("fdb", ar8236_proc_dir);
	remove_proc_entry("ar8236", NULL);
}
#endif

static int __init ar8236_init(void)
{
	register_switch_driver(&ar8236_switch_ops);

#ifdef CONFIG_PROC_FS
	ar8236_proc_init();
#endif
	return 0;
}
module_init(ar8236_init);

static void __exit ar8236_cleanup(void)
{
	unregister_switch_driver(&ar8236_switch_ops);

#ifdef CONFIG_PROC_FS
	ar8236_proc_cleanup();
#endif
}
module_exit(ar8236_cleanup);

MODULE_AUTHOR("Weigang <weigang@tp-link.com.cn>");
MODULE_DESCRIPTION("Driver for Atheros AR8236 ethernet switch chips");
MODULE_LICENSE("GPL");

