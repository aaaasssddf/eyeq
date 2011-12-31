#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include "rl.h"

extern struct net_device *iso_netdev;

static netdev_tx_t (*old_ndo_start_xmit)(struct sk_buff *, struct net_device *);
netdev_tx_t iso_ndo_start_xmit(struct sk_buff *, struct net_device *);
rx_handler_result_t iso_rx_handler(struct sk_buff **);

int iso_tx_hook_init(void);
void iso_tx_hook_exit(void);

int iso_rx_hook_init(void);
void iso_rx_hook_exit(void);

enum iso_verdict iso_tx(struct sk_buff *skb, const struct net_device *out);
enum iso_verdict iso_rx(struct sk_buff *skb, const struct net_device *in);

inline void skb_xmit(struct sk_buff *skb) {
	if(likely(old_ndo_start_xmit != NULL)) {
		old_ndo_start_xmit(skb, iso_netdev);
	}
}

int iso_tx_hook_init() {
	struct net_device_ops *ops;

	if(iso_netdev == NULL || iso_netdev->netdev_ops == NULL)
		return 1;

	ops = (struct net_device_ops *)iso_netdev->netdev_ops;

	rtnl_lock();
	old_ndo_start_xmit = ops->ndo_start_xmit;
	ops->ndo_start_xmit = iso_ndo_start_xmit;
	rtnl_unlock();
	return 0;
}

void iso_tx_hook_exit() {
	struct net_device_ops *ops = (struct net_device_ops *)iso_netdev->netdev_ops;

	rtnl_lock();
	ops->ndo_start_xmit = old_ndo_start_xmit;
	rtnl_unlock();
}

int iso_rx_hook_init() {
	int ret = 0;

	if(iso_netdev == NULL)
		return 1;

	rtnl_lock();
	ret = netdev_rx_handler_register(iso_netdev, iso_rx_handler, NULL);
	rtnl_unlock();

	/* Wait till stack sees our new handler */
	synchronize_net();
	return ret;
}

void iso_rx_hook_exit() {
	rtnl_lock();
	netdev_rx_handler_unregister(iso_netdev);
	rtnl_unlock();
	synchronize_net();
}

netdev_tx_t iso_ndo_start_xmit(struct sk_buff *skb, struct net_device *out) {
	enum iso_verdict verdict;

	skb_reset_mac_header(skb);
	verdict = iso_tx(skb, out);

	switch(verdict) {
	case ISO_VERDICT_DROP:
		/* XXX: Should we return NETDEV_TX_BUSY or NETDEV_TX_OK ? */
		/* NB: freeing the buffer seems to introduce some problems, so
		   choosing TX_BUSY. */
		/* kfree_skb(skb); */
		return NETDEV_TX_BUSY;

	case ISO_VERDICT_PASS:
		skb_xmit(skb);
		return NETDEV_TX_OK;

	case ISO_VERDICT_SUCCESS:
	default:
		return NETDEV_TX_OK;
	}

	/* Unreachable */
	return NETDEV_TX_OK;
}

rx_handler_result_t iso_rx_handler(struct sk_buff **pskb) {
	struct sk_buff *skb = *pskb;
	enum iso_verdict verdict;

	if(unlikely(skb->pkt_type == PACKET_LOOPBACK))
		return RX_HANDLER_PASS;

	verdict = iso_rx(skb, iso_netdev);

	switch(verdict) {
	case ISO_VERDICT_DROP:
		kfree_skb(skb);
		return RX_HANDLER_CONSUMED;

	case ISO_VERDICT_SUCCESS:
	case ISO_VERDICT_PASS:
	default:
		return RX_HANDLER_PASS;
	}

	/* Unreachable */
	return RX_HANDLER_PASS;
}

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */
