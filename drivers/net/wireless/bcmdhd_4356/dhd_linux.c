/*
 * Broadcom Dongle Host Driver (DHD), Linux-specific network interface
 * Basically selected code segments from usb-cdc.c and usb-rndis.c
 *
 * Copyright (C) 1999-2015, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: dhd_linux.c 556940 2015-05-15 08:50:10Z $
 */

#include <typedefs.h>
#include <linuxver.h>
#include <osl.h>
#ifdef SHOW_LOGTRACE
#include <linux/syscalls.h>
#include <event_log.h>
#endif 
#ifdef DHD_DEBUG
#include <linux/syscalls.h>
#endif 


#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/rtnetlink.h>
#include <linux/etherdevice.h>
#include <linux/random.h>
#include <linux/spinlock.h>
#include <linux/ethtool.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#ifdef CUSTOMER_HW_ONE
#include <linux/ioprio.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#endif
#include <linux/ip.h>
#include <linux/reboot.h>
#include <linux/notifier.h>
#include <net/addrconf.h>
#ifdef ENABLE_ADAPTIVE_SCHED
#include <linux/cpufreq.h>
#endif 
#ifdef MODULE_INIT_ASYNC
#include <linux/async.h>
#endif

#include <asm/uaccess.h>
#include <asm/unaligned.h>

#include <epivers.h>
#include <bcmutils.h>
#include <bcmendian.h>
#include <bcmdevs.h>

#include <proto/ethernet.h>
#include <proto/bcmevent.h>
#include <proto/vlan.h>
#ifdef DHD_L2_FILTER
#include <proto/bcmicmp.h>
#endif
#include <proto/802.3.h>

#include <dngl_stats.h>
#include <dhd_linux_wq.h>
#include <dhd.h>
#include <dhd_linux.h>
#ifdef PCIE_FULL_DONGLE
#include <dhd_flowring.h>
#include <bcmmsgbuf.h>
#include <pcie_core.h>
#include <bcmpcie.h>
#include <dhd_pcie.h>
#endif
#include <dhd_bus.h>
#include <dhd_proto.h>
#include <dhd_dbg.h>
#ifdef CONFIG_HAS_WAKELOCK
#include <linux/wakelock.h>
#endif
#ifdef WL_CFG80211
#include <wl_cfg80211.h>
#endif
#ifdef P2PONEINT
#include <wl_cfgp2p.h>
#endif
#ifdef PNO_SUPPORT
#include <dhd_pno.h>
#endif
#ifdef WLBTAMP
#include <proto/802.11_bta.h>
#include <proto/bt_amp_hci.h>
#include <dhd_bta.h>
#endif

#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif

#ifdef DHD_WMF
#include <dhd_wmf_linux.h>
#endif 

#ifdef AMPDU_VO_ENABLE
#include <proto/802.1d.h>
#endif 
#ifdef DHDTCPACK_SUPPRESS
#include <dhd_ip.h>
#endif 

#if defined(DHD_TCP_WINSIZE_ADJUST)
#include <linux/tcp.h>
#include <net/tcp.h>
#endif 

#if defined(DHD_RX_DUMP) || defined(DHD_TX_DUMP)
static const char *_get_packet_type_str(uint16 type);
#endif 
#ifdef DHD_RX_DUMP
int dhd_rx_dump_flag = 0;
int dhd_event_dump = 0;
#endif
#ifdef DHD_TX_DUMP
int dhd_tx_dump_flag = 0;
#endif
extern int otp_write;
#if defined(USE_STATIC_MEMDUMP)
#include <linux/path.h>
#include <linux/namei.h>
#include <linux/fs.h>

#define MEMDUMP_MINSIZE	786430
#define MEMDUMP_WATCHDOG_MS	250
#define MAX_RETRY	10
static int file_cnt = 0;
dhd_pub_t *g_dhdp = NULL;
#endif 

#ifdef CUSTOMER_HW_ONE
#include <linux/sched.h>

#define WLC_HT_TKIP_RESTRICT    0x02    
#define WLC_HT_WEP_RESTRICT     0x01    

#define CUSTOM_AP_AMPDU_BA_WSIZE    32

void wl_android_traffic_monitor(struct net_device *);
extern int wl_cfg80211_dhd_chk_link(void);
extern int wl_android_is_during_wifi_call(void);
extern int dhd_set_keepalive(int value);
static inline void set_wlan_ioprio(void);
static void adjust_thread_priority(void);
static void adjust_rxf_thread_priority(void);
static int rt_class(int priority);

extern int dhd_checkdied(dhd_pub_t *dhd);
static unsigned int dhdhtc_power_ctrl_mask = 0;
int dhdcdc_power_active_while_plugin = 1;
int dhdcdc_wifiLock = 0; 
static int txq_full_event_num = 0;
int wlan_ioprio_idle = 0;
static int prev_wlan_ioprio_idle = 0;
extern int multi_core_locked;
dhd_pub_t *priv_dhdp = NULL;
int is_screen_off = 0;
int multicast_lock = 0;
#ifdef PKT_FILTER_SUPPORT
static struct mutex enable_pktfilter_mutex;
#endif 

void wl_android_set_screen_off(int off);
static int dhd_set_suspend(int value, dhd_pub_t *dhd);


#define TRAFFIC_HIGH_WATER_MARK         670 *(3000/1000)
#define TRAFFIC_LOW_WATER_MARK          280 * (3000/1000)
struct mutex _dhd_onoff_mutex_lock_;
DEFINE_MUTEX(_dhd_onoff_mutex_lock_);
int wl_cfg80211_send_priv_event(struct net_device *dev, char *flag);
#ifdef BCMPCIE
static bool isresched = FALSE;
#endif 
#endif 

#ifdef WLMEDIA_HTSF
#include <linux/time.h>
#include <htsf.h>

#define HTSF_MINLEN 200    
#define HTSF_BUS_DELAY 150 
#define TSMAX  1000        
#define NUMBIN 34

static uint32 tsidx = 0;
static uint32 htsf_seqnum = 0;
uint32 tsfsync;
struct timeval tsync;
static uint32 tsport = 5010;

typedef struct histo_ {
	uint32 bin[NUMBIN];
} histo_t;

#if !ISPOWEROF2(DHD_SDALIGN)
#error DHD_SDALIGN is not a power of 2!
#endif

static histo_t vi_d1, vi_d2, vi_d3, vi_d4;
#endif 

#if defined(DHD_TCP_WINSIZE_ADJUST)
#define MIN_TCP_WIN_SIZE 18000
#define WIN_SIZE_SCALE_FACTOR 2
#define MAX_TARGET_PORTS 5

static uint target_ports[MAX_TARGET_PORTS] = {20, 0, 0, 0, 0};
static uint dhd_use_tcp_window_size_adjust = FALSE;
static void dhd_adjust_tcp_winsize(int op_mode, struct sk_buff *skb);
#endif 


#if defined(SOFTAP)
extern bool ap_cfg_running;
extern bool ap_fw_loaded;
#endif


#ifdef ENABLE_ADAPTIVE_SCHED
#define DEFAULT_CPUFREQ_THRESH		1000000	
#ifndef CUSTOM_CPUFREQ_THRESH
#define CUSTOM_CPUFREQ_THRESH	DEFAULT_CPUFREQ_THRESH
#endif 
#endif 

#define AOE_IP_ALIAS_SUPPORT 1

#ifdef BCM_FD_AGGR
#include <bcm_rpc.h>
#include <bcm_rpc_tp.h>
#endif
#ifdef PROP_TXSTATUS
#include <wlfc_proto.h>
#include <dhd_wlfc.h>
#endif

#include <wl_android.h>

#if defined(CUSTOMER_HW20) && defined(WLANAUDIO)
#include <sdaudio.h>
#endif 

#define DHD_MAX_STA     32


const uint8 wme_fifo2ac[] = { 0, 1, 2, 3, 1, 1 };
const uint8 prio2fifo[8] = { 1, 0, 0, 1, 2, 2, 3, 3 };
#define WME_PRIO2AC(prio)  wme_fifo2ac[prio2fifo[(prio)]]

#ifdef ARP_OFFLOAD_SUPPORT
void aoe_update_host_ipv4_table(dhd_pub_t *dhd_pub, u32 ipa, bool add, int idx);
static int dhd_inetaddr_notifier_call(struct notifier_block *this,
	unsigned long event, void *ptr);
static struct notifier_block dhd_inetaddr_notifier = {
	.notifier_call = dhd_inetaddr_notifier_call
};
static bool dhd_inetaddr_notifier_registered = FALSE;
#endif 

#ifdef CONFIG_IPV6
static int dhd_inet6addr_notifier_call(struct notifier_block *this,
	unsigned long event, void *ptr);
static struct notifier_block dhd_inet6addr_notifier = {
	.notifier_call = dhd_inet6addr_notifier_call
};
static bool dhd_inet6addr_notifier_registered = FALSE;
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && defined(CONFIG_PM_SLEEP)
#include <linux/suspend.h>
volatile bool dhd_mmc_suspend = FALSE;
DECLARE_WAIT_QUEUE_HEAD(dhd_dpc_wait);
#endif 

#if defined(OOB_INTR_ONLY)
extern void dhd_enable_oob_intr(struct dhd_bus *bus, bool enable);
#endif 
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && (1)
static void dhd_hang_process(void *dhd_info, void *event_data, u8 event);
#endif 
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
MODULE_LICENSE("GPL v2");
#endif 

#include <dhd_bus.h>

#ifdef BCM_FD_AGGR
#define DBUS_RX_BUFFER_SIZE_DHD(net)	(BCM_RPC_TP_DNGL_AGG_MAX_BYTE)
#else
#ifndef PROP_TXSTATUS
#define DBUS_RX_BUFFER_SIZE_DHD(net)	(net->mtu + net->hard_header_len + dhd->pub.hdrlen)
#else
#define DBUS_RX_BUFFER_SIZE_DHD(net)	(net->mtu + net->hard_header_len + dhd->pub.hdrlen + 128)
#endif
#endif 

#ifdef PROP_TXSTATUS
extern bool dhd_wlfc_skip_fc(void);
extern void dhd_wlfc_plat_init(void *dhd);
extern void dhd_wlfc_plat_deinit(void *dhd);
#endif 

#if LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 15)
const char *
print_tainted()
{
	return "";
}
#endif	

#if defined(WL_WIRELESS_EXT)
#include <wl_iw.h>
extern wl_iw_extra_params_t  g_wl_iw_params;
#endif 

#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(DHD_USE_EARLYSUSPEND)
#include <linux/earlysuspend.h>
#endif 

extern int dhd_get_suspend_bcn_li_dtim(dhd_pub_t *dhd);

#ifdef PKT_FILTER_SUPPORT
extern void dhd_pktfilter_offload_set(dhd_pub_t * dhd, char *arg);
extern void dhd_pktfilter_offload_enable(dhd_pub_t * dhd, char *arg, int enable, int master_mode);
extern void dhd_pktfilter_offload_delete(dhd_pub_t *dhd, int id);
#endif


#ifdef READ_MACADDR
extern int dhd_read_macaddr(struct dhd_info *dhd);
#else
static inline int dhd_read_macaddr(struct dhd_info *dhd) { return 0; }
#endif
#ifdef WRITE_MACADDR
extern int dhd_write_macaddr(struct ether_addr *mac);
#else
static inline int dhd_write_macaddr(struct ether_addr *mac) { return 0; }
#endif


#if defined(SOFTAP_TPUT_ENHANCE)
extern void dhd_bus_setidletime(dhd_pub_t *dhdp, int idle_time);
extern void dhd_bus_getidletime(dhd_pub_t *dhdp, int* idle_time);
#endif 


#if defined(USE_STATIC_MEMDUMP)
static void dhd_mem_dump(void *dhd_info, void *event_info, u8 event);
#endif 

#ifdef SET_RPS_CPUS
int custom_rps_map_set(struct netdev_rx_queue *queue, char *buf, size_t len);
void custom_rps_map_clear(struct netdev_rx_queue *queue);
#ifdef CONFIG_MACH_UNIVERSAL5433
#define RPS_CPUS_MASK "10"
#else
#define RPS_CPUS_MASK "6"
#endif 
#endif 

static int dhd_reboot_callback(struct notifier_block *this, unsigned long code, void *unused);
static struct notifier_block dhd_reboot_notifier = {
		.notifier_call = dhd_reboot_callback,
		.priority = 1,
};


typedef struct dhd_if_event {
	struct list_head	list;
	wl_event_data_if_t	event;
	char			name[IFNAMSIZ+1];
	uint8			mac[ETHER_ADDR_LEN];
} dhd_if_event_t;

typedef struct dhd_if {
	struct dhd_info *info;			
	
	struct net_device *net;
	int				idx;			
	uint			subunit;		
	uint8			mac_addr[ETHER_ADDR_LEN];	
	bool			set_macaddress;
	bool			set_multicast;
	uint8			bssidx;			
	bool			attached;		
	bool			txflowcontrol;	
	char			name[IFNAMSIZ+1]; 
	struct net_device_stats stats;
#ifdef DHD_WMF
	dhd_wmf_t		wmf;		
#endif 
#ifdef PCIE_FULL_DONGLE
	struct list_head sta_list;		
#if !defined(BCM_GMAC3)
	spinlock_t	sta_list_lock;		
#endif 
#endif 
	uint32  ap_isolate;			
} dhd_if_t;

#ifdef WLMEDIA_HTSF
typedef struct {
	uint32 low;
	uint32 high;
} tsf_t;

typedef struct {
	uint32 last_cycle;
	uint32 last_sec;
	uint32 last_tsf;
	uint32 coef;     
	uint32 coefdec1; 
	uint32 coefdec2; 
} htsf_t;

typedef struct {
	uint32 t1;
	uint32 t2;
	uint32 t3;
	uint32 t4;
} tstamp_t;

static tstamp_t ts[TSMAX];
static tstamp_t maxdelayts;
static uint32 maxdelay = 0, tspktcnt = 0, maxdelaypktno = 0;

#endif  

struct ipv6_work_info_t {
	uint8			if_idx;
	char			ipv6_addr[16];
	unsigned long		event;
};

#if defined(USE_STATIC_MEMDUMP)
typedef struct dhd_dump {
	uint8 *buf;
	int bufsize;
} dhd_dump_t;
#endif 

#if defined(CUSTOMER_HW20) && defined(WLANAUDIO)
#define MAX_WLANAUDIO_BLACKLIST 4

struct wlanaudio_blacklist {
	bool is_blacklist;
	uint32 cnt;
	ulong txfail_jiffies;
	struct ether_addr blacklist_addr;
};
#endif 


typedef struct dhd_info {
#if defined(WL_WIRELESS_EXT)
	wl_iw_t		iw;		
#endif 
	dhd_pub_t pub;
	dhd_if_t *iflist[DHD_MAX_IFS]; 

	void *adapter;			
	char fw_path[PATH_MAX];		
	char nv_path[PATH_MAX];		

	struct semaphore proto_sem;
#ifdef PROP_TXSTATUS
	spinlock_t	wlfc_spinlock;

#endif 
#ifdef WLMEDIA_HTSF
	htsf_t  htsf;
#endif
	wait_queue_head_t ioctl_resp_wait;
	wait_queue_head_t d3ack_wait;
	uint32	default_wd_interval;

	struct timer_list timer;
	bool wd_timer_valid;
	struct tasklet_struct tasklet;
	spinlock_t	sdlock;
	spinlock_t	txqlock;
	spinlock_t	dhd_lock;

	struct semaphore sdsem;
	tsk_ctl_t	thr_dpc_ctl;
	tsk_ctl_t	thr_wdt_ctl;

	tsk_ctl_t	thr_rxf_ctl;
	spinlock_t	rxf_lock;
	bool		rxthread_enabled;

	
#if defined(CONFIG_HAS_WAKELOCK) && (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	struct wake_lock wl_wifi;   
	struct wake_lock wl_rxwake; 
	struct wake_lock wl_ctrlwake; 
	struct wake_lock wl_wdwake; 
#ifdef BCMPCIE_OOB_HOST_WAKE
	struct wake_lock wl_intrwake; 
#endif 
#ifdef CUSTOMER_HW_ONE
	struct wake_lock wl_htc; 	
#endif
#endif 

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)) && 1
	struct mutex dhd_net_if_mutex;
	struct mutex dhd_suspend_mutex;
#endif
	spinlock_t wakelock_spinlock;
	uint32 wakelock_counter;
	int wakelock_wd_counter;
	int wakelock_rx_timeout_enable;
	int wakelock_ctrl_timeout_enable;
	bool waive_wakelock;
	uint32 wakelock_before_waive;

	
	wait_queue_head_t ctrl_wait;
	atomic_t pend_8021x_cnt;
	dhd_attach_states_t dhd_state;
#ifdef SHOW_LOGTRACE
	dhd_event_log_t event_data;
#endif 

#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(DHD_USE_EARLYSUSPEND)
	struct early_suspend early_suspend;
#endif 

#ifdef ARP_OFFLOAD_SUPPORT
	u32 pend_ipaddr;
#endif 
#ifdef CUSTOMER_HW_ONE
	bool dhd_force_exit;
#endif
#ifdef BCM_FD_AGGR
	void *rpc_th;
	void *rpc_osh;
	struct timer_list rpcth_timer;
	bool rpcth_timer_active;
	bool fdaggr;
#endif
#ifdef DHDTCPACK_SUPPRESS
	spinlock_t	tcpack_lock;
#endif 
	void			*dhd_deferred_wq;
#ifdef DEBUG_CPU_FREQ
	struct notifier_block freq_trans;
	int __percpu *new_freq;
#endif
	unsigned int unit;
	struct notifier_block pm_notifier;
#if defined(CUSTOMER_HW20) && defined(WLANAUDIO)
	struct wlanaudio_blacklist wlanaudio_blist[MAX_WLANAUDIO_BLACKLIST];
	bool is_wlanaudio_blist;
#endif 
} dhd_info_t;

#define DHDIF_FWDER(dhdif)      FALSE

uint dhd_download_fw_on_driverload = TRUE;

char firmware_path[MOD_PARAM_PATHLEN];
char nvram_path[MOD_PARAM_PATHLEN];

char fw_bak_path[MOD_PARAM_PATHLEN];
char nv_bak_path[MOD_PARAM_PATHLEN];

char info_string[MOD_PARAM_INFOLEN];
module_param_string(info_string, info_string, MOD_PARAM_INFOLEN, 0444);
int op_mode = 0;
int disable_proptx = 0;
module_param(op_mode, int, 0644);
extern int wl_control_wl_start(struct net_device *dev);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && (defined(BCMLXSDMMC) || \
	defined(BCMPCIE))
struct semaphore dhd_registration_sem;
#endif 

static void dhd_ifadd_event_handler(void *handle, void *event_info, u8 event);
static void dhd_ifdel_event_handler(void *handle, void *event_info, u8 event);
static void dhd_set_mac_addr_handler(void *handle, void *event_info, u8 event);
static void dhd_set_mcast_list_handler(void *handle, void *event_info, u8 event);
#ifdef CONFIG_IPV6
static void dhd_inet6_work_handler(void *dhd_info, void *event_data, u8 event);
#endif

#ifdef WL_CFG80211
extern void dhd_netdev_free(struct net_device *ndev);
#endif 

module_param(dhd_msg_level, int, 0);

#ifdef ARP_OFFLOAD_SUPPORT
uint dhd_arp_enable = TRUE;
module_param(dhd_arp_enable, uint, 0);


uint dhd_arp_mode = ARP_OL_AGENT | ARP_OL_PEER_AUTO_REPLY;

module_param(dhd_arp_mode, uint, 0);
#endif 

module_param(disable_proptx, int, 0644);
module_param_string(firmware_path, firmware_path, MOD_PARAM_PATHLEN, 0660);
module_param_string(nvram_path, nvram_path, MOD_PARAM_PATHLEN, 0660);


#define WATCHDOG_EXTEND_INTERVAL (2000)

uint dhd_watchdog_ms = CUSTOM_DHD_WATCHDOG_MS;
module_param(dhd_watchdog_ms, uint, 0644);

#if defined(DHD_DEBUG)
uint dhd_console_ms = 0;
module_param(dhd_console_ms, uint, 0644);
#endif 


uint dhd_slpauto = TRUE;
module_param(dhd_slpauto, uint, 0);

#ifdef PKT_FILTER_SUPPORT
uint dhd_pkt_filter_enable = TRUE;
module_param(dhd_pkt_filter_enable, uint, 0);
#endif

uint dhd_pkt_filter_init = 0;
module_param(dhd_pkt_filter_init, uint, 0);

uint dhd_master_mode = TRUE;
module_param(dhd_master_mode, uint, 0);

int dhd_watchdog_prio = 0;
module_param(dhd_watchdog_prio, int, 0);

int dhd_dpc_prio = CUSTOM_DPC_PRIO_SETTING;
module_param(dhd_dpc_prio, int, 0);

int dhd_rxf_prio = CUSTOM_RXF_PRIO_SETTING;
module_param(dhd_rxf_prio, int, 0);

int passive_channel_skip = 0;
module_param(passive_channel_skip, int, (S_IRUSR|S_IWUSR));

#if !defined(BCMDHDUSB)
extern int dhd_dongle_ramsize;
module_param(dhd_dongle_ramsize, int, 0);
#endif 

static int dhd_found = 0;
static int instance_base = 0; 
module_param(instance_base, int, 0644);

#if defined(CUSTOMER_HW20) && defined(WLANAUDIO)
dhd_info_t *dhd_global = NULL;
#endif 



#define DHD_PERIM_RADIO_INIT()              do {  } while (0)
#define DHD_PERIM_LOCK_TRY(unit, flag)      do {  } while (0)
#define DHD_PERIM_UNLOCK_TRY(unit, flag)    do {  } while (0)
#define DHD_PERIM_LOCK_ALL()                do {  } while (0)
#define DHD_PERIM_UNLOCK_ALL()              do {  } while (0)

#ifdef PCIE_FULL_DONGLE
#if defined(BCM_GMAC3)
#define DHD_IF_STA_LIST_LOCK_INIT(ifp)      do {  } while (0)
#define DHD_IF_STA_LIST_LOCK(ifp, flags)    ({ BCM_REFERENCE(flags); })
#define DHD_IF_STA_LIST_UNLOCK(ifp, flags)  ({ BCM_REFERENCE(flags); })
#else 
#define DHD_IF_STA_LIST_LOCK_INIT(ifp) spin_lock_init(&(ifp)->sta_list_lock)
#define DHD_IF_STA_LIST_LOCK(ifp, flags) do { \
	spin_lock_irqsave(&(ifp)->sta_list_lock, (flags)); \
	smp_mb(); } while (0)
#define DHD_IF_STA_LIST_UNLOCK(ifp, flags) \
	spin_unlock_irqrestore(&(ifp)->sta_list_lock, (flags))
#endif 
#endif 

#ifdef BCMCCX
uint dhd_roam_disable = 0;
#else
uint dhd_roam_disable = 0;
#endif 

uint dhd_radio_up = 1;

char iface_name[IFNAMSIZ] = {'\0'};
module_param_string(iface_name, iface_name, IFNAMSIZ, 0);


int dhd_ioctl_timeout_msec = IOCTL_RESP_TIMEOUT;

int dhd_idletime = DHD_IDLETIME_TICKS;
module_param(dhd_idletime, int, 0);

uint dhd_poll = FALSE;
module_param(dhd_poll, uint, 0);

uint dhd_intr = TRUE;
module_param(dhd_intr, uint, 0);

uint dhd_sdiod_drive_strength = 6;
module_param(dhd_sdiod_drive_strength, uint, 0);

#ifdef BCMSDIO
extern uint dhd_txbound;
extern uint dhd_rxbound;
module_param(dhd_txbound, uint, 0);
module_param(dhd_rxbound, uint, 0);

extern uint dhd_deferred_tx;
module_param(dhd_deferred_tx, uint, 0);

#ifdef BCMDBGFS
extern void dhd_dbg_init(dhd_pub_t *dhdp);
extern void dhd_dbg_remove(void);
#endif 

#endif 


#ifdef SDTEST
uint dhd_pktgen = 0;
module_param(dhd_pktgen, uint, 0);

uint dhd_pktgen_len = 0;
module_param(dhd_pktgen_len, uint, 0);
#endif 

#if defined(BCMSUP_4WAY_HANDSHAKE)
uint dhd_use_idsup = 0;
module_param(dhd_use_idsup, uint, 0);
#endif 

extern char dhd_version[];

int dhd_net_bus_devreset(struct net_device *dev, uint8 flag);
static void dhd_net_if_lock_local(dhd_info_t *dhd);
static void dhd_net_if_unlock_local(dhd_info_t *dhd);
#ifndef CUSTOMER_HW_ONE
static void dhd_suspend_lock(dhd_pub_t *dhdp);
static void dhd_suspend_unlock(dhd_pub_t *dhdp);
#endif

#ifdef WLMEDIA_HTSF
void htsf_update(dhd_info_t *dhd, void *data);
tsf_t prev_tsf, cur_tsf;

uint32 dhd_get_htsf(dhd_info_t *dhd, int ifidx);
static int dhd_ioctl_htsf_get(dhd_info_t *dhd, int ifidx);
static void dhd_dump_latency(void);
static void dhd_htsf_addtxts(dhd_pub_t *dhdp, void *pktbuf);
static void dhd_htsf_addrxts(dhd_pub_t *dhdp, void *pktbuf);
static void dhd_dump_htsfhisto(histo_t *his, char *s);
#endif 

int dhd_monitor_init(void *dhd_pub);
int dhd_monitor_uninit(void);


#if defined(WL_WIRELESS_EXT)
struct iw_statistics *dhd_get_wireless_stats(struct net_device *dev);
#endif 

static void dhd_dpc(ulong data);
extern int dhd_wait_pend8021x(struct net_device *dev);
void dhd_os_wd_timer_extend(void *bus, bool extend);

#ifdef TOE
#ifndef BDC
#error TOE requires BDC
#endif 
static int dhd_toe_get(dhd_info_t *dhd, int idx, uint32 *toe_ol);
static int dhd_toe_set(dhd_info_t *dhd, int idx, uint32 toe_ol);
#endif 

static int dhd_wl_host_event(dhd_info_t *dhd, int *ifidx, void *pktdata,
                             wl_event_msg_t *event_ptr, void **data_ptr);
#if defined(DHD_UNICAST_DHCP) || defined(DHD_L2_FILTER)
static const uint8 llc_snap_hdr[SNAP_HDR_LEN] = {0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00};
static int dhd_get_pkt_ip_type(dhd_pub_t *dhd, void *skb, uint8 **data_ptr,
	int *len_ptr, uint8 *prot_ptr);
static int dhd_get_pkt_ether_type(dhd_pub_t *dhd, void *skb, uint8 **data_ptr,
	int *len_ptr, uint16 *et_ptr, bool *snap_ptr);
#endif 

#ifdef DHD_UNICAST_DHCP
static int dhd_convert_dhcp_broadcast_ack_to_unicast(dhd_pub_t *pub, void *pktbuf, int ifidx);
#endif 
#ifdef DHD_L2_FILTER
static int dhd_l2_filter_block_ping(dhd_pub_t *pub, void *pktbuf, int ifidx);
#endif
#if defined(CONFIG_PM_SLEEP)
static int dhd_pm_callback(struct notifier_block *nfb, unsigned long action, void *ignored)
{
	int ret = NOTIFY_DONE;
	bool suspend = FALSE;
	dhd_info_t *dhdinfo = (dhd_info_t*)container_of(nfb, struct dhd_info, pm_notifier);

	BCM_REFERENCE(dhdinfo);
	switch (action) {
	case PM_HIBERNATION_PREPARE:
	case PM_SUSPEND_PREPARE:
		suspend = TRUE;
		break;
	case PM_POST_HIBERNATION:
	case PM_POST_SUSPEND:
		suspend = FALSE;
		break;
	}

#if defined(SUPPORT_P2P_GO_PS)
#ifdef PROP_TXSTATUS
	if (suspend) {
		DHD_OS_WAKE_LOCK_WAIVE(&dhdinfo->pub);
		dhd_wlfc_suspend(&dhdinfo->pub);
		DHD_OS_WAKE_LOCK_RESTORE(&dhdinfo->pub);
	} else
		dhd_wlfc_resume(&dhdinfo->pub);
#endif
#endif 

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && (LINUX_VERSION_CODE <= \
	KERNEL_VERSION(2, 6, 39))
	dhd_mmc_suspend = suspend;
	smp_mb();
#endif

	return ret;
}

static struct notifier_block dhd_pm_notifier = {
	.notifier_call = dhd_pm_callback,
	.priority = 10
};
static bool dhd_pm_notifier_registered = FALSE;

extern int register_pm_notifier(struct notifier_block *nb);
extern int unregister_pm_notifier(struct notifier_block *nb);
#endif 

static void dhd_sched_rxf(dhd_pub_t *dhdp, void *skb);
static void dhd_os_rxflock(dhd_pub_t *pub);
static void dhd_os_rxfunlock(dhd_pub_t *pub);

typedef struct dhd_dev_priv {
	dhd_info_t * dhd; 
	dhd_if_t   * ifp; 
	int          ifidx; 
} dhd_dev_priv_t;

#define DHD_DEV_PRIV_SIZE       (sizeof(dhd_dev_priv_t))
#define DHD_DEV_PRIV(dev)       ((dhd_dev_priv_t *)DEV_PRIV(dev))
#define DHD_DEV_INFO(dev)       (((dhd_dev_priv_t *)DEV_PRIV(dev))->dhd)
#define DHD_DEV_IFP(dev)        (((dhd_dev_priv_t *)DEV_PRIV(dev))->ifp)
#define DHD_DEV_IFIDX(dev)      (((dhd_dev_priv_t *)DEV_PRIV(dev))->ifidx)

static inline void
dhd_dev_priv_clear(struct net_device * dev)
{
	dhd_dev_priv_t * dev_priv;
	ASSERT(dev != (struct net_device *)NULL);
	dev_priv = DHD_DEV_PRIV(dev);
	dev_priv->dhd = (dhd_info_t *)NULL;
	dev_priv->ifp = (dhd_if_t *)NULL;
	dev_priv->ifidx = DHD_BAD_IF;
}

static inline void
dhd_dev_priv_save(struct net_device * dev, dhd_info_t * dhd, dhd_if_t * ifp,
                  int ifidx)
{
	dhd_dev_priv_t * dev_priv;
	ASSERT(dev != (struct net_device *)NULL);
	dev_priv = DHD_DEV_PRIV(dev);
	dev_priv->dhd = dhd;
	dev_priv->ifp = ifp;
	dev_priv->ifidx = ifidx;
}

#ifdef PCIE_FULL_DONGLE


dhd_info_t dhd_info_null = {
#if defined(BCM_GMAC3)
	.fwdh = FWDER_NULL,
#endif
	.pub = {
	         .info = &dhd_info_null,
#ifdef DHDTCPACK_SUPPRESS
	         .tcpack_sup_mode = TCPACK_SUP_REPLACE,
#endif 
	         .up = FALSE, .busstate = DHD_BUS_DOWN
	}
};
#define DHD_INFO_NULL (&dhd_info_null)
#define DHD_PUB_NULL  (&dhd_info_null.pub)

struct net_device dhd_net_dev_null = {
	.reg_state = NETREG_UNREGISTERED
};
#define DHD_NET_DEV_NULL (&dhd_net_dev_null)

dhd_if_t dhd_if_null = {
#if defined(BCM_GMAC3)
	.fwdh = FWDER_NULL,
#endif
#ifdef WMF
	.wmf = { .wmf_enable = TRUE },
#endif
	.info = DHD_INFO_NULL,
	.net = DHD_NET_DEV_NULL,
	.idx = DHD_BAD_IF
};
#define DHD_IF_NULL  (&dhd_if_null)

#define DHD_STA_NULL ((dhd_sta_t *)NULL)


static inline dhd_if_t *dhd_get_ifp(dhd_pub_t *dhdp, uint32 ifidx);

static void dhd_sta_free(dhd_pub_t *pub, dhd_sta_t *sta);
static dhd_sta_t * dhd_sta_alloc(dhd_pub_t * dhdp);

static void dhd_if_del_sta_list(dhd_if_t * ifp);
static void	dhd_if_flush_sta(dhd_if_t * ifp);

static int dhd_sta_pool_init(dhd_pub_t *dhdp, int max_sta);
static void dhd_sta_pool_fini(dhd_pub_t *dhdp, int max_sta);
static void dhd_sta_pool_clear(dhd_pub_t *dhdp, int max_sta);


static inline dhd_if_t *dhd_get_ifp(dhd_pub_t *dhdp, uint32 ifidx)
{
	ASSERT(ifidx < DHD_MAX_IFS);

	if (ifidx >= DHD_MAX_IFS)
		return NULL;

	return dhdp->info->iflist[ifidx];
}

static void
dhd_sta_free(dhd_pub_t * dhdp, dhd_sta_t * sta)
{
	int prio;

	ASSERT((sta != DHD_STA_NULL) && (sta->idx != ID16_INVALID));

	ASSERT((dhdp->staid_allocator != NULL) && (dhdp->sta_pool != NULL));
	id16_map_free(dhdp->staid_allocator, sta->idx);
	for (prio = 0; prio < (int)NUMPRIO; prio++)
		sta->flowid[prio] = FLOWID_INVALID;
	sta->ifp = DHD_IF_NULL; 
	sta->ifidx = DHD_BAD_IF;
	bzero(sta->ea.octet, ETHER_ADDR_LEN);
	INIT_LIST_HEAD(&sta->list);
	sta->idx = ID16_INVALID; 
}

static dhd_sta_t *
dhd_sta_alloc(dhd_pub_t * dhdp)
{
	uint16 idx;
	dhd_sta_t * sta;
	dhd_sta_pool_t * sta_pool;

	ASSERT((dhdp->staid_allocator != NULL) && (dhdp->sta_pool != NULL));

	idx = id16_map_alloc(dhdp->staid_allocator);
	if (idx == ID16_INVALID) {
		DHD_ERROR(("%s: cannot get free staid\n", __FUNCTION__));
		return DHD_STA_NULL;
	}

	sta_pool = (dhd_sta_pool_t *)(dhdp->sta_pool);
	sta = &sta_pool[idx];

	ASSERT((sta->idx == ID16_INVALID) &&
	       (sta->ifp == DHD_IF_NULL) && (sta->ifidx == DHD_BAD_IF));
	sta->idx = idx; 

	return sta;
}

static void
dhd_if_del_sta_list(dhd_if_t *ifp)
{
	dhd_sta_t *sta, *next;
	unsigned long flags;

	DHD_IF_STA_LIST_LOCK(ifp, flags);

	list_for_each_entry_safe(sta, next, &ifp->sta_list, list) {
#if defined(BCM_GMAC3)
		if (ifp->fwdh) {
			
			fwder_deassoc(ifp->fwdh, (uint16 *)(sta->ea.octet), (wofa_t)sta);
		}
#endif 
		list_del(&sta->list);
		dhd_sta_free(&ifp->info->pub, sta);
	}

	DHD_IF_STA_LIST_UNLOCK(ifp, flags);

	return;
}

static void
dhd_if_flush_sta(dhd_if_t * ifp)
{
#if defined(BCM_GMAC3)

	if (ifp && (ifp->fwdh != FWDER_NULL)) {
		dhd_sta_t *sta, *next;
		unsigned long flags;

		DHD_IF_STA_LIST_LOCK(ifp, flags);

		list_for_each_entry_safe(sta, next, &ifp->sta_list, list) {
			
			fwder_flush(ifp->fwdh, (wofa_t)sta);
		}

		DHD_IF_STA_LIST_UNLOCK(ifp, flags);
	}
#endif 
}

static int
dhd_sta_pool_init(dhd_pub_t *dhdp, int max_sta)
{
	int idx, sta_pool_memsz;
	dhd_sta_t * sta;
	dhd_sta_pool_t * sta_pool;
	void * staid_allocator;

	ASSERT(dhdp != (dhd_pub_t *)NULL);
	ASSERT((dhdp->staid_allocator == NULL) && (dhdp->sta_pool == NULL));

	
	staid_allocator = id16_map_init(dhdp->osh, max_sta, 1);
	if (staid_allocator == NULL) {
		DHD_ERROR(("%s: sta id allocator init failure\n", __FUNCTION__));
		return BCME_ERROR;
	}

	
	sta_pool_memsz = ((max_sta + 1) * sizeof(dhd_sta_t)); 
	sta_pool = (dhd_sta_pool_t *)MALLOC(dhdp->osh, sta_pool_memsz);
	if (sta_pool == NULL) {
		DHD_ERROR(("%s: sta table alloc failure\n", __FUNCTION__));
		id16_map_fini(dhdp->osh, staid_allocator);
		return BCME_ERROR;
	}

	dhdp->sta_pool = sta_pool;
	dhdp->staid_allocator = staid_allocator;

	
	bzero((uchar *)sta_pool, sta_pool_memsz);
	for (idx = max_sta; idx >= 1; idx--) { 
		sta = &sta_pool[idx];
		sta->idx = id16_map_alloc(staid_allocator);
		ASSERT(sta->idx <= max_sta);
	}
	
	for (idx = 1; idx <= max_sta; idx++) {
		sta = &sta_pool[idx];
		dhd_sta_free(dhdp, sta);
	}

	return BCME_OK;
}

static void
dhd_sta_pool_fini(dhd_pub_t *dhdp, int max_sta)
{
	dhd_sta_pool_t * sta_pool = (dhd_sta_pool_t *)dhdp->sta_pool;

	if (sta_pool) {
		int idx;
		int sta_pool_memsz = ((max_sta + 1) * sizeof(dhd_sta_t));
		for (idx = 1; idx <= max_sta; idx++) {
			ASSERT(sta_pool[idx].ifp == DHD_IF_NULL);
			ASSERT(sta_pool[idx].idx == ID16_INVALID);
		}
		MFREE(dhdp->osh, dhdp->sta_pool, sta_pool_memsz);
		dhdp->sta_pool = NULL;
	}

	id16_map_fini(dhdp->osh, dhdp->staid_allocator);
	dhdp->staid_allocator = NULL;
}

static void
dhd_sta_pool_clear(dhd_pub_t *dhdp, int max_sta)
{
	int idx, sta_pool_memsz;
	dhd_sta_t * sta;
	dhd_sta_pool_t * sta_pool;
	void *staid_allocator;

	if (!dhdp) {
		DHD_ERROR(("%s: dhdp is NULL\n", __FUNCTION__));
		return;
	}

	sta_pool = (dhd_sta_pool_t *)dhdp->sta_pool;
	staid_allocator = dhdp->staid_allocator;

	if (!sta_pool) {
		DHD_ERROR(("%s: sta_pool is NULL\n", __FUNCTION__));
		return;
	}

	if (!staid_allocator) {
		DHD_ERROR(("%s: staid_allocator is NULL\n", __FUNCTION__));
		return;
	}

	
	sta_pool_memsz = ((max_sta + 1) * sizeof(dhd_sta_t));
	bzero((uchar *)sta_pool, sta_pool_memsz);

	
	id16_map_clear(staid_allocator, max_sta, 1);

	
	for (idx = max_sta; idx >= 1; idx--) { 
		sta = &sta_pool[idx];
		sta->idx = id16_map_alloc(staid_allocator);
		ASSERT(sta->idx <= max_sta);
	}
	
	for (idx = 1; idx <= max_sta; idx++) {
		sta = &sta_pool[idx];
		dhd_sta_free(dhdp, sta);
	}
}

dhd_sta_t *
dhd_find_sta(void *pub, int ifidx, void *ea)
{
	dhd_sta_t *sta, *prev = NULL;
	dhd_if_t *ifp;
	unsigned long flags;
	bool loop = FALSE;

	ASSERT(ea != NULL);
	ifp = dhd_get_ifp((dhd_pub_t *)pub, ifidx);
	if (ifp == NULL)
		return DHD_STA_NULL;

	DHD_IF_STA_LIST_LOCK(ifp, flags);

	list_for_each_entry(sta, &ifp->sta_list, list) {
		if (!memcmp(sta->ea.octet, ea, ETHER_ADDR_LEN)) {
			DHD_IF_STA_LIST_UNLOCK(ifp, flags);
			return sta;
		} else {
			if (prev == sta) {
				DHD_ERROR(("%s: loop detected " MACDBG "\n",
					__FUNCTION__, MAC2STRDBG(sta->ea.octet)));
				loop = TRUE;
				break;
			} else
				prev = sta;
		}
	}

	DHD_IF_STA_LIST_UNLOCK(ifp, flags);
	if (loop)
		dhd_os_send_hang_message(pub);

	return DHD_STA_NULL;
}

dhd_sta_t *
dhd_add_sta(void *pub, int ifidx, void *ea)
{
	dhd_sta_t *sta;
	dhd_if_t *ifp;
	unsigned long flags;

	ASSERT(ea != NULL);
	ifp = dhd_get_ifp((dhd_pub_t *)pub, ifidx);
	if (ifp == NULL)
		return DHD_STA_NULL;

	sta = dhd_sta_alloc((dhd_pub_t *)pub);
	if (sta == DHD_STA_NULL) {
		DHD_ERROR(("%s: Alloc failed\n", __FUNCTION__));
		return DHD_STA_NULL;
	}

	memcpy(sta->ea.octet, ea, ETHER_ADDR_LEN);

	
	sta->ifp = ifp;
	sta->ifidx = ifidx;
	INIT_LIST_HEAD(&sta->list);

	DHD_IF_STA_LIST_LOCK(ifp, flags);

	DHD_ERROR(("%s: add sta " MACDBG "\n",
		__FUNCTION__, MAC2STRDBG(sta->ea.octet)));
	list_add_tail(&sta->list, &ifp->sta_list);

#if defined(BCM_GMAC3)
	if (ifp->fwdh) {
		ASSERT(ISALIGNED(ea, 2));
		
		fwder_reassoc(ifp->fwdh, (uint16 *)ea, (wofa_t)sta);
	}
#endif 

	DHD_IF_STA_LIST_UNLOCK(ifp, flags);

	return sta;
}

void
dhd_del_sta(void *pub, int ifidx, void *ea)
{
	dhd_sta_t *sta, *next, *prev = NULL;
	dhd_if_t *ifp;
	unsigned long flags;
	bool loop = FALSE;

	ASSERT(ea != NULL);
	ifp = dhd_get_ifp((dhd_pub_t *)pub, ifidx);
	if (ifp == NULL)
		return;

	DHD_IF_STA_LIST_LOCK(ifp, flags);

	list_for_each_entry_safe(sta, next, &ifp->sta_list, list) {
		if (!memcmp(sta->ea.octet, ea, ETHER_ADDR_LEN)) {
#if defined(BCM_GMAC3)
			if (ifp->fwdh) { 
				ASSERT(ISALIGNED(ea, 2));
				fwder_deassoc(ifp->fwdh, (uint16 *)ea, (wofa_t)sta);
			}
#endif 
			DHD_ERROR(("%s: del sta " MACDBG "\n",
				__FUNCTION__, MAC2STRDBG(sta->ea.octet)));
			list_del(&sta->list);
			dhd_sta_free(&ifp->info->pub, sta);
		} else {
			if (prev == sta) {
				DHD_ERROR(("%s: loop detected " MACDBG "\n",
					__FUNCTION__, MAC2STRDBG(sta->ea.octet)));
				loop = TRUE;
				break;
			} else
				prev = sta;
		}
	}

	DHD_IF_STA_LIST_UNLOCK(ifp, flags);

	if (loop)
		dhd_os_send_hang_message(pub);
	return;
}

dhd_sta_t*
dhd_findadd_sta(void *pub, int ifidx, void *ea)
{
	dhd_sta_t *sta;

	sta = dhd_find_sta(pub, ifidx, ea);

	if (!sta) {
		
		sta = dhd_add_sta(pub, ifidx, ea);
	}

	return sta;
}
#else
static inline void dhd_if_flush_sta(dhd_if_t * ifp) { }
static inline void dhd_if_del_sta_list(dhd_if_t *ifp) {}
static inline int dhd_sta_pool_init(dhd_pub_t *dhdp, int max_sta) { return BCME_OK; }
static inline void dhd_sta_pool_fini(dhd_pub_t *dhdp, int max_sta) {}
static inline void dhd_sta_pool_clear(dhd_pub_t *dhdp, int max_sta) {}
dhd_sta_t *dhd_findadd_sta(void *pub, int ifidx, void *ea) { return NULL; }
void dhd_del_sta(void *pub, int ifidx, void *ea) {}
#endif 


int dhd_bssidx2idx(dhd_pub_t *dhdp, uint32 bssidx)
{
	dhd_if_t *ifp;
	dhd_info_t *dhd = dhdp->info;
	int i;

	ASSERT(bssidx < DHD_MAX_IFS);
	ASSERT(dhdp);

	for (i = 0; i < DHD_MAX_IFS; i++) {
		ifp = dhd->iflist[i];
		if (ifp && (ifp->bssidx == bssidx)) {
			DHD_TRACE(("Index manipulated for %s from %d to %d\n",
				ifp->name, bssidx, i));
			break;
		}
	}
	return i;
}

static inline int dhd_rxf_enqueue(dhd_pub_t *dhdp, void* skb)
{
	uint32 store_idx;
	uint32 sent_idx;

	if (!skb) {
		DHD_ERROR(("dhd_rxf_enqueue: NULL skb!!!\n"));
		return BCME_ERROR;
	}

	dhd_os_rxflock(dhdp);
	store_idx = dhdp->store_idx;
	sent_idx = dhdp->sent_idx;
	if (dhdp->skbbuf[store_idx] != NULL) {
		
		dhd_os_rxfunlock(dhdp);
#ifdef RXF_DEQUEUE_ON_BUSY
		DHD_TRACE(("dhd_rxf_enqueue: pktbuf not consumed %p, store idx %d sent idx %d\n",
			skb, store_idx, sent_idx));
		return BCME_BUSY;
#else 
		DHD_ERROR(("dhd_rxf_enqueue: pktbuf not consumed %p, store idx %d sent idx %d\n",
			skb, store_idx, sent_idx));
#if defined(WAIT_DEQUEUE)
		OSL_SLEEP(1);
#endif
		return BCME_ERROR;
#endif 
	}
	DHD_TRACE(("dhd_rxf_enqueue: Store SKB %p. idx %d -> %d\n",
		skb, store_idx, (store_idx + 1) & (MAXSKBPEND - 1)));
	dhdp->skbbuf[store_idx] = skb;
	dhdp->store_idx = (store_idx + 1) & (MAXSKBPEND - 1);
	dhd_os_rxfunlock(dhdp);

	return BCME_OK;
}

static inline void* dhd_rxf_dequeue(dhd_pub_t *dhdp)
{
	uint32 store_idx;
	uint32 sent_idx;
	void *skb;

	dhd_os_rxflock(dhdp);

	store_idx = dhdp->store_idx;
	sent_idx = dhdp->sent_idx;
	skb = dhdp->skbbuf[sent_idx];

	if (skb == NULL) {
		dhd_os_rxfunlock(dhdp);
		DHD_ERROR(("dhd_rxf_dequeue: Dequeued packet is NULL, store idx %d sent idx %d\n",
			store_idx, sent_idx));
		return NULL;
	}

	dhdp->skbbuf[sent_idx] = NULL;
	dhdp->sent_idx = (sent_idx + 1) & (MAXSKBPEND - 1);

	DHD_TRACE(("dhd_rxf_dequeue: netif_rx_ni(%p), sent idx %d\n",
		skb, sent_idx));

	dhd_os_rxfunlock(dhdp);

	return skb;
}

int dhd_process_cid_mac(dhd_pub_t *dhdp, bool prepost)
{
#ifndef CUSTOMER_HW10
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;
#endif 

	if (prepost) { 
		dhd_read_macaddr(dhd);
	} else { 
		dhd_write_macaddr(&dhd->pub.mac);
	}

	return 0;
}

#if defined(PKT_FILTER_SUPPORT) && !defined(GAN_LITE_NAT_KEEPALIVE_FILTER)
static bool
_turn_on_arp_filter(dhd_pub_t *dhd, int op_mode)
{
	bool _apply = FALSE;
	
	if (op_mode & DHD_FLAG_IBSS_MODE) {
		_apply = TRUE;
		goto exit;
	}
	
	if ((dhd->arp_version == 1) &&
		(op_mode & (DHD_FLAG_P2P_GC_MODE | DHD_FLAG_P2P_GO_MODE))) {
		_apply = TRUE;
		goto exit;
	}

exit:
	return _apply;
}
#endif 

#if defined(CUSTOM_PLATFORM_NV_TEGRA)
#ifdef PKT_FILTER_SUPPORT
void
dhd_set_packet_filter_mode(struct net_device *dev, char *command)
{
	dhd_info_t *dhdi = *(dhd_info_t **)netdev_priv(dev);

	dhdi->pub.pkt_filter_mode = bcm_strtoul(command, &command, 0);
}

int
dhd_set_packet_filter_ports(struct net_device *dev, char *command)
{
	int i = 0, error = BCME_OK, count = 0, get_count = 0, action = 0;
	uint16 portnum = 0, *ports = NULL, get_ports[WL_PKT_FILTER_PORTS_MAX];
	dhd_info_t *dhdi = *(dhd_info_t **)netdev_priv(dev);
	dhd_pub_t *dhdp = &dhdi->pub;
	char iovbuf[WLC_IOCTL_SMLEN];

	
	action = bcm_strtoul(command, &command, 0);
	if (action > PKT_FILTER_PORTS_MAX)
		return BCME_BADARG;

	if (action == PKT_FILTER_PORTS_LOOPBACK) {
		
		bcm_mkiovar("cap", NULL, 0, iovbuf, sizeof(iovbuf));
		error = dhd_wl_ioctl_cmd(dhdp, WLC_GET_VAR, iovbuf, sizeof(iovbuf), FALSE, 0);
		if (error < 0) {
			DHD_ERROR(("%s: Get Capability failed (error=%d)\n", __FUNCTION__, error));
			return error;
		}

		if (strstr(iovbuf, "pktfltr2"))
			return bcm_strtoul(command, &command, 0);
		else {
			DHD_ERROR(("%s: pktfltr2 is not supported\n", __FUNCTION__));
			return BCME_UNSUPPORTED;
		}
	}

	if (action == PKT_FILTER_PORTS_CLEAR) {
		
		dhdp->pkt_filter_ports_count = 0;
		bzero(dhdp->pkt_filter_ports, sizeof(dhdp->pkt_filter_ports));
	}
	else {
		portnum = bcm_strtoul(command, &command, 0);
		if (portnum == 0) {
			
			return BCME_BADARG;
		}

		
		count = dhdp->pkt_filter_ports_count;
		ports = dhdp->pkt_filter_ports;

		if (action == PKT_FILTER_PORTS_ADD) {
			

			
			while ((portnum != 0) && (count < WL_PKT_FILTER_PORTS_MAX)) {
				for (i = 0; i < count; i++) {
					
					if (portnum == ports[i])
						break;
				}
				if (portnum != ports[i])
					ports[count++] = portnum;
				portnum = bcm_strtoul(command, &command, 0);
			}
		} else if ((action == PKT_FILTER_PORTS_DEL) && (count > 0)) {
			
			bcopy(dhdp->pkt_filter_ports, get_ports, count * sizeof(uint16));
			get_count = count;

			while (portnum != 0) {
				count = 0;
				for (i = 0; i < get_count; i++) {
					if (portnum != get_ports[i])
						ports[count++] = get_ports[i];
				}
				get_count = count;
				bcopy(ports, get_ports, count * sizeof(uint16));
				portnum = bcm_strtoul(command, &command, 0);
			}
		}
		dhdp->pkt_filter_ports_count = count;
	}
	return error;
}

static void
dhd_enable_packet_filter_ports(dhd_pub_t *dhd, bool enable)
{
	int error = 0;
	wl_pkt_filter_ports_t *portlist = NULL;
	const uint pkt_filter_ports_buf_len = sizeof("pkt_filter_ports")
		+ WL_PKT_FILTER_PORTS_FIXED_LEN	+ (WL_PKT_FILTER_PORTS_MAX * sizeof(uint16));
	char pkt_filter_ports_buf[pkt_filter_ports_buf_len];
	char iovbuf[pkt_filter_ports_buf_len];

	DHD_TRACE(("%s: enable %d, in_suspend %d, mode %d, port count %d\n", __FUNCTION__,
		enable, dhd->in_suspend, dhd->pkt_filter_mode,
		dhd->pkt_filter_ports_count));

	bzero(pkt_filter_ports_buf, sizeof(pkt_filter_ports_buf));
	portlist = (wl_pkt_filter_ports_t*)pkt_filter_ports_buf;
	portlist->version = WL_PKT_FILTER_PORTS_VERSION;
	portlist->reserved = 0;

	if (enable) {
		if (!(dhd->pkt_filter_mode & PKT_FILTER_MODE_PORTS_ONLY))
			return;

		
		dhd_master_mode |= PKT_FILTER_MODE_PORTS_ONLY;
		if (dhd->pkt_filter_mode & PKT_FILTER_MODE_FORWARD_ON_MATCH)
			
			dhd_master_mode |= PKT_FILTER_MODE_FORWARD_ON_MATCH;
		else
			
			dhd_master_mode &= ~PKT_FILTER_MODE_FORWARD_ON_MATCH;

		portlist->count = dhd->pkt_filter_ports_count;
		bcopy(dhd->pkt_filter_ports, portlist->ports,
			dhd->pkt_filter_ports_count * sizeof(uint16));
	} else {
		
		portlist->count = 0;
		dhd_master_mode &= ~PKT_FILTER_MODE_PORTS_ONLY;
		dhd_master_mode |= PKT_FILTER_MODE_FORWARD_ON_MATCH;
	}

	DHD_INFO(("%s: update: mode %d, port count %d\n", __FUNCTION__, dhd_master_mode,
		portlist->count));

	
	bcm_mkiovar("pkt_filter_ports",
		(char*)portlist,
		(WL_PKT_FILTER_PORTS_FIXED_LEN + (portlist->count * sizeof(uint16))),
		iovbuf, sizeof(iovbuf));
	error = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
	if (error < 0)
		DHD_ERROR(("%s: set pkt_filter_ports failed %d\n", __FUNCTION__, error));

	
	bcm_mkiovar("pkt_filter_mode", (char*)&dhd_master_mode,
		sizeof(dhd_master_mode), iovbuf, sizeof(iovbuf));
	error = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
	if (error < 0)
		DHD_ERROR(("%s: set pkt_filter_mode failed %d\n", __FUNCTION__, error));

	return;
}
#endif 
#endif 

void dhd_set_packet_filter(dhd_pub_t *dhd)
{
#ifdef PKT_FILTER_SUPPORT
	int i;

	DHD_TRACE(("%s: enter\n", __FUNCTION__));
	if (dhd_pkt_filter_enable) {
		for (i = 0; i < dhd->pktfilter_count; i++) {
			dhd_pktfilter_offload_set(dhd, dhd->pktfilter[i]);
		}
	}
#endif 
}

void dhd_enable_packet_filter(int value, dhd_pub_t *dhd)
{
#ifdef PKT_FILTER_SUPPORT
	int i;

#ifdef CUSTOMER_HW_ONE
	
	mutex_lock(&enable_pktfilter_mutex);
	if (!multicast_lock&&is_screen_off)
		value = 1;
	else
		value = 0;
	DHD_ERROR(("%s: enter, value = %d, multicast_lock = %d, is_screen_off= %d\n",
		__FUNCTION__, value, multicast_lock, is_screen_off));
	
#else
	DHD_TRACE(("%s: enter, value = %d\n", __FUNCTION__, value));
#endif

#if defined(CUSTOM_PLATFORM_NV_TEGRA)
	dhd_enable_packet_filter_ports(dhd, value);
#endif 

	
	
	if (dhd_pkt_filter_enable && (!value ||
	    (dhd_support_sta_mode(dhd) && !dhd->dhcp_in_progress)))
	    {
		for (i = 0; i < dhd->pktfilter_count; i++) {
#ifndef GAN_LITE_NAT_KEEPALIVE_FILTER
			if (value && (i == DHD_ARP_FILTER_NUM) &&
				!_turn_on_arp_filter(dhd, dhd->op_mode)) {
				DHD_TRACE(("Do not turn on ARP white list pkt filter:"
					"val %d, cnt %d, op_mode 0x%x\n",
					value, i, dhd->op_mode));
				continue;
			}
#endif 
			dhd_pktfilter_offload_enable(dhd, dhd->pktfilter[i],
				value, dhd_master_mode);
		}
	}
#ifdef CUSTOMER_HW_ONE
	
	mutex_unlock(&enable_pktfilter_mutex);
	
#endif
#endif 
}

static int dhd_set_suspend(int value, dhd_pub_t *dhd)
{
#ifndef SUPPORT_PM2_ONLY
	int power_mode = PM_MAX;
#endif 
	
#ifdef CUSTOMER_HW_ONE
	char iovbuf[WL_EVENTING_MASK_LEN+32];
	char eventmask[WL_EVENTING_MASK_LEN];
#else
	char iovbuf[32];
	int bcn_li_dtim = 0; 
#endif
#ifndef ENABLE_FW_ROAM_SUSPEND
	uint roamvar = 1;
#endif 
	uint nd_ra_filter = 0;
	int ret = 0;

	if (!dhd)
		return -ENODEV;


	DHD_TRACE(("%s: enter, value = %d in_suspend=%d\n",
		__FUNCTION__, value, dhd->in_suspend));

#ifndef CUSTOMER_HW_ONE
	dhd_suspend_lock(dhd);
#endif

#ifdef CUSTOM_SET_CPUCORE
	DHD_TRACE(("%s set cpucore(suspend%d)\n", __FUNCTION__, value));
	
	dhd_set_cpucore(dhd, TRUE);
#endif 

#ifdef CUSTOMER_HW_ONE
	
	is_screen_off = value;
	wl_android_set_screen_off(is_screen_off);

	DHD_ERROR(("%s: screen_off = %d allow_p2p_event = %d\n",
		__FUNCTION__, is_screen_off, dhd->allow_p2p_event));
	
	if (is_screen_off && !dhd->allow_p2p_event) {
		bcm_mkiovar("event_msgs", eventmask, WL_EVENTING_MASK_LEN, iovbuf, sizeof(iovbuf));
		if ((ret = dhd_wl_ioctl_cmd(dhd,
			WLC_GET_VAR, iovbuf, sizeof(iovbuf), FALSE, 0)) < 0) {
			DHD_ERROR(("%s read Event mask failed %d\n", __FUNCTION__, ret));
			goto exit;
		}
		bcopy(iovbuf, eventmask, WL_EVENTING_MASK_LEN);

		clrbit(eventmask, WLC_E_ACTION_FRAME_RX);
		
		
		
		clrbit(eventmask, WLC_E_P2P_DISC_LISTEN_COMPLETE);

		
		bcm_mkiovar("event_msgs", eventmask, WL_EVENTING_MASK_LEN, iovbuf, sizeof(iovbuf));
		if ((ret = dhd_wl_ioctl_cmd(dhd,
			WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0) {
			DHD_ERROR(("%s Set Event mask failed %d\n", __FUNCTION__, ret));
			goto exit;
		}
	} else {
		bcm_mkiovar("event_msgs", eventmask, WL_EVENTING_MASK_LEN, iovbuf, sizeof(iovbuf));
		if ((ret  = dhd_wl_ioctl_cmd(dhd,
			WLC_GET_VAR, iovbuf, sizeof(iovbuf), FALSE, 0)) < 0) {
			DHD_ERROR(("%s read Event mask failed %d\n", __FUNCTION__, ret));
			 goto exit;
		}
		bcopy(iovbuf, eventmask, WL_EVENTING_MASK_LEN);

		setbit(eventmask, WLC_E_ACTION_FRAME_RX);
		
		
		
		setbit(eventmask, WLC_E_P2P_DISC_LISTEN_COMPLETE);

		
		bcm_mkiovar("event_msgs", eventmask, WL_EVENTING_MASK_LEN, iovbuf, sizeof(iovbuf));
		if ((ret = dhd_wl_ioctl_cmd(dhd,
			WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0) {
			DHD_ERROR(("%s Set Event mask failed %d\n", __FUNCTION__, ret));
			goto exit;
		}
	}
#endif 

	if (dhd->up) {
		if (value && dhd->in_suspend) {
#ifdef PKT_FILTER_SUPPORT
				dhd->early_suspended = 1;
#endif
				
				DHD_ERROR(("%s: force extra Suspend setting \n", __FUNCTION__));

#ifndef SUPPORT_PM2_ONLY
				dhd_wl_ioctl_cmd(dhd, WLC_SET_PM, (char *)&power_mode,
				                 sizeof(power_mode), TRUE, 0);
#endif 

				
				dhd_enable_packet_filter(1, dhd);


#ifndef CUSTOMER_HW_ONE
				bcn_li_dtim = dhd_get_suspend_bcn_li_dtim(dhd);
				bcm_mkiovar("bcn_li_dtim", (char *)&bcn_li_dtim,
					4, iovbuf, sizeof(iovbuf));
				if (dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf),
					TRUE, 0) < 0)
					DHD_ERROR(("%s: set dtim failed\n", __FUNCTION__));
#endif 

#ifndef ENABLE_FW_ROAM_SUSPEND
				
				bcm_mkiovar("roam_off", (char *)&roamvar, 4,
					iovbuf, sizeof(iovbuf));
				dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
#endif 
				if (FW_SUPPORTED(dhd, ndoe)) {
					
					nd_ra_filter = 1;
					bcm_mkiovar("nd_ra_filter_enable", (char *)&nd_ra_filter, 4,
						iovbuf, sizeof(iovbuf));
					if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf,
						sizeof(iovbuf), TRUE, 0)) < 0)
						DHD_ERROR(("failed to set nd_ra_filter (%d)\n",
							ret));
				}
#ifdef CUSTOMER_HW_ONE
				
				ret = dhd_set_keepalive(1);
				if (ret < 0) {
					DHD_ERROR(("%s Set keepalive error %d\n",
						__FUNCTION__, ret));
					goto exit;
				}

				
				DHD_TRACE(("%s: force extra Suspend setting \n", __FUNCTION__));

				
				dhdhtc_set_power_control(0, DHDHTC_POWER_CTRL_BROWSER_LOAD_PAGE);
				ret = dhdhtc_update_wifi_power_mode(is_screen_off);
				if (ret < 0) {
					DHD_ERROR(("%s Set dhdhtc_update_wifi_power_mode"
						" error %d\n", __FUNCTION__, ret));
					goto exit;
				}
				
				DHD_ERROR(("Clear KDDI APK bit when screen off\n"));
				dhdhtc_set_power_control(0, DHDHTC_POWER_CTRL_KDDI_APK);
				ret = dhdhtc_update_wifi_power_mode(is_screen_off);
				if (ret < 0) {
					DHD_ERROR(("%s Set dhdhtc_update_wifi_power_mode"
						" error %d\n", __FUNCTION__, ret));
					goto exit;
				}
				ret = dhdhtc_update_dtim_listen_interval(is_screen_off);
				if (ret < 0) {
					DHD_ERROR(("%s Set dhdhtc_update_dtim_listen_interval"
						" error %d\n", __FUNCTION__, ret));
					goto exit;
				}
#endif 
			} else {
#ifdef PKT_FILTER_SUPPORT
				dhd->early_suspended = 0;
#endif
				
				DHD_ERROR(("%s: Remove extra suspend setting \n", __FUNCTION__));

#ifndef SUPPORT_PM2_ONLY
				power_mode = PM_FAST;
				dhd_wl_ioctl_cmd(dhd, WLC_SET_PM, (char *)&power_mode,
				                 sizeof(power_mode), TRUE, 0);
#endif 
#ifdef PKT_FILTER_SUPPORT
				
				dhd_enable_packet_filter(0, dhd);
#endif 

#ifndef CUSTOMER_HW_ONE
				
				bcm_mkiovar("bcn_li_dtim", (char *)&bcn_li_dtim,
					4, iovbuf, sizeof(iovbuf));

				dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
#endif 

#ifndef ENABLE_FW_ROAM_SUSPEND
				roamvar = dhd_roam_disable;
				bcm_mkiovar("roam_off", (char *)&roamvar, 4, iovbuf,
					sizeof(iovbuf));
				dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
#endif 
				if (FW_SUPPORTED(dhd, ndoe)) {
					
					nd_ra_filter = 0;
					bcm_mkiovar("nd_ra_filter_enable", (char *)&nd_ra_filter, 4,
						iovbuf, sizeof(iovbuf));
					if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf,
						sizeof(iovbuf), TRUE, 0)) < 0)
						DHD_ERROR(("failed to set nd_ra_filter (%d)\n",
							ret));
				}
#ifdef CUSTOMER_HW_ONE
				wl_cfg80211_dhd_chk_link();
				ret = dhdhtc_update_wifi_power_mode(is_screen_off);
				if (ret < 0) {
					DHD_ERROR(("%s Set dhdhtc_update_wifi_power_mode"
						" error %d\n", __FUNCTION__, ret));
					goto exit;
				}

				ret = dhdhtc_update_dtim_listen_interval(is_screen_off);
				if (ret < 0) {
					DHD_ERROR(("%s Set dhdhtc_update_dtim_listen_interval"
						" error %d\n", __FUNCTION__, ret));
					goto exit;
				}

				
				DHD_TRACE(("%s: Remove extra suspend setting \n", __FUNCTION__));
				
				ret = dhd_set_keepalive(0);
				if (ret < 0) {
					DHD_ERROR(("%s Set keepalive error %d\n",
						__FUNCTION__, ret));
					goto exit;
				}
#endif 
			}
	}
#ifndef CUSTOMER_HW_ONE
	dhd_suspend_unlock(dhd);
#endif

#ifdef CUSTOMER_HW_ONE
exit:
	if (ret < 0)
		DHD_ERROR(("%s cmd issue error leave\n", __FUNCTION__));
#endif

	return 0;
}

static int dhd_suspend_resume_helper(struct dhd_info *dhd, int val, int force)
{
	dhd_pub_t *dhdp = &dhd->pub;
	int ret = 0;

	DHD_OS_WAKE_LOCK(dhdp);
	DHD_PERIM_LOCK(dhdp);

	
	dhdp->in_suspend = val;
	if ((force || !dhdp->suspend_disable_flag) &&
		dhd_support_sta_mode(dhdp))
	{
		ret = dhd_set_suspend(val, dhdp);
	}

	DHD_PERIM_UNLOCK(dhdp);
	DHD_OS_WAKE_UNLOCK(dhdp);
	return ret;
}

#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(DHD_USE_EARLYSUSPEND)
static void dhd_early_suspend(struct early_suspend *h)
{
	struct dhd_info *dhd = container_of(h, struct dhd_info, early_suspend);
	DHD_TRACE_HW4(("%s: enter\n", __FUNCTION__));

#ifdef CUSTOMER_HW_ONE
	
	return;
#endif
	if (dhd)
		dhd_suspend_resume_helper(dhd, 1, 0);
}

static void dhd_late_resume(struct early_suspend *h)
{
	struct dhd_info *dhd = container_of(h, struct dhd_info, early_suspend);
	DHD_TRACE_HW4(("%s: enter\n", __FUNCTION__));

#ifdef CUSTOMER_HW_ONE
	
	return;
#endif
	if (dhd)
		dhd_suspend_resume_helper(dhd, 0, 0);
}
#endif 


void
dhd_timeout_start(dhd_timeout_t *tmo, uint usec)
{
	tmo->limit = usec;
	tmo->increment = 0;
	tmo->elapsed = 0;
	tmo->tick = jiffies_to_usecs(1);
}

int
dhd_timeout_expired(dhd_timeout_t *tmo)
{
	
	if (tmo->increment == 0) {
		tmo->increment = 1;
		return 0;
	}

	if (tmo->elapsed >= tmo->limit)
		return 1;

	
	tmo->elapsed += tmo->increment;

	if ((!CAN_SLEEP()) || tmo->increment < tmo->tick) {
		OSL_DELAY(tmo->increment);
		tmo->increment *= 2;
		if (tmo->increment > tmo->tick)
			tmo->increment = tmo->tick;
	} else {
		wait_queue_head_t delay_wait;
		DECLARE_WAITQUEUE(wait, current);
		init_waitqueue_head(&delay_wait);
		add_wait_queue(&delay_wait, &wait);
		set_current_state(TASK_INTERRUPTIBLE);
		(void)schedule_timeout(1);
		remove_wait_queue(&delay_wait, &wait);
		set_current_state(TASK_RUNNING);
	}

	return 0;
}

int
dhd_net2idx(dhd_info_t *dhd, struct net_device *net)
{
	int i = 0;

	if (!dhd) {
		DHD_ERROR(("%s : DHD_BAD_IF return\n", __FUNCTION__));
		return DHD_BAD_IF;
	}
	while (i < DHD_MAX_IFS) {
		if (dhd->iflist[i] && dhd->iflist[i]->net && (dhd->iflist[i]->net == net))
			return i;
		i++;
	}

	return DHD_BAD_IF;
}

struct net_device * dhd_idx2net(void *pub, int ifidx)
{
	struct dhd_pub *dhd_pub = (struct dhd_pub *)pub;
	struct dhd_info *dhd_info;

	if (!dhd_pub || ifidx < 0 || ifidx >= DHD_MAX_IFS)
		return NULL;
	dhd_info = dhd_pub->info;
	if (dhd_info && dhd_info->iflist[ifidx])
		return dhd_info->iflist[ifidx]->net;
	return NULL;
}

int
dhd_ifname2idx(dhd_info_t *dhd, char *name)
{
	int i = DHD_MAX_IFS;

	ASSERT(dhd);

	if (name == NULL || *name == '\0')
		return 0;

	while (--i > 0)
		if (dhd->iflist[i] && !strncmp(dhd->iflist[i]->name, name, IFNAMSIZ))
				break;

	DHD_TRACE(("%s: return idx %d for \"%s\"\n", __FUNCTION__, i, name));

	return i;	
}

int
dhd_ifidx2hostidx(dhd_info_t *dhd, int ifidx)
{
	int i = DHD_MAX_IFS;

	ASSERT(dhd);

	while (--i > 0)
		if (dhd->iflist[i] && (dhd->iflist[i]->idx == ifidx))
				break;

	DHD_TRACE(("%s: return hostidx %d for ifidx %d\n", __FUNCTION__, i, ifidx));

	return i;	
}

char *
dhd_ifname(dhd_pub_t *dhdp, int ifidx)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;

	ASSERT(dhd);

	if (ifidx < 0 || ifidx >= DHD_MAX_IFS) {
		DHD_ERROR(("%s: ifidx %d out of range\n", __FUNCTION__, ifidx));
		return "<if_bad>";
	}

	if (dhd->iflist[ifidx] == NULL) {
		DHD_ERROR(("%s: null i/f %d\n", __FUNCTION__, ifidx));
		return "<if_null>";
	}

	if (dhd->iflist[ifidx]->net)
		return dhd->iflist[ifidx]->net->name;

	return "<if_none>";
}

uint8 *
dhd_bssidx2bssid(dhd_pub_t *dhdp, int idx)
{
	int i;
	dhd_info_t *dhd = (dhd_info_t *)dhdp;

	ASSERT(dhd);
	for (i = 0; i < DHD_MAX_IFS; i++)
	if (dhd->iflist[i] && dhd->iflist[i]->bssidx == idx)
		return dhd->iflist[i]->mac_addr;

	return NULL;
}


static void
_dhd_set_multicast_list(dhd_info_t *dhd, int ifidx)
{
	struct net_device *dev;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
	struct netdev_hw_addr *ha;
#else
	struct dev_mc_list *mclist;
#endif
	uint32 allmulti, cnt;

	wl_ioctl_t ioc;
	char *buf, *bufp;
	uint buflen;
	int ret;

			ASSERT(dhd && dhd->iflist[ifidx]);
			dev = dhd->iflist[ifidx]->net;
			if (!dev)
				return;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
			netif_addr_lock_bh(dev);
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
			cnt = netdev_mc_count(dev);
#else
			cnt = dev->mc_count;
#endif 

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
			netif_addr_unlock_bh(dev);
#endif

			
	allmulti = (dev->flags & IFF_ALLMULTI) ? TRUE : FALSE;

	


	buflen = sizeof("mcast_list") + sizeof(cnt) + (cnt * ETHER_ADDR_LEN);
	if (!(bufp = buf = MALLOC(dhd->pub.osh, buflen))) {
		DHD_ERROR(("%s: out of memory for mcast_list, cnt %d\n",
		           dhd_ifname(&dhd->pub, ifidx), cnt));
		return;
	}

	strncpy(bufp, "mcast_list", buflen - 1);
	bufp[buflen - 1] = '\0';
	bufp += strlen("mcast_list") + 1;

	cnt = htol32(cnt);
	memcpy(bufp, &cnt, sizeof(cnt));
	bufp += sizeof(cnt);


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
			netif_addr_lock_bh(dev);
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
			netdev_for_each_mc_addr(ha, dev) {
				if (!cnt)
					break;
				memcpy(bufp, ha->addr, ETHER_ADDR_LEN);
				bufp += ETHER_ADDR_LEN;
				cnt--;
	}
#else
	for (mclist = dev->mc_list; (mclist && (cnt > 0));
		cnt--, mclist = mclist->next) {
				memcpy(bufp, (void *)mclist->dmi_addr, ETHER_ADDR_LEN);
				bufp += ETHER_ADDR_LEN;
			}
#endif 

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
			netif_addr_unlock_bh(dev);
#endif

	memset(&ioc, 0, sizeof(ioc));
	ioc.cmd = WLC_SET_VAR;
	ioc.buf = buf;
	ioc.len = buflen;
	ioc.set = TRUE;

	ret = dhd_wl_ioctl(&dhd->pub, ifidx, &ioc, ioc.buf, ioc.len);
	if (ret < 0) {
		DHD_ERROR(("%s: set mcast_list failed, cnt %d\n",
			dhd_ifname(&dhd->pub, ifidx), cnt));
		allmulti = cnt ? TRUE : allmulti;
	}

	MFREE(dhd->pub.osh, buf, buflen);


	buflen = sizeof("allmulti") + sizeof(allmulti);
	if (!(buf = MALLOC(dhd->pub.osh, buflen))) {
		DHD_ERROR(("%s: out of memory for allmulti\n", dhd_ifname(&dhd->pub, ifidx)));
		return;
	}
	allmulti = htol32(allmulti);

	if (!bcm_mkiovar("allmulti", (void*)&allmulti, sizeof(allmulti), buf, buflen)) {
		DHD_ERROR(("%s: mkiovar failed for allmulti, datalen %d buflen %u\n",
		           dhd_ifname(&dhd->pub, ifidx), (int)sizeof(allmulti), buflen));
		MFREE(dhd->pub.osh, buf, buflen);
		return;
	}


	memset(&ioc, 0, sizeof(ioc));
	ioc.cmd = WLC_SET_VAR;
	ioc.buf = buf;
	ioc.len = buflen;
	ioc.set = TRUE;

	ret = dhd_wl_ioctl(&dhd->pub, ifidx, &ioc, ioc.buf, ioc.len);
	if (ret < 0) {
		DHD_ERROR(("%s: set allmulti %d failed\n",
		           dhd_ifname(&dhd->pub, ifidx), ltoh32(allmulti)));
	}

	MFREE(dhd->pub.osh, buf, buflen);

	

	allmulti = (dev->flags & IFF_PROMISC) ? TRUE : FALSE;

	allmulti = htol32(allmulti);

	memset(&ioc, 0, sizeof(ioc));
	ioc.cmd = WLC_SET_PROMISC;
	ioc.buf = &allmulti;
	ioc.len = sizeof(allmulti);
	ioc.set = TRUE;

	ret = dhd_wl_ioctl(&dhd->pub, ifidx, &ioc, ioc.buf, ioc.len);
	if (ret < 0) {
		DHD_ERROR(("%s: set promisc %d failed\n",
		           dhd_ifname(&dhd->pub, ifidx), ltoh32(allmulti)));
	}
}

int
_dhd_set_mac_address(dhd_info_t *dhd, int ifidx, uint8 *addr)
{
	char buf[32];
	wl_ioctl_t ioc;
	int ret;

#ifdef CUSTOMER_HW_ONE
	if (dhd->pub.os_stopped) {
		DHD_ERROR(("%s: interface stopped.\n", __FUNCTION__));
		return -1;
	}
#endif
	if (!bcm_mkiovar("cur_etheraddr", (char*)addr, ETHER_ADDR_LEN, buf, 32)) {
		DHD_ERROR(("%s: mkiovar failed for cur_etheraddr\n", dhd_ifname(&dhd->pub, ifidx)));
		return -1;
	}
	memset(&ioc, 0, sizeof(ioc));
	ioc.cmd = WLC_SET_VAR;
	ioc.buf = buf;
	ioc.len = 32;
	ioc.set = TRUE;

	ret = dhd_wl_ioctl(&dhd->pub, ifidx, &ioc, ioc.buf, ioc.len);
	if (ret < 0) {
		DHD_ERROR(("%s: set cur_etheraddr failed\n", dhd_ifname(&dhd->pub, ifidx)));
	} else {
		memcpy(dhd->iflist[ifidx]->net->dev_addr, addr, ETHER_ADDR_LEN);
		if (ifidx == 0)
			memcpy(dhd->pub.mac.octet, addr, ETHER_ADDR_LEN);
	}

	return ret;
}

#if defined(SOFTAP) && !defined(CUSTOMER_HW_ONE)
#ifdef CONFIG_WIRELESS_EXT
extern struct net_device *ap_net_dev;
#else
struct net_device *ap_net_dev = NULL;
#endif
extern tsk_ctl_t ap_eth_ctl; 
#endif 

static void
dhd_ifadd_event_handler(void *handle, void *event_info, u8 event)
{
	dhd_info_t *dhd = handle;
	dhd_if_event_t *if_event = event_info;
	struct net_device *ndev;
	int ifidx, bssidx;
	int ret;
#if 1 && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0))
	struct wireless_dev *vwdev, *primary_wdev;
	struct net_device *primary_ndev;
#endif 

	if (event != DHD_WQ_WORK_IF_ADD) {
		DHD_ERROR(("%s: unexpected event \n", __FUNCTION__));
		return;
	}

	if (!dhd) {
		DHD_ERROR(("%s: dhd info not available \n", __FUNCTION__));
		return;
	}

	if (!if_event) {
		DHD_ERROR(("%s: event data is null \n", __FUNCTION__));
		return;
	}

	dhd_net_if_lock_local(dhd);
	DHD_OS_WAKE_LOCK(&dhd->pub);
	DHD_PERIM_LOCK(&dhd->pub);

	ifidx = if_event->event.ifidx;
	bssidx = if_event->event.bssidx;
	DHD_TRACE(("%s: registering if with ifidx %d\n", __FUNCTION__, ifidx));

	ndev = dhd_allocate_if(&dhd->pub, ifidx, if_event->name,
		if_event->mac, bssidx, TRUE);
	if (!ndev) {
		DHD_ERROR(("%s: net device alloc failed  \n", __FUNCTION__));
		goto done;
	}

#if 1 && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0))
	vwdev = kzalloc(sizeof(*vwdev), GFP_KERNEL);
	if (unlikely(!vwdev)) {
		WL_ERR(("Could not allocate wireless device\n"));
		goto done;
	}
	primary_ndev = dhd->pub.info->iflist[0]->net;
	primary_wdev = ndev_to_wdev(primary_ndev);
	vwdev->wiphy = primary_wdev->wiphy;
	vwdev->iftype = if_event->event.role;
	vwdev->netdev = ndev;
	ndev->ieee80211_ptr = vwdev;
	SET_NETDEV_DEV(ndev, wiphy_dev(vwdev->wiphy));
	DHD_ERROR(("virtual interface(%s) is created\n", if_event->name));
#endif 

	DHD_PERIM_UNLOCK(&dhd->pub);
	ret = dhd_register_if(&dhd->pub, ifidx, TRUE);
	DHD_PERIM_LOCK(&dhd->pub);
	if (ret != BCME_OK) {
		DHD_ERROR(("%s: dhd_register_if failed\n", __FUNCTION__));
		dhd_remove_if(&dhd->pub, ifidx, TRUE);
		goto done;
	}
#ifdef PCIE_FULL_DONGLE
	
	if (FW_SUPPORTED((&dhd->pub), ap) && !(DHD_IF_ROLE_STA(if_event->event.role))) {
		char iovbuf[WLC_IOCTL_SMLEN];
		uint32 var_int =  1;

		memset(iovbuf, 0, sizeof(iovbuf));
		bcm_mkiovar("ap_isolate", (char *)&var_int, 4, iovbuf, sizeof(iovbuf));
		ret = dhd_wl_ioctl_cmd(&dhd->pub, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, ifidx);

		if (ret != BCME_OK) {
			DHD_ERROR(("%s: Failed to set ap_isolate to dongle\n", __FUNCTION__));
			dhd_remove_if(&dhd->pub, ifidx, TRUE);
		}
	}
#endif 
done:
	MFREE(dhd->pub.osh, if_event, sizeof(dhd_if_event_t));

	DHD_PERIM_UNLOCK(&dhd->pub);
	DHD_OS_WAKE_UNLOCK(&dhd->pub);
	dhd_net_if_unlock_local(dhd);
}

static void
dhd_ifdel_event_handler(void *handle, void *event_info, u8 event)
{
	dhd_info_t *dhd = handle;
	int ifidx;
	dhd_if_event_t *if_event = event_info;


	if (event != DHD_WQ_WORK_IF_DEL) {
		DHD_ERROR(("%s: unexpected event \n", __FUNCTION__));
		return;
	}

	if (!dhd) {
		DHD_ERROR(("%s: dhd info not available \n", __FUNCTION__));
		return;
	}

	if (!if_event) {
		DHD_ERROR(("%s: event data is null \n", __FUNCTION__));
		return;
	}

	dhd_net_if_lock_local(dhd);
	DHD_OS_WAKE_LOCK(&dhd->pub);
	DHD_PERIM_LOCK(&dhd->pub);

	ifidx = if_event->event.ifidx;
	DHD_TRACE(("Removing interface with idx %d\n", ifidx));

	dhd_remove_if(&dhd->pub, ifidx, TRUE);

	MFREE(dhd->pub.osh, if_event, sizeof(dhd_if_event_t));

	DHD_PERIM_UNLOCK(&dhd->pub);
	DHD_OS_WAKE_UNLOCK(&dhd->pub);
	dhd_net_if_unlock_local(dhd);
}

static void
dhd_set_mac_addr_handler(void *handle, void *event_info, u8 event)
{
	dhd_info_t *dhd = handle;
	dhd_if_t *ifp = event_info;

	if (event != DHD_WQ_WORK_SET_MAC) {
		DHD_ERROR(("%s: unexpected event \n", __FUNCTION__));
	}

	if (!dhd) {
		DHD_ERROR(("%s: dhd info not available \n", __FUNCTION__));
		return;
	}

	dhd_net_if_lock_local(dhd);
	DHD_OS_WAKE_LOCK(&dhd->pub);
	DHD_PERIM_LOCK(&dhd->pub);

#if defined(SOFTAP) && !defined(CUSTOMER_HW_ONE)
	{
		unsigned long flags;
		bool in_ap = FALSE;
		DHD_GENERAL_LOCK(&dhd->pub, flags);
		in_ap = (ap_net_dev != NULL);
		DHD_GENERAL_UNLOCK(&dhd->pub, flags);

		if (in_ap)  {
			DHD_ERROR(("attempt to set MAC for %s in AP Mode, blocked. \n",
			           ifp->net->name));
			goto done;
		}
	}
#endif 

	if (ifp == NULL || !dhd->pub.up) {
		DHD_ERROR(("%s: interface info not available/down \n", __FUNCTION__));
		goto done;
	}

	DHD_ERROR(("%s: MACID is overwritten\n", __FUNCTION__));
	ifp->set_macaddress = FALSE;
	if (_dhd_set_mac_address(dhd, ifp->idx, ifp->mac_addr) == 0)
		DHD_INFO(("%s: MACID is overwritten\n",	__FUNCTION__));
	else
		DHD_ERROR(("%s: _dhd_set_mac_address() failed\n", __FUNCTION__));

done:
	DHD_PERIM_UNLOCK(&dhd->pub);
	DHD_OS_WAKE_UNLOCK(&dhd->pub);
	dhd_net_if_unlock_local(dhd);
}

static void
dhd_set_mcast_list_handler(void *handle, void *event_info, u8 event)
{
	dhd_info_t *dhd = handle;
	dhd_if_t *ifp = event_info;
	int ifidx;

	if (event != DHD_WQ_WORK_SET_MCAST_LIST) {
		DHD_ERROR(("%s: unexpected event \n", __FUNCTION__));
		return;
	}

	if (!dhd) {
		DHD_ERROR(("%s: dhd info not available \n", __FUNCTION__));
		return;
	}

	dhd_net_if_lock_local(dhd);
	DHD_OS_WAKE_LOCK(&dhd->pub);
	DHD_PERIM_LOCK(&dhd->pub);

#if defined(SOFTAP) && !defined(CUSTOMER_HW_ONE)
	{
		bool in_ap = FALSE;
		unsigned long flags;
		DHD_GENERAL_LOCK(&dhd->pub, flags);
		in_ap = (ap_net_dev != NULL);
		DHD_GENERAL_UNLOCK(&dhd->pub, flags);

		if (in_ap)  {
			DHD_ERROR(("set MULTICAST list for %s in AP Mode, blocked. \n",
			           ifp->net->name));
			ifp->set_multicast = FALSE;
			goto done;
		}
	}
#endif 

	if (ifp == NULL || !dhd->pub.up) {
		DHD_ERROR(("%s: interface info not available/down \n", __FUNCTION__));
		goto done;
	}

	ifidx = ifp->idx;


	_dhd_set_multicast_list(dhd, ifidx);
	DHD_INFO(("%s: set multicast list for if %d\n", __FUNCTION__, ifidx));

done:
	DHD_PERIM_UNLOCK(&dhd->pub);
	DHD_OS_WAKE_UNLOCK(&dhd->pub);
	dhd_net_if_unlock_local(dhd);
}

static int
dhd_set_mac_address(struct net_device *dev, void *addr)
{
	int ret = 0;

	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	struct sockaddr *sa = (struct sockaddr *)addr;
	int ifidx;
	dhd_if_t *dhdif;

#ifdef CUSTOMER_HW_ONE
	DHD_ERROR(("enter %s\n", __func__));

	if (!dhd->pub.up || (dhd->pub.busstate == DHD_BUS_DOWN)) {
		DHD_ERROR(("%s: dhd is down. skip it.\n", __func__));
		return -ENODEV;
	}
#endif
	ifidx = dhd_net2idx(dhd, dev);
	if (ifidx == DHD_BAD_IF)
		return -1;

	dhdif = dhd->iflist[ifidx];

	dhd_net_if_lock_local(dhd);
	memcpy(dhdif->mac_addr, sa->sa_data, ETHER_ADDR_LEN);
	dhdif->set_macaddress = TRUE;
	dhd_net_if_unlock_local(dhd);
	dhd_deferred_schedule_work(dhd->dhd_deferred_wq, (void *)dhdif, DHD_WQ_WORK_SET_MAC,
		dhd_set_mac_addr_handler, DHD_WORK_PRIORITY_LOW);
	return ret;
}

static void
dhd_set_multicast_list(struct net_device *dev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	int ifidx;

#ifdef CUSTOMER_HW_ONE
	DHD_ERROR(("enter %s\n", __func__));

	if (!dhd->pub.up || (dhd->pub.busstate == DHD_BUS_DOWN) || dhd->pub.os_stopped) {
		DHD_ERROR(("%s: dhd is down, os_stopped[%d]. skip it.\n",
			__FUNCTION__, dhd->pub.os_stopped));
		return;
	}
#endif
	ifidx = dhd_net2idx(dhd, dev);
	if (ifidx == DHD_BAD_IF)
		return;

	dhd->iflist[ifidx]->set_multicast = TRUE;
	dhd_deferred_schedule_work(dhd->dhd_deferred_wq, (void *)dhd->iflist[ifidx],
		DHD_WQ_WORK_SET_MCAST_LIST, dhd_set_mcast_list_handler, DHD_WORK_PRIORITY_LOW);
}

#ifdef PROP_TXSTATUS
int
dhd_os_wlfc_block(dhd_pub_t *pub)
{
	dhd_info_t *di = (dhd_info_t *)(pub->info);
	ASSERT(di != NULL);
	spin_lock_bh(&di->wlfc_spinlock);
	return 1;
}

int
dhd_os_wlfc_unblock(dhd_pub_t *pub)
{
	dhd_info_t *di = (dhd_info_t *)(pub->info);

	ASSERT(di != NULL);
	spin_unlock_bh(&di->wlfc_spinlock);
	return 1;
}

#endif 

#if defined(DHD_8021X_DUMP) || defined(DHD_TX_DUMP)
void
dhd_tx_dump(osl_t *osh, void *pkt)
{
	uint8 *dump_data;
	uint16 protocol;

	dump_data = PKTDATA(osh, pkt);
	protocol = (dump_data[12] << 8) | dump_data[13];

	DHD_ERROR(("TX DUMP - %s\n", _get_packet_type_str(protocol)));
	if (protocol == ETHER_TYPE_802_1X) {
		DHD_ERROR(("ETHER_TYPE_802_1X [TX]: ver %d, type %d, replay %d\n",
			dump_data[14], dump_data[15], dump_data[30]));
	}

	if (dump_data[0] == 0xFF) {
		DHD_ERROR(("%s: BROADCAST\n", __FUNCTION__));

		if ((dump_data[12] == 8) &&
			(dump_data[13] == 6)) {
			DHD_ERROR(("%s: ARP %d\n",
				__FUNCTION__, dump_data[0x15]));
		}
	} else if (dump_data[0] & 1) {
		DHD_ERROR(("%s: MULTICAST: " MACDBG "\n",
			__FUNCTION__, MAC2STRDBG(&dump_data[0])));
	} else {
		DHD_ERROR(("%s: UNICAST: " MACDBG "\n",
			__FUNCTION__, MAC2STRDBG(&dump_data[0])));
	}
}
#endif 

int BCMFASTPATH
dhd_sendpkt(dhd_pub_t *dhdp, int ifidx, void *pktbuf)
{
	int ret = BCME_OK;
	dhd_info_t *dhd = (dhd_info_t *)(dhdp->info);
	struct ether_header *eh = NULL;

	
	if (!dhdp->up || (dhdp->busstate == DHD_BUS_DOWN)) {
		
		PKTFREE(dhdp->osh, pktbuf, TRUE);
		return -ENODEV;
	}

#ifdef PCIE_FULL_DONGLE
	if (dhdp->busstate == DHD_BUS_SUSPEND) {
		DHD_ERROR(("%s : pcie is still in suspend state!!\n", __FUNCTION__));
		PKTFREE(dhdp->osh, pktbuf, TRUE);
		return -EBUSY;
	}
#endif 

#ifdef DHD_UNICAST_DHCP
	
	
	if (dhdp->dhcp_unicast) {
	    dhd_convert_dhcp_broadcast_ack_to_unicast(dhdp, pktbuf, ifidx);
	}
#endif 
#ifdef DHD_L2_FILTER
	
	if (dhdp->trace_ping) {
		if (dhd_l2_filter_block_ping(dhdp, pktbuf, ifidx) == BCME_OK) {
			
		}
	}
#endif
	
	if (PKTLEN(dhdp->osh, pktbuf) >= ETHER_HDR_LEN) {
		uint8 *pktdata = (uint8 *)PKTDATA(dhdp->osh, pktbuf);
		eh = (struct ether_header *)pktdata;

		if (ETHER_ISMULTI(eh->ether_dhost))
			dhdp->tx_multicast++;
		if (ntoh16(eh->ether_type) == ETHER_TYPE_802_1X)
			atomic_inc(&dhd->pend_8021x_cnt);
	} else {
			PKTFREE(dhd->pub.osh, pktbuf, TRUE);
			return BCME_ERROR;
	}

	
#ifndef PKTPRIO_OVERRIDE
	if (PKTPRIO(pktbuf) == 0)
#endif 
		pktsetprio(pktbuf, FALSE);


#if defined(PCIE_FULL_DONGLE) && !defined(PCIE_TX_DEFERRAL)
	ret = dhd_flowid_update(dhdp, ifidx, dhdp->flow_prio_map[(PKTPRIO(pktbuf))], pktbuf);
	if (ret != BCME_OK) {
		PKTCFREE(dhd->pub.osh, pktbuf, TRUE);
		return ret;
	}
#endif

#ifdef PROP_TXSTATUS
	if (dhd_wlfc_is_supported(dhdp)) {
		
		DHD_PKTTAG_SETIF(PKTTAG(pktbuf), ifidx);

		
		DHD_PKTTAG_SETDSTN(PKTTAG(pktbuf), eh->ether_dhost);

		
		if (ETHER_ISMULTI(eh->ether_dhost))
			
			DHD_PKTTAG_SETFIFO(PKTTAG(pktbuf), AC_COUNT);
		else
			DHD_PKTTAG_SETFIFO(PKTTAG(pktbuf), WME_PRIO2AC(PKTPRIO(pktbuf)));
	} else
#endif 
	
	dhd_prot_hdrpush(dhdp, ifidx, pktbuf);

	
#ifdef WLMEDIA_HTSF
	dhd_htsf_addtxts(dhdp, pktbuf);
#endif
#if defined(DHD_8021X_DUMP) || defined(DHD_TX_DUMP)
#if defined(DHD_TX_DUMP)
	if (dhd_tx_dump_flag) {
#endif
		dhd_tx_dump(dhdp->osh, pktbuf);
#if defined(DHD_TX_DUMP)
		dhd_tx_dump_flag = 0;
	}
#endif
#endif 
#ifdef PROP_TXSTATUS
	{
		if (dhd_wlfc_commit_packets(dhdp, (f_commitpkt_t)dhd_bus_txdata,
			dhdp->bus, pktbuf, TRUE) == WLFC_UNSUPPORTED) {
			
#ifdef BCMPCIE
			ret = dhd_bus_txdata(dhdp->bus, pktbuf, (uint8)ifidx);
#else
			ret = dhd_bus_txdata(dhdp->bus, pktbuf);
#endif 
		}
	}
#else
#ifdef BCMPCIE
	ret = dhd_bus_txdata(dhdp->bus, pktbuf, (uint8)ifidx);
#else
	ret = dhd_bus_txdata(dhdp->bus, pktbuf);
#endif 
#endif 

	return ret;
}

int BCMFASTPATH
dhd_start_xmit(struct sk_buff *skb, struct net_device *net)
{
	int ret;
	uint datalen;
	void *pktbuf;
	dhd_info_t *dhd = DHD_DEV_INFO(net);
	dhd_if_t *ifp = NULL;
	int ifidx;
	unsigned long flags;
#ifdef WLMEDIA_HTSF
	uint8 htsfdlystat_sz = dhd->pub.htsfdlystat_sz;
#else
	uint8 htsfdlystat_sz = 0;
#endif
#ifdef DHD_WMF
	struct ether_header *eh;
	uint8 *iph;
#endif 

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	DHD_GENERAL_LOCK(&dhd->pub, flags);
	dhd->pub.tx_in_progress = TRUE;
#ifdef PCIE_FULL_DONGLE
	if (dhd->pub.busstate == DHD_BUS_SUSPEND) {
		DHD_ERROR(("%s : pcie is still in suspend state!!\n", __FUNCTION__));
		dev_kfree_skb(skb);
		ifp = DHD_DEV_IFP(net);
		ifp->stats.tx_dropped++;
		dhd->pub.tx_dropped++;
		dhd->pub.tx_in_progress = FALSE;
		DHD_GENERAL_UNLOCK(&dhd->pub, flags);
		return -EBUSY;
	}
#endif 
	DHD_GENERAL_UNLOCK(&dhd->pub, flags);
	DHD_OS_WAKE_LOCK(&dhd->pub);
	DHD_PERIM_LOCK_TRY(DHD_FWDER_UNIT(dhd), TRUE);

	DHD_GENERAL_LOCK(&dhd->pub, flags);
	
#ifdef CUSTOMER_HW_ONE
	if (dhd->pub.busstate == DHD_BUS_DOWN || dhd->pub.hang_was_sent || dhd->pub.os_stopped) {
#else
	if (dhd->pub.busstate == DHD_BUS_DOWN || dhd->pub.hang_was_sent) {
#endif
		DHD_ERROR(("%s: xmit rejected pub.up=%d busstate=%d \n",
			__FUNCTION__, dhd->pub.up, dhd->pub.busstate));
		netif_stop_queue(net);
		
		if (dhd->pub.up) {
            if (otp_write)
			    DHD_ERROR(("%s: Event HANG sent up\n", __FUNCTION__));
            else
                DHD_ERROR(("%s: Event HANG but OTP is empty\n", __FUNCTION__));
			net_os_send_hang_message(net);
		}
		dhd->pub.tx_in_progress = FALSE;
		DHD_GENERAL_UNLOCK(&dhd->pub, flags);
		DHD_PERIM_UNLOCK_TRY(DHD_FWDER_UNIT(dhd), TRUE);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 20))
		return -ENODEV;
#else
		return NETDEV_TX_BUSY;
#endif
	}
	DHD_GENERAL_UNLOCK(&dhd->pub, flags);

	ifp = DHD_DEV_IFP(net);
	ifidx = DHD_DEV_IFIDX(net);

	ASSERT(ifidx == dhd_net2idx(dhd, net));
	ASSERT((ifp != NULL) && (ifp == dhd->iflist[ifidx]));

	if (ifidx == DHD_BAD_IF) {
		DHD_ERROR(("%s: bad ifidx %d\n", __FUNCTION__, ifidx));
		netif_stop_queue(net);
		DHD_GENERAL_LOCK(&dhd->pub, flags);
		dhd->pub.tx_in_progress = FALSE;
		DHD_GENERAL_UNLOCK(&dhd->pub, flags);
		DHD_PERIM_UNLOCK_TRY(DHD_FWDER_UNIT(dhd), TRUE);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 20))
		return -ENODEV;
#else
		return NETDEV_TX_BUSY;
#endif
	}

	
	if (((unsigned long)(skb->data)) & 0x1) {
		unsigned char *data = skb->data;
		uint32 length = skb->len;
		PKTPUSH(dhd->pub.osh, skb, 1);
		memmove(skb->data, data, length);
		PKTSETLEN(dhd->pub.osh, skb, length);
	}

	datalen  = PKTLEN(dhd->pub.osh, skb);

	

	if (skb_headroom(skb) < dhd->pub.hdrlen + htsfdlystat_sz) {
		struct sk_buff *skb2;

		DHD_INFO(("%s: insufficient headroom\n",
		          dhd_ifname(&dhd->pub, ifidx)));
		dhd->pub.tx_realloc++;

		skb2 = skb_realloc_headroom(skb, dhd->pub.hdrlen + htsfdlystat_sz);

		dev_kfree_skb(skb);
		if ((skb = skb2) == NULL) {
			DHD_ERROR(("%s: skb_realloc_headroom failed\n",
			           dhd_ifname(&dhd->pub, ifidx)));
			ret = -ENOMEM;
			goto done;
		}
	}

	
	if (!(pktbuf = PKTFRMNATIVE(dhd->pub.osh, skb))) {
		DHD_ERROR(("%s: PKTFRMNATIVE failed\n",
		           dhd_ifname(&dhd->pub, ifidx)));
		dev_kfree_skb_any(skb);
		ret = -ENOMEM;
		goto done;
	}
#ifdef WLMEDIA_HTSF
	if (htsfdlystat_sz && PKTLEN(dhd->pub.osh, pktbuf) >= ETHER_ADDR_LEN) {
		uint8 *pktdata = (uint8 *)PKTDATA(dhd->pub.osh, pktbuf);
		struct ether_header *eh = (struct ether_header *)pktdata;

		if (!ETHER_ISMULTI(eh->ether_dhost) &&
			(ntoh16(eh->ether_type) == ETHER_TYPE_IP)) {
			eh->ether_type = hton16(ETHER_TYPE_BRCM_PKTDLYSTATS);
		}
	}
#endif
#ifdef DHD_WMF
	eh = (struct ether_header *)PKTDATA(dhd->pub.osh, pktbuf);
	iph = (uint8 *)eh + ETHER_HDR_LEN;

	if (ifp->wmf.wmf_enable && (ntoh16(eh->ether_type) == ETHER_TYPE_IP) &&
		(IP_VER(iph) == IP_VER_4) && (ETHER_ISMULTI(eh->ether_dhost) ||
		((IPV4_PROT(iph) == IP_PROT_IGMP) && dhd->pub.wmf_ucast_igmp))) {
#if defined(DHD_IGMP_UCQUERY) || defined(DHD_UCAST_UPNP)
		void *sdu_clone;
		bool ucast_convert = FALSE;
#ifdef DHD_UCAST_UPNP
		uint32 dest_ip;

		dest_ip = ntoh32(*((uint32 *)(iph + IPV4_DEST_IP_OFFSET)));
		ucast_convert = dhd->pub.wmf_ucast_upnp && MCAST_ADDR_UPNP_SSDP(dest_ip);
#endif 
#ifdef DHD_IGMP_UCQUERY
		ucast_convert |= dhd->pub.wmf_ucast_igmp_query &&
			(IPV4_PROT(iph) == IP_PROT_IGMP) &&
			(*(iph + IPV4_HLEN(iph)) == IGMPV2_HOST_MEMBERSHIP_QUERY);
#endif 
		if (ucast_convert) {
			dhd_sta_t *sta;
			unsigned long flags;

			DHD_IF_STA_LIST_LOCK(ifp, flags);

			
			list_for_each_entry(sta, &ifp->sta_list, list) {
				if ((sdu_clone = PKTDUP(dhd->pub.osh, pktbuf)) == NULL) {
					DHD_IF_STA_LIST_UNLOCK(ifp, flags);
					DHD_GENERAL_LOCK(&dhd->pub, flags);
					dhd->pub.tx_in_progress = FALSE;
					DHD_GENERAL_UNLOCK(&dhd->pub, flags);
					DHD_PERIM_UNLOCK_TRY(DHD_FWDER_UNIT(dhd), TRUE);
					DHD_OS_WAKE_UNLOCK(&dhd->pub);
					return (WMF_NOP);
				}
				dhd_wmf_forward(ifp->wmf.wmfh, sdu_clone, 0, sta, 1);
			}

			DHD_IF_STA_LIST_UNLOCK(ifp, flags);
			DHD_GENERAL_LOCK(&dhd->pub, flags);
			dhd->pub.tx_in_progress = FALSE;
			DHD_GENERAL_UNLOCK(&dhd->pub, flags);
			DHD_PERIM_UNLOCK_TRY(DHD_FWDER_UNIT(dhd), TRUE);
			DHD_OS_WAKE_UNLOCK(&dhd->pub);

			PKTFREE(dhd->pub.osh, pktbuf, TRUE);
			return NETDEV_TX_OK;
		} else
#endif 
		{
			ret = dhd_wmf_packets_handle(&dhd->pub, pktbuf, NULL, ifidx, 0);
			switch (ret) {
			case WMF_TAKEN:
			case WMF_DROP:
				DHD_GENERAL_LOCK(&dhd->pub, flags);
				dhd->pub.tx_in_progress = FALSE;
				DHD_GENERAL_UNLOCK(&dhd->pub, flags);
				DHD_PERIM_UNLOCK_TRY(DHD_FWDER_UNIT(dhd), TRUE);
				DHD_OS_WAKE_UNLOCK(&dhd->pub);
				return NETDEV_TX_OK;
			default:
				
				break;
			}
		}
	}
#endif 

#ifdef DHDTCPACK_SUPPRESS
	if (dhd->pub.tcpack_sup_mode == TCPACK_SUP_HOLD) {
		
		if (dhd_tcpack_hold(&dhd->pub, pktbuf, ifidx)) {
			ret = 0;
			goto done;
		}
	} else {
		
		if (dhd_tcpack_suppress(&dhd->pub, pktbuf)) {
			ret = 0;
			goto done;
		}
	}
#endif 

	ret = dhd_sendpkt(&dhd->pub, ifidx, pktbuf);

#ifdef CUSTOMER_HW_ONE
	
	if (ret == BCME_NORESOURCE) {
		txq_full_event_num++;

		
		if (txq_full_event_num >= MAX_TXQ_FULL_EVENT) {
			txq_full_event_num = 0;
			net_os_send_hang_message(net);
		}
	}
	else {
		txq_full_event_num = 0;
	}
#endif
done:
	if (ret) {
		ifp->stats.tx_dropped++;
		dhd->pub.tx_dropped++;
	}
	else {

#ifdef PROP_TXSTATUS
		
		if (!dhd_wlfc_is_supported(&dhd->pub))
#endif
		{
			dhd->pub.tx_packets++;
			ifp->stats.tx_packets++;
			ifp->stats.tx_bytes += datalen;
		}
	}

	DHD_GENERAL_LOCK(&dhd->pub, flags);
	dhd->pub.tx_in_progress = FALSE;
	DHD_GENERAL_UNLOCK(&dhd->pub, flags);
	DHD_PERIM_UNLOCK_TRY(DHD_FWDER_UNIT(dhd), TRUE);
	DHD_OS_WAKE_UNLOCK(&dhd->pub);

	
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 20))
	return 0;
#else
	return NETDEV_TX_OK;
#endif
}


void
dhd_txflowcontrol(dhd_pub_t *dhdp, int ifidx, bool state)
{
	struct net_device *net;
	dhd_info_t *dhd = dhdp->info;
	int i;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	ASSERT(dhd);

	if (ifidx == ALL_INTERFACES) {
		
		dhdp->txoff = state;
		for (i = 0; i < DHD_MAX_IFS; i++) {
			if (dhd->iflist[i]) {
				net = dhd->iflist[i]->net;
				if (state == ON)
					netif_stop_queue(net);
				else
					netif_wake_queue(net);
			}
		}
	}
	else {
		if (dhd->iflist[ifidx]) {
			net = dhd->iflist[ifidx]->net;
			if (state == ON)
				netif_stop_queue(net);
			else
				netif_wake_queue(net);
		}
	}
}

#if defined(DHD_RX_DUMP) || defined(DHD_TX_DUMP)
typedef struct {
	uint16 type;
	const char *str;
} PKTTYPE_INFO;

static const PKTTYPE_INFO packet_type_info[] =
{
	{ ETHER_TYPE_IP, "IP" },
	{ ETHER_TYPE_ARP, "ARP" },
	{ ETHER_TYPE_BRCM, "BRCM" },
	{ ETHER_TYPE_802_1X, "802.1X" },
	{ ETHER_TYPE_WAI, "WAPI" },
	{ 0, ""}
};

static const char *_get_packet_type_str(uint16 type)
{
	int i;
	int n = sizeof(packet_type_info)/sizeof(packet_type_info[1]) - 1;

	for (i = 0; i < n; i++) {
		if (packet_type_info[i].type == type)
			return packet_type_info[i].str;
	}

	return packet_type_info[n].str;
}
#endif 


#ifdef DHD_WMF
bool
dhd_is_rxthread_enabled(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd = dhdp->info;

	return dhd->rxthread_enabled;
}
#endif 

void
dhd_rx_frame(dhd_pub_t *dhdp, int ifidx, void *pktbuf, int numpkt, uint8 chan)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;
	struct sk_buff *skb;
	uchar *eth;
	uint len;
	void *data = NULL, *pnext = NULL;
	int i;
	dhd_if_t *ifp;
	wl_event_msg_t event;
	int tout_rx = 0;
	int tout_ctrl = 0;
	void *skbhead = NULL;
	void *skbprev = NULL;
#if defined(DHD_RX_DUMP) || defined(DHD_8021X_DUMP)
	char *dump_data;
	uint16 protocol;
#endif 

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

#ifdef CUSTOMER_HW_ONE
	memset(&event, 0, sizeof(event));
	if (dhdp->os_stopped) {
		for (i = 0; pktbuf && i < numpkt; i++, pktbuf = pnext) {
			pnext = PKTNEXT(dhdp->osh, pktbuf);
			PKTSETNEXT(dhdp->osh, pktbuf, NULL);
			skb = PKTTONATIVE(dhdp->osh, pktbuf);
#if defined(CONFIG_DHD_USE_STATIC_BUF) && defined(DHD_USE_STATIC_IOCTLBUF)
			if (skb && (skb->mac_len == PREALLOC_USED_MAGIC)) {
				PKTFREE_STATIC(dhdp->osh, skb, FALSE);
			} else
#endif
			PKTFREE(dhdp->osh, skb, FALSE);
		}
		DHD_ERROR(("%s: module removed. skip rx frame\n", __FUNCTION__));
		return;
	}
#endif 
	for (i = 0; pktbuf && i < numpkt; i++, pktbuf = pnext) {
		struct ether_header *eh;
#ifdef WLBTAMP
		struct dot11_llc_snap_header *lsh;
#endif

		pnext = PKTNEXT(dhdp->osh, pktbuf);
		PKTSETNEXT(dhdp->osh, pktbuf, NULL);

		ifp = dhd->iflist[ifidx];
		if (ifp == NULL) {
			DHD_ERROR(("%s: ifp is NULL. drop packet\n",
				__FUNCTION__));
			PKTCFREE(dhdp->osh, pktbuf, FALSE);
			continue;
		}

		eh = (struct ether_header *)PKTDATA(dhdp->osh, pktbuf);

		
#ifndef PROP_TXSTATUS_VSDB
		if ((!ifp->net || ifp->net->reg_state != NETREG_REGISTERED) &&
			(ntoh16(eh->ether_type) != ETHER_TYPE_BRCM)) {
#else
		if ((!ifp->net || ifp->net->reg_state != NETREG_REGISTERED || !dhd->pub.up) &&
			(ntoh16(eh->ether_type) != ETHER_TYPE_BRCM)) {
#endif 
			DHD_ERROR(("%s: net device is NOT registered yet. drop packet\n",
			__FUNCTION__));
			PKTCFREE(dhdp->osh, pktbuf, FALSE);
			continue;
		}

#ifdef WLBTAMP
		lsh = (struct dot11_llc_snap_header *)&eh[1];

		if ((ntoh16(eh->ether_type) < ETHER_TYPE_MIN) &&
		    (PKTLEN(dhdp->osh, pktbuf) >= RFC1042_HDR_LEN) &&
		    bcmp(lsh, BT_SIG_SNAP_MPROT, DOT11_LLC_SNAP_HDR_LEN - 2) == 0 &&
		    lsh->type == HTON16(BTA_PROT_L2CAP)) {
			amp_hci_ACL_data_t *ACL_data = (amp_hci_ACL_data_t *)
			        ((uint8 *)eh + RFC1042_HDR_LEN);
			ACL_data = NULL;
		}
#endif 

#ifdef PROP_TXSTATUS
		if (dhd_wlfc_is_header_only_pkt(dhdp, pktbuf)) {
			PKTCFREE(dhdp->osh, pktbuf, FALSE);
			continue;
		}
#endif
#ifdef DHD_L2_FILTER
		
		if (dhdp->block_ping) {
			if (dhd_l2_filter_block_ping(dhdp, pktbuf, ifidx) == BCME_OK) {
				PKTFREE(dhdp->osh, pktbuf, FALSE);
				continue;
			}
		} else if (dhdp->trace_ping) {
			if (dhd_l2_filter_block_ping(dhdp, pktbuf, ifidx) == BCME_OK) {
				
			}
		}
#endif
#ifdef DHD_WMF
		
		if (ifp->wmf.wmf_enable && (ETHER_ISMULTI(eh->ether_dhost))) {
			dhd_sta_t *sta;
			int ret;

			sta = dhd_find_sta(dhdp, ifidx, (void *)eh->ether_shost);
			ret = dhd_wmf_packets_handle(dhdp, pktbuf, sta, ifidx, 1);
			switch (ret) {
				case WMF_TAKEN:
					
					continue;
				case WMF_DROP:
					
					DHD_ERROR(("%s: WMF decides to drop packet\n",
						__FUNCTION__));
					PKTCFREE(dhdp->osh, pktbuf, FALSE);
					continue;
				default:
					
					break;
			}
		}
#endif 
#ifdef DHDTCPACK_SUPPRESS
		dhd_tcpdata_info_get(dhdp, pktbuf);
#endif
		skb = PKTTONATIVE(dhdp->osh, pktbuf);

		ifp = dhd->iflist[ifidx];
		if (ifp == NULL)
			ifp = dhd->iflist[0];

		ASSERT(ifp);
		skb->dev = ifp->net;

#ifdef PCIE_FULL_DONGLE
		if ((DHD_IF_ROLE_AP(dhdp, ifidx) || DHD_IF_ROLE_P2PGO(dhdp, ifidx)) &&
			(!ifp->ap_isolate)) {
			eh = (struct ether_header *)PKTDATA(dhdp->osh, pktbuf);
			if (ETHER_ISUCAST(eh->ether_dhost)) {
				if (dhd_find_sta(dhdp, ifidx, (void *)eh->ether_dhost)) {
					dhd_sendpkt(dhdp, ifidx, pktbuf);
					continue;
				}
			} else {
				void *npktbuf = PKTDUP(dhdp->osh, pktbuf);
				dhd_sendpkt(dhdp, ifidx, npktbuf);
			}
		}
#endif 

		eth = skb->data;
		len = skb->len;

#if defined(DHD_RX_DUMP) || defined(DHD_8021X_DUMP)
	if (dhd_rx_dump_flag) {
		dump_data = skb->data;
		protocol = (dump_data[12] << 8) | dump_data[13];

		if (protocol == ETHER_TYPE_802_1X) {
			DHD_ERROR(("ETHER_TYPE_802_1X [RX]: "
				"ver %d, type %d, replay %d\n",
				dump_data[14], dump_data[15],
				dump_data[30]));
		}
#endif 
#if defined(DHD_RX_DUMP)
		DHD_ERROR(("RX DUMP - %s\n", _get_packet_type_str(protocol)));
		if (protocol != ETHER_TYPE_BRCM) {
			if (dump_data[0] == 0xFF) {
				DHD_ERROR(("%s: BROADCAST\n", __FUNCTION__));

				if ((dump_data[12] == 8) &&
					(dump_data[13] == 6)) {
					DHD_ERROR(("%s: ARP %d\n",
						__FUNCTION__, dump_data[0x15]));
				}
			} else if (dump_data[0] & 1) {
				DHD_ERROR(("%s: MULTICAST: " MACDBG "\n",
					__FUNCTION__, MAC2STRDBG(&dump_data[6])));
			
			
			} else {
				DHD_ERROR(("%s: UNICAST: " MACDBG "\n",
					__FUNCTION__, MAC2STRDBG(&dump_data[6])));
			}
			
#ifdef DHD_RX_FULL_DUMP
			{
				int k;
				for (k = 0; k < skb->len; k++) {
					DHD_ERROR(("%02X ", dump_data[k]));
					if ((k & 15) == 15)
						DHD_ERROR(("\n"));
				}
				DHD_ERROR(("\n"));
			}
#endif 
		
		
		} else {
			dhd_event_dump = 1;
		
		}
#endif 
#if defined(DHD_RX_DUMP) || defined(DHD_8021X_DUMP)
		dhd_rx_dump_flag = 0;
	}
#endif

		skb->protocol = eth_type_trans(skb, skb->dev);

		if (skb->pkt_type == PACKET_MULTICAST) {
			dhd->pub.rx_multicast++;
			ifp->stats.multicast++;
		}

		skb->data = eth;
		skb->len = len;

#ifdef WLMEDIA_HTSF
		dhd_htsf_addrxts(dhdp, pktbuf);
#endif
		
		skb_pull(skb, ETH_HLEN);

		
		memset(&event, 0, sizeof(event));
		if (ntoh16(skb->protocol) == ETHER_TYPE_BRCM) {
			dhd_wl_host_event(dhd, &ifidx,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)
			skb_mac_header(skb),
#else
			skb->mac.raw,
#endif 
			&event,
			&data);

			wl_event_to_host_order(&event);
			if (!tout_ctrl)
				tout_ctrl = DHD_PACKET_TIMEOUT_MS;
#ifdef DHD_RX_DUMP
			if (dhd_event_dump) {
				DHD_ERROR(("%s: the first event type after resume=%d\n",
					__FUNCTION__, event.event_type));
				dhd_event_dump = 0;
			}
#endif
#ifdef WLBTAMP
			if (event.event_type == WLC_E_BTA_HCI_EVENT) {
#ifdef CUSTOMER_HW_ONE
				if (!data) {
					DHD_ERROR(("[HTCKW] dhd_rx_frame: data= NULL\n"));
				} else
#endif
				dhd_bta_doevt(dhdp, data, event.datalen);
			}
#endif 

#if defined(PNO_SUPPORT)
			if (event.event_type == WLC_E_PFN_NET_FOUND) {
				
				tout_ctrl = CUSTOM_PNO_EVENT_LOCK_xTIME * DHD_PACKET_TIMEOUT_MS;
			}
#endif 

#ifdef DHD_DONOT_FORWARD_BCMEVENT_AS_NETWORK_PKT
#if defined(CONFIG_DHD_USE_STATIC_BUF) && defined(DHD_USE_STATIC_IOCTLBUF)
			PKTFREE_STATIC(dhdp->osh, pktbuf, FALSE);
#else
			PKTFREE(dhdp->osh, pktbuf, FALSE);
#endif 
			continue;
#endif 
		} else {
			tout_rx = DHD_PACKET_TIMEOUT_MS;

#ifdef PROP_TXSTATUS
			dhd_wlfc_save_rxpath_ac_time(dhdp, (uint8)PKTPRIO(skb));
#endif 
		}

		ASSERT(ifidx < DHD_MAX_IFS && dhd->iflist[ifidx]);
		ifp = dhd->iflist[ifidx];

		if (ifp->net)
			ifp->net->last_rx = jiffies;

		if (ntoh16(skb->protocol) != ETHER_TYPE_BRCM) {
			dhdp->dstats.rx_bytes += skb->len;
			dhdp->rx_packets++; 
			ifp->stats.rx_bytes += skb->len;
			ifp->stats.rx_packets++;
		}
#if defined(DHD_TCP_WINSIZE_ADJUST)
		if (dhd_use_tcp_window_size_adjust) {
			if (ifidx == 0 && ntoh16(skb->protocol) == ETHER_TYPE_IP) {
				dhd_adjust_tcp_winsize(dhdp->op_mode, skb);
			}
		}
#endif 

		if (in_interrupt()) {
			netif_rx(skb);
		} else {
			if (dhd->rxthread_enabled) {
				if (!skbhead)
					skbhead = skb;
				else
					PKTSETNEXT(dhdp->osh, skbprev, skb);
				skbprev = skb;
			} else {

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
				netif_rx_ni(skb);
#else
				ulong flags;
				netif_rx(skb);
				local_irq_save(flags);
				RAISE_RX_SOFTIRQ();
				local_irq_restore(flags);
#endif 
			}
		}
	}

	if (dhd->rxthread_enabled && skbhead)
		dhd_sched_rxf(dhdp, skbhead);

	DHD_OS_WAKE_LOCK_RX_TIMEOUT_ENABLE(dhdp, tout_rx);
	DHD_OS_WAKE_LOCK_CTRL_TIMEOUT_ENABLE(dhdp, tout_ctrl);
}

void
dhd_event(struct dhd_info *dhd, char *evpkt, int evlen, int ifidx)
{
	
	return;
}

void
dhd_txcomplete(dhd_pub_t *dhdp, void *txp, bool success)
{
	dhd_info_t *dhd = (dhd_info_t *)(dhdp->info);
	struct ether_header *eh;
	uint16 type;
#ifdef WLBTAMP
	uint len;
#endif

	dhd_prot_hdrpull(dhdp, NULL, txp, NULL, NULL);

	eh = (struct ether_header *)PKTDATA(dhdp->osh, txp);
	type  = ntoh16(eh->ether_type);

	if (type == ETHER_TYPE_802_1X)
		atomic_dec(&dhd->pend_8021x_cnt);

#ifdef WLBTAMP
	len = PKTLEN(dhdp->osh, txp);

	
	if ((type < ETHER_TYPE_MIN) && (len >= RFC1042_HDR_LEN)) {
		struct dot11_llc_snap_header *lsh = (struct dot11_llc_snap_header *)&eh[1];

		if (bcmp(lsh, BT_SIG_SNAP_MPROT, DOT11_LLC_SNAP_HDR_LEN - 2) == 0 &&
		    ntoh16(lsh->type) == BTA_PROT_L2CAP) {

			dhd_bta_tx_hcidata_complete(dhdp, txp, success);
		}
	}
#endif 
#ifdef PROP_TXSTATUS
	if (dhdp->wlfc_state && (dhdp->proptxstatus_mode != WLFC_FCMODE_NONE)) {
		dhd_if_t *ifp = dhd->iflist[DHD_PKTTAG_IF(PKTTAG(txp))];
		uint datalen  = PKTLEN(dhd->pub.osh, txp);

		if (success) {
			dhd->pub.tx_packets++;
			ifp->stats.tx_packets++;
			ifp->stats.tx_bytes += datalen;
		} else {
			ifp->stats.tx_dropped++;
		}
	}
#endif
}

static struct net_device_stats *
dhd_get_stats(struct net_device *net)
{
	dhd_info_t *dhd = DHD_DEV_INFO(net);
	dhd_if_t *ifp;
	int ifidx;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	ifidx = dhd_net2idx(dhd, net);
	if (ifidx == DHD_BAD_IF) {
		DHD_ERROR(("%s: BAD_IF\n", __FUNCTION__));

		memset(&net->stats, 0, sizeof(net->stats));
		return &net->stats;
	}

	ifp = dhd->iflist[ifidx];
	ASSERT(dhd && ifp);

	if (dhd->pub.up) {
#ifdef CUSTOMER_HW_ONE
		if (dhd->pub.os_stopped) {
			DHD_ERROR(("%s: module removed. return old value. ifp=%p, dhd=%p\n",
				__FUNCTION__, ifp, dhd));
		} else
#endif
		
		dhd_prot_dstats(&dhd->pub);
	}
	return &ifp->stats;
}

#if defined(USE_STATIC_MEMDUMP)
unsigned long long
dhd_get_memdump_file_size(void)
{
	struct kstat stat;
	int ret;
	struct path p;

#ifdef CUSTOMER_HW_ONE
	ret = kern_path("/data/ramdump/brcm_mem_dump", 0, &p);
#else
	ret = kern_path("/data/mem_dump", 0, &p);
#endif
	if (ret != 0) {
		DHD_INFO(("%s: Failed to fstat file, ret=%d\n",
			__FUNCTION__, ret));
		return -1;
	}
	vfs_getattr(&p, &stat);
	return stat.size;
}
#endif 

static int
dhd_watchdog_thread(void *data)
{
	tsk_ctl_t *tsk = (tsk_ctl_t *)data;
	dhd_info_t *dhd = (dhd_info_t *)tsk->parent;
#if defined(USE_STATIC_MEMDUMP)
	int fsize = dhd_get_memdump_file_size();

	if (fsize >= MEMDUMP_MINSIZE) {
		file_cnt = -1;
		DHD_ERROR(("%s: DHD already memdump %d bytes\n",
			__FUNCTION__, fsize));
	} else {
		file_cnt = 0;
	}
#endif 

	if (dhd_watchdog_prio > 0) {
		struct sched_param param;
		param.sched_priority = (dhd_watchdog_prio < MAX_RT_PRIO)?
			dhd_watchdog_prio:(MAX_RT_PRIO-1);
		setScheduler(current, SCHED_FIFO, &param);
	}

	while (1)
		if (down_interruptible (&tsk->sema) == 0) {
			unsigned long flags;
			unsigned long jiffies_at_start = jiffies;
			unsigned long time_lapse;

#ifdef CUSTOMER_HW_ONE
			if (dhd->dhd_force_exit == TRUE)
				break;
#endif

#if defined(USE_STATIC_MEMDUMP)
			if (file_cnt == MAX_RETRY) {
				fsize = dhd_get_memdump_file_size();
				if (fsize >= MEMDUMP_MINSIZE) {
					DHD_ERROR(("DHD : write completed!!!\n"));
					
					dump_stack();
				}
				file_cnt = 0;
			} else if (file_cnt >= 0) {
				file_cnt ++;
			}
#endif 

			SMP_RD_BARRIER_DEPENDS();
			if (tsk->terminated) {
				break;
			}

			if (dhd->pub.dongle_reset == FALSE) {
				DHD_TIMER(("%s:\n", __FUNCTION__));

				
				dhd_bus_watchdog(&dhd->pub);


				DHD_GENERAL_LOCK(&dhd->pub, flags);
				
				dhd->pub.tickcnt++;
				time_lapse = jiffies - jiffies_at_start;

				
				if (dhd->wd_timer_valid)
					mod_timer(&dhd->timer,
					    jiffies +
					    msecs_to_jiffies(dhd_watchdog_ms) -
					    min(msecs_to_jiffies(dhd_watchdog_ms), time_lapse));
					DHD_GENERAL_UNLOCK(&dhd->pub, flags);
			}
		} else {
			break;
	}

	complete_and_exit(&tsk->completed, 0);
}

static void dhd_watchdog(ulong data)
{
	dhd_info_t *dhd = (dhd_info_t *)data;
	unsigned long flags;

	if (dhd->pub.dongle_reset) {
		return;
	}

	if (dhd->thr_wdt_ctl.thr_pid >= 0) {
		up(&dhd->thr_wdt_ctl.sema);
		return;
#ifdef CUSTOMER_HW_ONE
	} else {
		DHD_ERROR(("watch_dog thr stopped, ignore\n"));
		return;
#endif
	}

	
	dhd_bus_watchdog(&dhd->pub);

	DHD_GENERAL_LOCK(&dhd->pub, flags);
	
	dhd->pub.tickcnt++;

	
	if (dhd->wd_timer_valid)
		mod_timer(&dhd->timer, jiffies + msecs_to_jiffies(dhd_watchdog_ms));
	DHD_GENERAL_UNLOCK(&dhd->pub, flags);

}

#ifdef ENABLE_ADAPTIVE_SCHED
static void
dhd_sched_policy(int prio)
{
	struct sched_param param;
	if (cpufreq_quick_get(0) <= CUSTOM_CPUFREQ_THRESH) {
		param.sched_priority = 0;
		setScheduler(current, SCHED_NORMAL, &param);
	} else {
		if (get_scheduler_policy(current) != SCHED_FIFO) {
			param.sched_priority = (prio < MAX_RT_PRIO)? prio : (MAX_RT_PRIO-1);
			setScheduler(current, SCHED_FIFO, &param);
		}
	}
}
#endif 
#ifdef DEBUG_CPU_FREQ
static int dhd_cpufreq_notifier(struct notifier_block *nb, unsigned long val, void *data)
{
	dhd_info_t *dhd = container_of(nb, struct dhd_info, freq_trans);
	struct cpufreq_freqs *freq = data;
	if (dhd) {
		if (!dhd->new_freq)
			goto exit;
		if (val == CPUFREQ_POSTCHANGE) {
			DHD_ERROR(("cpu freq is changed to %u kHZ on CPU %d\n",
				freq->new, freq->cpu));
			*per_cpu_ptr(dhd->new_freq, freq->cpu) = freq->new;
		}
	}
exit:
	return 0;
}
#endif 
static int
dhd_dpc_thread(void *data)
{
	tsk_ctl_t *tsk = (tsk_ctl_t *)data;
	dhd_info_t *dhd = (dhd_info_t *)tsk->parent;
#ifdef CUSTOMER_HW_ONE
	unsigned long pet_dog_start_time = 0;
	unsigned long start_time = 0;
#endif

	if (dhd_dpc_prio > 0)
	{
		struct sched_param param;
		param.sched_priority = (dhd_dpc_prio < MAX_RT_PRIO)?dhd_dpc_prio:(MAX_RT_PRIO-1);
		setScheduler(current, SCHED_FIFO, &param);
	}

#ifdef CUSTOM_DPC_CPUCORE
	set_cpus_allowed_ptr(current, cpumask_of(CUSTOM_DPC_CPUCORE));
#endif
#ifdef CUSTOM_SET_CPUCORE
	dhd->pub.current_dpc = current;
#endif 
	
	while (1) {
#ifdef CUSTOMER_HW_ONE
		if (time_after(jiffies, pet_dog_start_time + 3*HZ) && rt_class(dhd_dpc_prio)) {
			DHD_ERROR(("dhd_bus_dpc(): kick dog!\n"));
			
			pet_dog_start_time = jiffies;
		}
		if (prev_wlan_ioprio_idle != wlan_ioprio_idle) {
			set_wlan_ioprio();
			prev_wlan_ioprio_idle = wlan_ioprio_idle;
		}
#endif
		if (!binary_sema_down(tsk)) {
#ifdef ENABLE_ADAPTIVE_SCHED
			dhd_sched_policy(dhd_dpc_prio);
#endif 
#ifdef CUSTOMER_HW_ONE
			adjust_thread_priority();
			if (dhd->dhd_force_exit == TRUE)
				break;
#endif
			SMP_RD_BARRIER_DEPENDS();
			if (tsk->terminated) {
				break;
			}

			
			if (dhd->pub.busstate != DHD_BUS_DOWN) {
#ifdef CUSTOMER_HW_ONE
				int cpu_core = 0;
				int ret = 0;
#endif
				dhd_os_wd_timer_extend(&dhd->pub, TRUE);
#ifdef CUSTOMER_HW_ONE
				start_time = jiffies;
#endif
				while (dhd_bus_dpc(dhd->pub.bus)) {
					
#ifdef CUSTOMER_HW_ONE
					if (time_after(jiffies, start_time + 3*HZ) &&
						rt_class(dhd_dpc_prio)) {
						DHD_ERROR(("dhd_bus_dpc is busy in"
							" real time priority over 3 secs!\n"));
						if (cpu_core == 0) {
							ret = set_cpus_allowed_ptr(current,
								cpumask_of(nr_cpu_ids-1));
							if (ret) {
								DHD_ERROR(("set_cpus_allowed_ptr to"
									" 3 failed ret=%d\n", ret));
							} else {
								cpu_core = nr_cpu_ids - 1;
								DHD_ERROR(("switch to cpu %d over"
									" 3 secs.\n", cpu_core));
							}
						}
						start_time = jiffies;
					}
#endif 
				}
#ifdef CUSTOMER_HW_ONE
				if (cpu_core != 0) {
					ret = set_cpus_allowed_ptr(current, cpumask_of(0));
					if (ret)
						DHD_ERROR(("set_cpus_allowed_ptr back to 0 failed,"
							" ret=%d\n", ret));
					else
						DHD_ERROR(("switch task back to cpu 0.\n"));
				}
#endif
				dhd_os_wd_timer_extend(&dhd->pub, FALSE);
				DHD_OS_WAKE_UNLOCK(&dhd->pub);

			} else {
				if (dhd->pub.up)
					dhd_bus_stop(dhd->pub.bus, TRUE);
				DHD_OS_WAKE_UNLOCK(&dhd->pub);
			}
		}
		else
			break;
	}
	complete_and_exit(&tsk->completed, 0);
}

static int
dhd_rxf_thread(void *data)
{
	tsk_ctl_t *tsk = (tsk_ctl_t *)data;
	dhd_info_t *dhd = (dhd_info_t *)tsk->parent;
#if defined(WAIT_DEQUEUE)
#define RXF_WATCHDOG_TIME 250 
	ulong watchdogTime = OSL_SYSUPTIME(); 
#endif
	dhd_pub_t *pub = &dhd->pub;

	if (dhd_rxf_prio > 0)
	{
		struct sched_param param;
		param.sched_priority = (dhd_rxf_prio < MAX_RT_PRIO)?dhd_rxf_prio:(MAX_RT_PRIO-1);
		setScheduler(current, SCHED_FIFO, &param);
	}

	DAEMONIZE("dhd_rxf");
	

	
	complete(&tsk->completed);
#ifdef CUSTOM_SET_CPUCORE
	dhd->pub.current_rxf = current;
#endif 
	
	while (1) {
		if (down_interruptible(&tsk->sema) == 0) {
			void *skb;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0)
			ulong flags;
#endif
#ifdef ENABLE_ADAPTIVE_SCHED
			dhd_sched_policy(dhd_rxf_prio);
#endif 
#ifdef CUSTOMER_HW_ONE
			adjust_rxf_thread_priority();
#endif

			SMP_RD_BARRIER_DEPENDS();

			if (tsk->terminated) {
				break;
			}
			skb = dhd_rxf_dequeue(pub);

			if (skb == NULL) {
				continue;
			}
			while (skb) {
				void *skbnext = PKTNEXT(pub->osh, skb);
				PKTSETNEXT(pub->osh, skb, NULL);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
				netif_rx_ni(skb);
#else
				netif_rx(skb);
				local_irq_save(flags);
				RAISE_RX_SOFTIRQ();
				local_irq_restore(flags);

#endif
				skb = skbnext;
			}
#if defined(WAIT_DEQUEUE)
			if (OSL_SYSUPTIME() - watchdogTime > RXF_WATCHDOG_TIME) {
				OSL_SLEEP(1);
				watchdogTime = OSL_SYSUPTIME();
			}
#endif

			DHD_OS_WAKE_UNLOCK(pub);
		}
		else
			break;
	}
	complete_and_exit(&tsk->completed, 0);
}

#ifdef BCMPCIE
void dhd_dpc_kill(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd;

	if (!dhdp)
		return;

	dhd = dhdp->info;

	if (!dhd)
		return;

	tasklet_kill(&dhd->tasklet);
	DHD_ERROR(("%s: tasklet disabled\n", __FUNCTION__));
}
#endif 

static void
dhd_dpc(ulong data)
{
	dhd_info_t *dhd;
	int cpu = smp_processor_id();

	dhd = (dhd_info_t *)data;

	dhd->pub.irq_cpu_count[cpu]++;

	
	if (dhd->pub.busstate != DHD_BUS_DOWN) {
#if defined(CUSTOMER_HW_ONE) && defined(BCMPCIE)
	isresched = dhd_bus_dpc(dhd->pub.bus);
	if (isresched)
#else
		if (dhd_bus_dpc(dhd->pub.bus))
#endif 
			tasklet_schedule(&dhd->tasklet);
		else
			DHD_OS_WAKE_UNLOCK(&dhd->pub);
	} else {
		dhd_bus_stop(dhd->pub.bus, TRUE);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
	}
}

void
dhd_sched_dpc(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;

	DHD_INTR(("%s\n", __FUNCTION__));
#if defined(CUSTOMER_HW_ONE) && defined(BCMPCIE)
	if (dhd->thr_dpc_ctl.thr_pid < 0)
		if (!test_bit(TASKLET_STATE_SCHED, &dhd->tasklet.state) && !isresched)
#endif 
	DHD_OS_WAKE_LOCK(dhdp);
	if (dhd->thr_dpc_ctl.thr_pid >= 0) {
		if (!binary_sema_up(&dhd->thr_dpc_ctl))
			DHD_OS_WAKE_UNLOCK(dhdp);
		return;
	} else {
		tasklet_schedule(&dhd->tasklet);
	}
}

static void
dhd_sched_rxf(dhd_pub_t *dhdp, void *skb)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;
#ifdef RXF_DEQUEUE_ON_BUSY
	int ret = BCME_OK;
	int retry = 2;
#endif 

	DHD_OS_WAKE_LOCK(dhdp);

	DHD_TRACE(("dhd_sched_rxf: Enter\n"));
#ifdef RXF_DEQUEUE_ON_BUSY
	do {
		ret = dhd_rxf_enqueue(dhdp, skb);
		if (ret == BCME_OK || ret == BCME_ERROR)
			break;
		else
			OSL_SLEEP(50); 
	} while (retry-- > 0);

	if (retry <= 0 && ret == BCME_BUSY) {
		void *skbp = skb;

		while (skbp) {
			void *skbnext = PKTNEXT(dhdp->osh, skbp);
			PKTSETNEXT(dhdp->osh, skbp, NULL);
			netif_rx_ni(skbp);
			skbp = skbnext;
		}
		DHD_ERROR(("send skb to kernel backlog without rxf_thread\n"));
	}
	else {
		if (dhd->thr_rxf_ctl.thr_pid >= 0) {
			up(&dhd->thr_rxf_ctl.sema);
		}
	}
#else 
	do {
		if (dhd_rxf_enqueue(dhdp, skb) == BCME_OK)
			break;
	} while (1);
	if (dhd->thr_rxf_ctl.thr_pid >= 0) {
		up(&dhd->thr_rxf_ctl.sema);
	}
	return;
#endif 
}

#ifdef TOE
static int
dhd_toe_get(dhd_info_t *dhd, int ifidx, uint32 *toe_ol)
{
	wl_ioctl_t ioc;
	char buf[32];
	int ret;

	memset(&ioc, 0, sizeof(ioc));

	ioc.cmd = WLC_GET_VAR;
	ioc.buf = buf;
	ioc.len = (uint)sizeof(buf);
	ioc.set = FALSE;

	strncpy(buf, "toe_ol", sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	if ((ret = dhd_wl_ioctl(&dhd->pub, ifidx, &ioc, ioc.buf, ioc.len)) < 0) {
		
		if (ret == -EIO) {
			DHD_ERROR(("%s: toe not supported by device\n",
				dhd_ifname(&dhd->pub, ifidx)));
			return -EOPNOTSUPP;
		}

		DHD_INFO(("%s: could not get toe_ol: ret=%d\n", dhd_ifname(&dhd->pub, ifidx), ret));
		return ret;
	}

	memcpy(toe_ol, buf, sizeof(uint32));
	return 0;
}

static int
dhd_toe_set(dhd_info_t *dhd, int ifidx, uint32 toe_ol)
{
	wl_ioctl_t ioc;
	char buf[32];
	int toe, ret;

	memset(&ioc, 0, sizeof(ioc));

	ioc.cmd = WLC_SET_VAR;
	ioc.buf = buf;
	ioc.len = (uint)sizeof(buf);
	ioc.set = TRUE;

	

	strncpy(buf, "toe_ol", sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	memcpy(&buf[sizeof("toe_ol")], &toe_ol, sizeof(uint32));

	if ((ret = dhd_wl_ioctl(&dhd->pub, ifidx, &ioc, ioc.buf, ioc.len)) < 0) {
		DHD_ERROR(("%s: could not set toe_ol: ret=%d\n",
			dhd_ifname(&dhd->pub, ifidx), ret));
		return ret;
	}

	

	toe = (toe_ol != 0);

	strcpy(buf, "toe");
	memcpy(&buf[sizeof("toe")], &toe, sizeof(uint32));

	if ((ret = dhd_wl_ioctl(&dhd->pub, ifidx, &ioc, ioc.buf, ioc.len)) < 0) {
		DHD_ERROR(("%s: could not set toe: ret=%d\n", dhd_ifname(&dhd->pub, ifidx), ret));
		return ret;
	}

	return 0;
}
#endif 

#if defined(WL_CFG80211)
void dhd_set_scb_probe(dhd_pub_t *dhd)
{
#define NUM_SCB_MAX_PROBE 3
	int ret = 0;
	wl_scb_probe_t scb_probe;
	char iovbuf[WL_EVENTING_MASK_LEN + 12];

	memset(&scb_probe, 0, sizeof(wl_scb_probe_t));

	if (dhd->op_mode & DHD_FLAG_HOSTAP_MODE)
		return;

	bcm_mkiovar("scb_probe", NULL, 0, iovbuf, sizeof(iovbuf));

	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0)
		DHD_ERROR(("%s: GET max_scb_probe failed\n", __FUNCTION__));

	memcpy(&scb_probe, iovbuf, sizeof(wl_scb_probe_t));

	scb_probe.scb_max_probe = NUM_SCB_MAX_PROBE;

	bcm_mkiovar("scb_probe", (char *)&scb_probe,
		sizeof(wl_scb_probe_t), iovbuf, sizeof(iovbuf));
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0)
		DHD_ERROR(("%s: max_scb_probe setting failed\n", __FUNCTION__));
#undef NUM_SCB_MAX_PROBE
	return;
}
#endif 

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
static void
dhd_ethtool_get_drvinfo(struct net_device *net, struct ethtool_drvinfo *info)
{
	dhd_info_t *dhd = DHD_DEV_INFO(net);

	snprintf(info->driver, sizeof(info->driver), "wl");
	snprintf(info->version, sizeof(info->version), "%lu", dhd->pub.drv_version);
}

struct ethtool_ops dhd_ethtool_ops = {
	.get_drvinfo = dhd_ethtool_get_drvinfo
};
#endif 


#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 4, 2)
static int
dhd_ethtool(dhd_info_t *dhd, void *uaddr)
{
	struct ethtool_drvinfo info;
	char drvname[sizeof(info.driver)];
	uint32 cmd;
#ifdef TOE
	struct ethtool_value edata;
	uint32 toe_cmpnt, csum_dir;
	int ret;
#endif

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	
	if (copy_from_user(&cmd, uaddr, sizeof (uint32)))
		return -EFAULT;

	switch (cmd) {
	case ETHTOOL_GDRVINFO:
		
		if (copy_from_user(&info, uaddr, sizeof(info)))
			return -EFAULT;
		strncpy(drvname, info.driver, sizeof(info.driver));
		drvname[sizeof(info.driver)-1] = '\0';

		
		memset(&info, 0, sizeof(info));
		info.cmd = cmd;

		
		if (strcmp(drvname, "?dhd") == 0) {
			snprintf(info.driver, sizeof(info.driver), "dhd");
			strncpy(info.version, EPI_VERSION_STR, sizeof(info.version) - 1);
			info.version[sizeof(info.version) - 1] = '\0';
		}

		
		else if (!dhd->pub.up) {
			DHD_ERROR(("%s: dongle is not up\n", __FUNCTION__));
			return -ENODEV;
		}

		
		else if (dhd->pub.iswl)
			snprintf(info.driver, sizeof(info.driver), "wl");
		else
			snprintf(info.driver, sizeof(info.driver), "xx");

		snprintf(info.version, sizeof(info.version), "%lu", dhd->pub.drv_version);
		if (copy_to_user(uaddr, &info, sizeof(info)))
			return -EFAULT;
		DHD_CTL(("%s: given %*s, returning %s\n", __FUNCTION__,
		         (int)sizeof(drvname), drvname, info.driver));
		break;

#ifdef TOE
	
	case ETHTOOL_GRXCSUM:
	case ETHTOOL_GTXCSUM:
		if ((ret = dhd_toe_get(dhd, 0, &toe_cmpnt)) < 0)
			return ret;

		csum_dir = (cmd == ETHTOOL_GTXCSUM) ? TOE_TX_CSUM_OL : TOE_RX_CSUM_OL;

		edata.cmd = cmd;
		edata.data = (toe_cmpnt & csum_dir) ? 1 : 0;

		if (copy_to_user(uaddr, &edata, sizeof(edata)))
			return -EFAULT;
		break;

	
	case ETHTOOL_SRXCSUM:
	case ETHTOOL_STXCSUM:
		if (copy_from_user(&edata, uaddr, sizeof(edata)))
			return -EFAULT;

		
		if ((ret = dhd_toe_get(dhd, 0, &toe_cmpnt)) < 0)
			return ret;

		csum_dir = (cmd == ETHTOOL_STXCSUM) ? TOE_TX_CSUM_OL : TOE_RX_CSUM_OL;

		if (edata.data != 0)
			toe_cmpnt |= csum_dir;
		else
			toe_cmpnt &= ~csum_dir;

		if ((ret = dhd_toe_set(dhd, 0, toe_cmpnt)) < 0)
			return ret;

		
		if (cmd == ETHTOOL_STXCSUM) {
			if (edata.data)
				dhd->iflist[0]->net->features |= NETIF_F_IP_CSUM;
			else
				dhd->iflist[0]->net->features &= ~NETIF_F_IP_CSUM;
		}

		break;
#endif 

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}
#endif 

static bool dhd_check_hang(struct net_device *net, dhd_pub_t *dhdp, int error)
{
	dhd_info_t *dhd;

	if (!dhdp) {
		DHD_ERROR(("%s: dhdp is NULL\n", __FUNCTION__));
		return FALSE;
	}

	if (!dhdp->up)
		return FALSE;

	dhd = (dhd_info_t *)dhdp->info;
#if !defined(BCMPCIE)
	if (dhd->thr_dpc_ctl.thr_pid < 0) {
		DHD_ERROR(("%s : skipped due to negative pid - unloading?\n", __FUNCTION__));
		return FALSE;
	}
#endif 

#ifdef CONFIG_MACH_UNIVERSAL5433
	
	if ((check_rev() && (error == -ETIMEDOUT)) || (error == -EREMOTEIO) ||
#else
	if ((error == -ETIMEDOUT) || (error == -EREMOTEIO) ||
#endif 
		((dhdp->busstate == DHD_BUS_DOWN) && (!dhdp->dongle_reset))) {
#ifdef BCMPCIE
		DHD_ERROR(("%s: Event HANG send up due to  re=%d te=%d d3acke=%d e=%d s=%d\n",
			__FUNCTION__, dhdp->rxcnt_timeout, dhdp->txcnt_timeout,
			dhdp->d3ackcnt_timeout, error, dhdp->busstate));
#else
		DHD_ERROR(("%s: Event HANG send up due to  re=%d te=%d e=%d s=%d\n", __FUNCTION__,
			dhdp->rxcnt_timeout, dhdp->txcnt_timeout, error, dhdp->busstate));
#endif 

		net_os_send_hang_message(net);
		return TRUE;
	}
	return FALSE;
}

int dhd_ioctl_process(dhd_pub_t *pub, int ifidx, dhd_ioctl_t *ioc, void *data_buf)
{
	int bcmerror = BCME_OK;
	int buflen = 0;
	struct net_device *net;

	net = dhd_idx2net(pub, ifidx);
	if (!net) {
		bcmerror = BCME_BADARG;
		goto done;
	}

	if (data_buf)
		buflen = MIN(ioc->len, DHD_IOCTL_MAXLEN);

	
	if (ioc->driver == DHD_IOCTL_MAGIC) {
		bcmerror = dhd_ioctl((void *)pub, ioc, data_buf, buflen);
		if (bcmerror)
			pub->bcmerror = bcmerror;
		goto done;
	}

	
	if (pub->busstate != DHD_BUS_DATA) {
		bcmerror = BCME_DONGLE_DOWN;
		goto done;
	}

	if (!pub->iswl) {
		bcmerror = BCME_DONGLE_DOWN;
		goto done;
	}

	if (ioc->cmd == WLC_SET_KEY ||
	    (ioc->cmd == WLC_SET_VAR && data_buf != NULL &&
	     strncmp("wsec_key", data_buf, 9) == 0) ||
	    (ioc->cmd == WLC_SET_VAR && data_buf != NULL &&
	     strncmp("bsscfg:wsec_key", data_buf, 15) == 0) ||
	    ioc->cmd == WLC_DISASSOC)
		dhd_wait_pend8021x(net);

#ifdef WLMEDIA_HTSF
	if (data_buf) {
		
		if (strcmp("htsf", data_buf) == 0) {
			dhd_ioctl_htsf_get(dhd, 0);
			return BCME_OK;
		}

		if (strcmp("htsflate", data_buf) == 0) {
			if (ioc->set) {
				memset(ts, 0, sizeof(tstamp_t)*TSMAX);
				memset(&maxdelayts, 0, sizeof(tstamp_t));
				maxdelay = 0;
				tspktcnt = 0;
				maxdelaypktno = 0;
				memset(&vi_d1.bin, 0, sizeof(uint32)*NUMBIN);
				memset(&vi_d2.bin, 0, sizeof(uint32)*NUMBIN);
				memset(&vi_d3.bin, 0, sizeof(uint32)*NUMBIN);
				memset(&vi_d4.bin, 0, sizeof(uint32)*NUMBIN);
			} else {
				dhd_dump_latency();
			}
			return BCME_OK;
		}
		if (strcmp("htsfclear", data_buf) == 0) {
			memset(&vi_d1.bin, 0, sizeof(uint32)*NUMBIN);
			memset(&vi_d2.bin, 0, sizeof(uint32)*NUMBIN);
			memset(&vi_d3.bin, 0, sizeof(uint32)*NUMBIN);
			memset(&vi_d4.bin, 0, sizeof(uint32)*NUMBIN);
			htsf_seqnum = 0;
			return BCME_OK;
		}
		if (strcmp("htsfhis", data_buf) == 0) {
			dhd_dump_htsfhisto(&vi_d1, "H to D");
			dhd_dump_htsfhisto(&vi_d2, "D to D");
			dhd_dump_htsfhisto(&vi_d3, "D to H");
			dhd_dump_htsfhisto(&vi_d4, "H to H");
			return BCME_OK;
		}
		if (strcmp("tsport", data_buf) == 0) {
			if (ioc->set) {
				memcpy(&tsport, data_buf + 7, 4);
			} else {
				DHD_ERROR(("current timestamp port: %d \n", tsport));
			}
			return BCME_OK;
		}
	}
#endif 

	if ((ioc->cmd == WLC_SET_VAR || ioc->cmd == WLC_GET_VAR) &&
		data_buf != NULL && strncmp("rpc_", data_buf, 4) == 0) {
#ifdef BCM_FD_AGGR
		bcmerror = dhd_fdaggr_ioctl(pub, ifidx, (wl_ioctl_t *)ioc, data_buf, buflen);
#else
		bcmerror = BCME_UNSUPPORTED;
#endif
		goto done;
	}
	bcmerror = dhd_wl_ioctl(pub, ifidx, (wl_ioctl_t *)ioc, data_buf, buflen);

done:
	dhd_check_hang(net, pub, bcmerror);

	return bcmerror;
}

static int
dhd_ioctl_entry(struct net_device *net, struct ifreq *ifr, int cmd)
{
	dhd_info_t *dhd = DHD_DEV_INFO(net);
	dhd_ioctl_t ioc;
	int bcmerror = 0;
	int ifidx;
	int ret;
	void *local_buf = NULL;
	u16 buflen = 0;

#ifdef CUSTOMER_HW_ONE
	if (dhd->pub.os_stopped) {
		DHD_ERROR(("%s: interface stopped. cmd 0x%04x\n", __FUNCTION__, cmd));
		return -1;
	}

	
	if (!dhd->pub.up || (dhd->pub.busstate == DHD_BUS_DOWN)) {
		DHD_ERROR(("%s: dhd is down. skip it.\n", __func__));
		return -ENODEV;
	}
#endif
	DHD_OS_WAKE_LOCK(&dhd->pub);
	DHD_PERIM_LOCK(&dhd->pub);

	
	if (!dhd_download_fw_on_driverload && dhd->pub.up == 0) {
		DHD_TRACE(("%s: Interface is down \n", __FUNCTION__));
		DHD_PERIM_UNLOCK(&dhd->pub);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return BCME_NOTUP;
	}

	
	if (dhd->pub.hang_was_sent) {
		DHD_TRACE(("%s: HANG was sent up earlier\n", __FUNCTION__));
		DHD_OS_WAKE_LOCK_CTRL_TIMEOUT_ENABLE(&dhd->pub, DHD_EVENT_TIMEOUT_MS);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return OSL_ERROR(BCME_DONGLE_DOWN);
	}

	ifidx = dhd_net2idx(dhd, net);
	DHD_TRACE(("%s: ifidx %d, cmd 0x%04x\n", __FUNCTION__, ifidx, cmd));

	if (ifidx == DHD_BAD_IF) {
		DHD_ERROR(("%s: BAD IF\n", __FUNCTION__));
		DHD_PERIM_UNLOCK(&dhd->pub);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return -1;
	}

#if defined(WL_WIRELESS_EXT)
	
	if ((cmd >= SIOCIWFIRST) && (cmd <= SIOCIWLAST)) {
		
		ret = wl_iw_ioctl(net, ifr, cmd);
		DHD_PERIM_UNLOCK(&dhd->pub);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return ret;
	}
#endif 

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 4, 2)
	if (cmd == SIOCETHTOOL) {
		ret = dhd_ethtool(dhd, (void*)ifr->ifr_data);
		DHD_PERIM_UNLOCK(&dhd->pub);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return ret;
	}
#endif 

	if (cmd == SIOCDEVPRIVATE+1) {
		ret = wl_android_priv_cmd(net, ifr, cmd);
		dhd_check_hang(net, &dhd->pub, ret);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return ret;
	}

	if (cmd != SIOCDEVPRIVATE) {
		DHD_PERIM_UNLOCK(&dhd->pub);
		DHD_OS_WAKE_UNLOCK(&dhd->pub);
		return -EOPNOTSUPP;
	}

	memset(&ioc, 0, sizeof(ioc));

#ifdef CONFIG_COMPAT
	if (is_compat_task()) {
		compat_wl_ioctl_t compat_ioc;
		if (copy_from_user(&compat_ioc, ifr->ifr_data, sizeof(compat_wl_ioctl_t))) {
			bcmerror = BCME_BADADDR;
			goto done;
		}
		ioc.cmd = compat_ioc.cmd;
		ioc.buf = compat_ptr(compat_ioc.buf);
		ioc.len = compat_ioc.len;
		ioc.set = compat_ioc.set;
		ioc.used = compat_ioc.used;
		ioc.needed = compat_ioc.needed;
		
		if ((copy_from_user(&ioc.driver, (char *)ifr->ifr_data + sizeof(compat_wl_ioctl_t),
			sizeof(uint)) != 0)) {
			bcmerror = BCME_BADADDR;
			goto done;
		}
	} else
#endif 
	{
		
		if (copy_from_user(&ioc, ifr->ifr_data, sizeof(wl_ioctl_t))) {
			bcmerror = BCME_BADADDR;
			goto done;
		}

		
		if ((copy_from_user(&ioc.driver, (char *)ifr->ifr_data + sizeof(wl_ioctl_t),
			sizeof(uint)) != 0)) {
			bcmerror = BCME_BADADDR;
			goto done;
		}
	}

	if (!capable(CAP_NET_ADMIN)) {
		bcmerror = BCME_EPERM;
		goto done;
	}

	if (ioc.len > 0) {
		buflen = MIN(ioc.len, DHD_IOCTL_MAXLEN);
		if (!(local_buf = MALLOC(dhd->pub.osh, buflen+1))) {
			bcmerror = BCME_NOMEM;
			goto done;
		}

		DHD_PERIM_UNLOCK(&dhd->pub);
		if (copy_from_user(local_buf, ioc.buf, buflen)) {
			DHD_PERIM_LOCK(&dhd->pub);
			bcmerror = BCME_BADADDR;
			goto done;
		}
		DHD_PERIM_LOCK(&dhd->pub);

		*(char *)(local_buf + buflen) = '\0';
	}

	bcmerror = dhd_ioctl_process(&dhd->pub, ifidx, &ioc, local_buf);

	if (!bcmerror && buflen && local_buf && ioc.buf) {
		DHD_PERIM_UNLOCK(&dhd->pub);
		if (copy_to_user(ioc.buf, local_buf, buflen))
			bcmerror = -EFAULT;
		DHD_PERIM_LOCK(&dhd->pub);
	}

done:
	if (local_buf)
		MFREE(dhd->pub.osh, local_buf, buflen+1);

	DHD_PERIM_UNLOCK(&dhd->pub);
	DHD_OS_WAKE_UNLOCK(&dhd->pub);

	return OSL_ERROR(bcmerror);
}



static int
dhd_stop(struct net_device *net)
{
	int ifidx = 0;
	dhd_info_t *dhd = DHD_DEV_INFO(net);
	DHD_OS_WAKE_LOCK(&dhd->pub);
	DHD_PERIM_LOCK(&dhd->pub);
#ifdef CUSTOMER_HW_ONE
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)) && 1
	if (mutex_is_locked(&_dhd_onoff_mutex_lock_) != 0) {
		DHD_ERROR(("%s : mutex lock by dhd_open wait to unlock!\n", __FUNCTION__));
	}
	mutex_lock(&_dhd_onoff_mutex_lock_);
#endif 
#endif 
	DHD_TRACE(("%s: Enter %p\n", __FUNCTION__, net));

	dhd->pub.rxcnt_timeout = 0;
	dhd->pub.txcnt_timeout = 0;
#ifdef BCMPCIE
	dhd->pub.d3ackcnt_timeout = 0;
#endif 

	if (dhd->pub.up == 0) {
		goto exit;
	}

	dhd_if_flush_sta(DHD_DEV_IFP(net));


	ifidx = dhd_net2idx(dhd, net);
	BCM_REFERENCE(ifidx);
#ifdef CUSTOMER_HW_ONE
	dhd->pub.os_stopped = 1;
	smp_mb();
#if defined(HW_OOB)
	
#endif
#endif 

	
	netif_stop_queue(net);
	dhd->pub.up = 0;

#ifdef WL_CFG80211
	if (ifidx == 0) {
		wl_cfg80211_down(NULL);

		if (!dhd_download_fw_on_driverload) {
			if ((dhd->dhd_state & DHD_ATTACH_STATE_ADD_IF) &&
				(dhd->dhd_state & DHD_ATTACH_STATE_CFG80211)) {
				int i;


				dhd_net_if_lock_local(dhd);
				for (i = 1; i < DHD_MAX_IFS; i++)
					dhd_remove_if(&dhd->pub, i, FALSE);
				dhd_net_if_unlock_local(dhd);
			}
		}
	}
#endif 

#ifdef PROP_TXSTATUS
	dhd_wlfc_cleanup(&dhd->pub, NULL, 0);
#endif
	
	dhd_prot_stop(&dhd->pub);

	OLD_MOD_DEC_USE_COUNT;
exit:
#if defined(WL_CFG80211)
	if (ifidx == 0 && !dhd_download_fw_on_driverload)
		wl_android_wifi_off(net);
#endif 

	dhd->pub.hang_was_sent = 0;

	
	if (!dhd_download_fw_on_driverload) {
		dhd->pub.dhd_cspec.country_abbrev[0] = 0x00;
		dhd->pub.dhd_cspec.rev = 0;
		dhd->pub.dhd_cspec.ccode[0] = 0x00;
	}
#ifdef CUSTOMER_HW_ONE
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)) && 1
	mutex_unlock(&_dhd_onoff_mutex_lock_);
#endif 
#endif 
	DHD_PERIM_UNLOCK(&dhd->pub);
	DHD_OS_WAKE_UNLOCK(&dhd->pub);
	return 0;
}

#if defined(WL_CFG80211) && defined(USE_INITIAL_SHORT_DWELL_TIME)
extern bool g_first_broadcast_scan;
#endif 

#ifdef WL11U
static int dhd_interworking_enable(dhd_pub_t *dhd)
{
	char iovbuf[WLC_IOCTL_SMLEN];
	uint32 enable = true;
	int ret = BCME_OK;

	bcm_mkiovar("interworking", (char *)&enable, sizeof(enable), iovbuf, sizeof(iovbuf));
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0) {
		DHD_ERROR(("%s: enableing interworking failed, ret=%d\n", __FUNCTION__, ret));
	}

	if (ret == BCME_OK) {
		
		uint32 cap = WL_WNM_BSSTRANS | WL_WNM_NOTIF;
		bcm_mkiovar("wnm", (char *)&cap, sizeof(cap), iovbuf, sizeof(iovbuf));
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR,
			iovbuf, sizeof(iovbuf), TRUE, 0)) < 0) {
			DHD_ERROR(("%s: failed to set WNM info, ret=%d\n", __FUNCTION__, ret));
		}
	}

	return ret;
}
#endif 

static int
dhd_open(struct net_device *net)
{
	dhd_info_t *dhd = DHD_DEV_INFO(net);
#ifdef TOE
	uint32 toe_ol;
#endif
	int ifidx;
	int32 ret = 0;
#ifdef CUSTOMER_HW_ONE
	int32 dhd_open_retry_count = 0;
#endif



#if defined(MULTIPLE_SUPPLICANT)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)) && 1 && defined(BCMSDIO)
	if (mutex_is_locked(&_dhd_sdio_mutex_lock_) != 0) {
		DHD_ERROR(("%s : dhd_open: call dev open before insmod complete!\n", __FUNCTION__));
	}
	mutex_lock(&_dhd_sdio_mutex_lock_);
#endif
#endif 

#ifdef CUSTOMER_HW_ONE
dhd_open_retry:
#endif
	DHD_OS_WAKE_LOCK(&dhd->pub);
	DHD_PERIM_LOCK(&dhd->pub);
#ifdef CUSTOMER_HW_ONE
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)) && 1
	if (mutex_is_locked(&_dhd_onoff_mutex_lock_) != 0) {
		DHD_ERROR(("%s : mutex locked by dhd_stop wait for unlock !!\n", __FUNCTION__));
	}
	mutex_lock(&_dhd_onoff_mutex_lock_);
#endif 
	dhd->pub.os_stopped = 0;
	dhd->pub.txdesc_no_res = 0;
	dhd->pub.allow_p2p_event = 0;
#endif 
	dhd->pub.dongle_trap_occured = 0;
	dhd->pub.hang_was_sent = 0;

#if !defined(WL_CFG80211)
	ret = wl_control_wl_start(net);
	if (ret != 0) {
		DHD_ERROR(("%s: failed with code %d\n", __FUNCTION__, ret));
		ret = -1;
		goto exit;
	}

#endif 

	ifidx = dhd_net2idx(dhd, net);
	DHD_TRACE(("%s: ifidx %d\n", __FUNCTION__, ifidx));

	if (ifidx < 0) {
		DHD_ERROR(("%s: Error: called with invalid IF\n", __FUNCTION__));
		ret = -1;
		goto exit;
	}

	if (!dhd->iflist[ifidx]) {
		DHD_ERROR(("%s: Error: called when IF already deleted\n", __FUNCTION__));
		ret = -1;
		goto exit;
	}

	if (ifidx == 0) {
		atomic_set(&dhd->pend_8021x_cnt, 0);
#if defined(WL_CFG80211)
		if (!dhd_download_fw_on_driverload) {
			DHD_ERROR(("\n%s\n", dhd_version));
#if defined(USE_INITIAL_SHORT_DWELL_TIME)
			g_first_broadcast_scan = TRUE;
#endif 
			ret = wl_android_wifi_on(net);
			if (ret != 0) {
				DHD_ERROR(("%s : wl_android_wifi_on failed (%d)\n",
					__FUNCTION__, ret));
				ret = -1;
				goto exit;
			}
		}
#endif 

		if (dhd->pub.busstate != DHD_BUS_DATA) {

			
			DHD_PERIM_UNLOCK(&dhd->pub);
			ret = dhd_bus_start(&dhd->pub);
			DHD_PERIM_LOCK(&dhd->pub);
			if (ret) {
				DHD_ERROR(("%s: failed with code %d\n", __FUNCTION__, ret));
				ret = -1;
				goto exit;
			}

		}

		
		memcpy(net->dev_addr, dhd->pub.mac.octet, ETHER_ADDR_LEN);
#ifdef CUSTOMER_HW_ONE
		memcpy(dhd->iflist[ifidx]->mac_addr, dhd->pub.mac.octet, ETHER_ADDR_LEN);
#endif

#ifdef TOE
		
		if (dhd_toe_get(dhd, ifidx, &toe_ol) >= 0 && (toe_ol & TOE_TX_CSUM_OL) != 0)
			dhd->iflist[ifidx]->net->features |= NETIF_F_IP_CSUM;
		else
			dhd->iflist[ifidx]->net->features &= ~NETIF_F_IP_CSUM;
#endif 

#if defined(WL_CFG80211)
		if (unlikely(wl_cfg80211_up(NULL))) {
			DHD_ERROR(("%s: failed to bring up cfg80211\n", __FUNCTION__));
			ret = -1;
			goto exit;
		}
		dhd_set_scb_probe(&dhd->pub);
#endif 
	}

	
	netif_start_queue(net);
	dhd->pub.up = 1;

#ifdef BCMDBGFS
	dhd_dbg_init(&dhd->pub);
#endif

	OLD_MOD_INC_USE_COUNT;
exit:
#ifdef CUSTOMER_HW_ONE
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)) && 1
	mutex_unlock(&_dhd_onoff_mutex_lock_);
#endif 
#endif 
	if (ret)
		dhd_stop(net);

	DHD_PERIM_UNLOCK(&dhd->pub);
	DHD_OS_WAKE_UNLOCK(&dhd->pub);
#ifdef CUSTOMER_HW_ONE
	if (ret && (dhd_open_retry_count < 3)) {
		dhd_open_retry_count++;
		goto dhd_open_retry;
	}
#endif

#if defined(MULTIPLE_SUPPLICANT)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)) && 1 && defined(BCMSDIO)
	mutex_unlock(&_dhd_sdio_mutex_lock_);
#endif
#endif 

	return ret;
}

int dhd_do_driver_init(struct net_device *net)
{
	dhd_info_t *dhd = NULL;

	if (!net) {
		DHD_ERROR(("Primary Interface not initialized \n"));
		return -EINVAL;
	}

#ifdef MULTIPLE_SUPPLICANT
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)) && 1 && defined(BCMSDIO)
	if (mutex_is_locked(&_dhd_sdio_mutex_lock_) != 0) {
		DHD_ERROR(("%s : dhdsdio_probe is already running!\n", __FUNCTION__));
		return 0;
	}
#endif 
#endif 

	
	dhd = DHD_DEV_INFO(net);

	if (dhd->pub.busstate == DHD_BUS_DATA) {
		DHD_TRACE(("Driver already Inititalized. Nothing to do"));
		return 0;
	}

	if (dhd_open(net) < 0) {
		DHD_ERROR(("Driver Init Failed \n"));
		return -1;
	}

	return 0;
}

int
dhd_event_ifadd(dhd_info_t *dhdinfo, wl_event_data_if_t *ifevent, char *name, uint8 *mac)
{

#ifdef WL_CFG80211
	if (wl_cfg80211_notify_ifadd(ifevent->ifidx, name, mac, ifevent->bssidx) == BCME_OK)
		return BCME_OK;
#endif

	if (ifevent->ifidx > 0) {
		dhd_if_event_t *if_event = MALLOC(dhdinfo->pub.osh, sizeof(dhd_if_event_t));

		memcpy(&if_event->event, ifevent, sizeof(if_event->event));
		memcpy(if_event->mac, mac, ETHER_ADDR_LEN);
		strncpy(if_event->name, name, IFNAMSIZ);
		if_event->name[IFNAMSIZ - 1] = '\0';
		dhd_deferred_schedule_work(dhdinfo->dhd_deferred_wq, (void *)if_event,
			DHD_WQ_WORK_IF_ADD, dhd_ifadd_event_handler, DHD_WORK_PRIORITY_LOW);
	}

	return BCME_OK;
}

int
dhd_event_ifdel(dhd_info_t *dhdinfo, wl_event_data_if_t *ifevent, char *name, uint8 *mac)
{
	dhd_if_event_t *if_event;

#if defined(WL_CFG80211) && !defined(P2PONEINT)
	if (wl_cfg80211_notify_ifdel(ifevent->ifidx, name, mac, ifevent->bssidx) == BCME_OK)
		return BCME_OK;
#endif 

	if_event = MALLOC(dhdinfo->pub.osh, sizeof(dhd_if_event_t));
	memcpy(&if_event->event, ifevent, sizeof(if_event->event));
	memcpy(if_event->mac, mac, ETHER_ADDR_LEN);
	strncpy(if_event->name, name, IFNAMSIZ);
	if_event->name[IFNAMSIZ - 1] = '\0';
	dhd_deferred_schedule_work(dhdinfo->dhd_deferred_wq, (void *)if_event, DHD_WQ_WORK_IF_DEL,
		dhd_ifdel_event_handler, DHD_WORK_PRIORITY_LOW);

	return BCME_OK;
}

struct net_device*
dhd_allocate_if(dhd_pub_t *dhdpub, int ifidx, char *name,
	uint8 *mac, uint8 bssidx, bool need_rtnl_lock)
{
	dhd_info_t *dhdinfo = (dhd_info_t *)dhdpub->info;
	dhd_if_t *ifp;

	ASSERT(dhdinfo && (ifidx < DHD_MAX_IFS));
	ifp = dhdinfo->iflist[ifidx];

	if (ifp != NULL) {
		if (ifp->net != NULL) {
			DHD_ERROR(("%s: free existing IF %s\n", __FUNCTION__, ifp->net->name));

			dhd_dev_priv_clear(ifp->net); 

			if (ifp->net->reg_state == NETREG_UNINITIALIZED) {
				free_netdev(ifp->net);
			} else {
				netif_stop_queue(ifp->net);
				if (need_rtnl_lock)
					unregister_netdev(ifp->net);
				else
					unregister_netdevice(ifp->net);
			}
			ifp->net = NULL;
		}
	} else {
		ifp = MALLOC(dhdinfo->pub.osh, sizeof(dhd_if_t));
		if (ifp == NULL) {
			DHD_ERROR(("%s: OOM - dhd_if_t(%zu)\n", __FUNCTION__, sizeof(dhd_if_t)));
			return NULL;
		}
	}

	memset(ifp, 0, sizeof(dhd_if_t));
	ifp->info = dhdinfo;
	ifp->idx = ifidx;
	ifp->bssidx = bssidx;
	if (mac != NULL)
		memcpy(&ifp->mac_addr, mac, ETHER_ADDR_LEN);

	
	ifp->net = alloc_etherdev(DHD_DEV_PRIV_SIZE);
	if (ifp->net == NULL) {
		DHD_ERROR(("%s: OOM - alloc_etherdev(%zu)\n", __FUNCTION__, sizeof(dhdinfo)));
		goto fail;
	}

	
	dhd_dev_priv_save(ifp->net, dhdinfo, ifp, ifidx);

	if (name && name[0]) {
		strncpy(ifp->net->name, name, IFNAMSIZ);
		ifp->net->name[IFNAMSIZ - 1] = '\0';
	}
#ifdef WL_CFG80211
	if (ifidx == 0)
		ifp->net->destructor = free_netdev;
	else
		ifp->net->destructor = dhd_netdev_free;
#else
	ifp->net->destructor = free_netdev;
#endif 
	strncpy(ifp->name, ifp->net->name, IFNAMSIZ);
	ifp->name[IFNAMSIZ - 1] = '\0';
	dhdinfo->iflist[ifidx] = ifp;

#ifdef PCIE_FULL_DONGLE
	
	INIT_LIST_HEAD(&ifp->sta_list);
	DHD_IF_STA_LIST_LOCK_INIT(ifp);
#endif 

	return ifp->net;

fail:
	if (ifp != NULL) {
		if (ifp->net != NULL) {
			dhd_dev_priv_clear(ifp->net);
			free_netdev(ifp->net);
			ifp->net = NULL;
		}
		MFREE(dhdinfo->pub.osh, ifp, sizeof(*ifp));
		ifp = NULL;
	}
	dhdinfo->iflist[ifidx] = NULL;
	return NULL;
}

int
dhd_remove_if(dhd_pub_t *dhdpub, int ifidx, bool need_rtnl_lock)
{
	dhd_info_t *dhdinfo = (dhd_info_t *)dhdpub->info;
	dhd_if_t *ifp;

	ifp = dhdinfo->iflist[ifidx];
	if (ifp != NULL) {
		if (ifp->net != NULL) {
			DHD_ERROR(("deleting interface '%s' idx %d\n", ifp->net->name, ifp->idx));

			if (ifp->net->reg_state == NETREG_UNINITIALIZED) {
				free_netdev(ifp->net);
			} else {
				netif_stop_queue(ifp->net);



#ifdef SET_RPS_CPUS
				custom_rps_map_clear(ifp->net->_rx);
#endif 
				if (need_rtnl_lock)
					unregister_netdev(ifp->net);
				else
					unregister_netdevice(ifp->net);
			}
			ifp->net = NULL;
		}
#ifdef DHD_WMF
		dhd_wmf_cleanup(dhdpub, ifidx);
#endif 

		dhd_if_del_sta_list(ifp);

		dhdinfo->iflist[ifidx] = NULL;
		MFREE(dhdinfo->pub.osh, ifp, sizeof(*ifp));

	}

	return BCME_OK;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31))
static struct net_device_ops dhd_ops_pri = {
	.ndo_open = dhd_open,
	.ndo_stop = dhd_stop,
	.ndo_get_stats = dhd_get_stats,
	.ndo_do_ioctl = dhd_ioctl_entry,
	.ndo_start_xmit = dhd_start_xmit,
	.ndo_set_mac_address = dhd_set_mac_address,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
	.ndo_set_rx_mode = dhd_set_multicast_list,
#else
	.ndo_set_multicast_list = dhd_set_multicast_list,
#endif
};

static struct net_device_ops dhd_ops_virt = {
	.ndo_get_stats = dhd_get_stats,
	.ndo_do_ioctl = dhd_ioctl_entry,
	.ndo_start_xmit = dhd_start_xmit,
	.ndo_set_mac_address = dhd_set_mac_address,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
	.ndo_set_rx_mode = dhd_set_multicast_list,
#else
	.ndo_set_multicast_list = dhd_set_multicast_list,
#endif
};

#ifdef P2PONEINT
extern int wl_cfgp2p_if_open(struct net_device *net);
extern int wl_cfgp2p_if_stop(struct net_device *net);

static struct net_device_ops dhd_cfgp2p_ops_virt = {
	.ndo_open = wl_cfgp2p_if_open,
	.ndo_stop = wl_cfgp2p_if_stop,
	.ndo_get_stats = dhd_get_stats,
	.ndo_do_ioctl = dhd_ioctl_entry,
	.ndo_start_xmit = dhd_start_xmit,
	.ndo_set_mac_address = dhd_set_mac_address,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
	.ndo_set_rx_mode = dhd_set_multicast_list,
#else
	.ndo_set_multicast_list = dhd_set_multicast_list,
#endif
};
#endif 
#endif 

#ifdef DEBUGGER
extern void debugger_init(void *bus_handle);
#endif


#ifdef SHOW_LOGTRACE
static char *logstrs_path = "/root/logstrs.bin";
module_param(logstrs_path, charp, S_IRUGO);

int
dhd_init_logstrs_array(dhd_event_log_t *temp)
{
	struct file *filep = NULL;
	struct kstat stat;
	mm_segment_t fs;
	char *raw_fmts =  NULL;
	int logstrs_size = 0;

	logstr_header_t *hdr = NULL;
	uint32 *lognums = NULL;
	char *logstrs = NULL;
	int ram_index = 0;
	char **fmts;
	int num_fmts = 0;
	uint32 i = 0;
	int error = 0;
	set_fs(KERNEL_DS);
	fs = get_fs();
	filep = filp_open(logstrs_path, O_RDONLY, 0);
	if (IS_ERR(filep)) {
		DHD_ERROR(("Failed to open the file logstrs.bin in %s",  __FUNCTION__));
		goto fail;
	}
	error = vfs_stat(logstrs_path, &stat);
	if (error) {
		DHD_ERROR(("Failed in %s to find file stat", __FUNCTION__));
		goto fail;
	}
	logstrs_size = (int) stat.size;

	raw_fmts = kmalloc(logstrs_size, GFP_KERNEL);
	if (raw_fmts == NULL) {
		DHD_ERROR(("Failed to allocate raw_fmts memory"));
		goto fail;
	}
	if (vfs_read(filep, raw_fmts, logstrs_size, &filep->f_pos) !=	logstrs_size) {
		DHD_ERROR(("Error: Log strings file read failed"));
		goto fail;
	}

	
	hdr = (logstr_header_t *) (raw_fmts + logstrs_size -
		sizeof(logstr_header_t));

	if (hdr->log_magic == LOGSTRS_MAGIC) {
		num_fmts =	hdr->rom_logstrs_offset / sizeof(uint32);
		ram_index = (hdr->ram_lognums_offset -
			hdr->rom_lognums_offset) / sizeof(uint32);
		lognums = (uint32 *) &raw_fmts[hdr->rom_lognums_offset];
		logstrs = (char *)	 &raw_fmts[hdr->rom_logstrs_offset];
	} else {
		num_fmts = *((uint32 *) (raw_fmts)) / sizeof(uint32);
		if (num_fmts == 0) {
			#define NUM_4324B5_ROM_FMTS	186
			#define FIRST_4324B5_ROM_LOGSTR "Con\n"
			ram_index = NUM_4324B5_ROM_FMTS;
			lognums = (uint32 *) raw_fmts;
			num_fmts =	ram_index;
			logstrs = (char *) &raw_fmts[num_fmts << 2];
			while (strncmp(FIRST_4324B5_ROM_LOGSTR, logstrs, 4)) {
				num_fmts++;
				logstrs = (char *) &raw_fmts[num_fmts << 2];
			}
		} else {
				ram_index = 0;
				lognums = (uint32 *) raw_fmts;
				logstrs = (char *)	&raw_fmts[num_fmts << 2];
			}
	}
	fmts = kmalloc(num_fmts  * sizeof(char *), GFP_KERNEL);
	if (fmts == NULL) {
		DHD_ERROR(("Failed to allocate fmts memory"));
		goto fail;
	}

	for (i = 0; i < num_fmts; i++) {
		if (i == ram_index) {
			logstrs = raw_fmts;
		}
		fmts[i] = &logstrs[lognums[i]];
	}
	temp->fmts = fmts;
	temp->raw_fmts = raw_fmts;
	temp->num_fmts = num_fmts;
	filp_close(filep, NULL);
	set_fs(fs);
	return 0;
fail:
	if (raw_fmts) {
		kfree(raw_fmts);
		raw_fmts = NULL;
	}
	if (!IS_ERR(filep))
		filp_close(filep, NULL);
	set_fs(fs);
	temp->fmts = NULL;
	return -1;
}
#endif 


dhd_pub_t *
dhd_attach(osl_t *osh, struct dhd_bus *bus, uint bus_hdrlen)
{
	dhd_info_t *dhd = NULL;
	struct net_device *net = NULL;
	char if_name[IFNAMSIZ] = {'\0'};
	uint32 bus_type = -1;
	uint32 bus_num = -1;
	uint32 slot_num = -1;
	wifi_adapter_info_t *adapter = NULL;

	dhd_attach_states_t dhd_state = DHD_ATTACH_STATE_INIT;
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	
#if defined(BCMSDIO)
	dhd_bus_get_ids(bus, &bus_type, &bus_num, &slot_num);
#endif 
	adapter = dhd_wifi_platform_get_adapter(bus_type, bus_num, slot_num);

	
	dhd = wifi_platform_prealloc(adapter, DHD_PREALLOC_DHD_INFO, sizeof(dhd_info_t));
	if (dhd == NULL) {
		dhd = MALLOC(osh, sizeof(dhd_info_t));
		if (dhd == NULL) {
			DHD_ERROR(("%s: OOM - alloc dhd_info\n", __FUNCTION__));
			goto fail;
		}
	}
	memset(dhd, 0, sizeof(dhd_info_t));
	dhd_state |= DHD_ATTACH_STATE_DHD_ALLOC;

	dhd->unit = dhd_found + instance_base; 

	dhd->pub.osh = osh;
	dhd->adapter = adapter;

#ifdef GET_CUSTOM_MAC_ENABLE
	wifi_platform_get_mac_addr(dhd->adapter, dhd->pub.mac.octet);
#endif 
	dhd->thr_dpc_ctl.thr_pid = DHD_PID_KT_TL_INVALID;
	dhd->thr_wdt_ctl.thr_pid = DHD_PID_KT_INVALID;

	
	sema_init(&dhd->sdsem, 1);

	dhd_update_fw_nv_path(dhd);

	
	dhd->pub.info = dhd;


	
	dhd->pub.bus = bus;
	dhd->pub.hdrlen = bus_hdrlen;

	
	if (iface_name[0]) {
		int len;
		char ch;
		strncpy(if_name, iface_name, IFNAMSIZ);
		if_name[IFNAMSIZ - 1] = 0;
		len = strlen(if_name);
#ifdef CUSTOMER_HW_ONE
		if (len > 0) {
#endif
		ch = if_name[len - 1];
		if ((ch > '9' || ch < '0') && (len < IFNAMSIZ - 2))
			strcat(if_name, "%d");
#ifdef CUSTOMER_HW_ONE
		}
#endif
	}
	net = dhd_allocate_if(&dhd->pub, 0, if_name, NULL, 0, TRUE);
	if (net == NULL)
		goto fail;
	dhd_state |= DHD_ATTACH_STATE_ADD_IF;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31))
	net->open = NULL;
#else
	net->netdev_ops = NULL;
#endif

	sema_init(&dhd->proto_sem, 1);

#ifdef PROP_TXSTATUS
	spin_lock_init(&dhd->wlfc_spinlock);

	dhd->pub.skip_fc = dhd_wlfc_skip_fc;
	dhd->pub.plat_init = dhd_wlfc_plat_init;
	dhd->pub.plat_deinit = dhd_wlfc_plat_deinit;
#endif 

	
	init_waitqueue_head(&dhd->ioctl_resp_wait);
	init_waitqueue_head(&dhd->d3ack_wait);
	init_waitqueue_head(&dhd->ctrl_wait);

	
	spin_lock_init(&dhd->sdlock);
	spin_lock_init(&dhd->txqlock);
	spin_lock_init(&dhd->dhd_lock);
	spin_lock_init(&dhd->rxf_lock);
#if defined(RXFRAME_THREAD)
	dhd->rxthread_enabled = TRUE;
#endif 

#ifdef DHDTCPACK_SUPPRESS
	spin_lock_init(&dhd->tcpack_lock);
#endif 

	
	spin_lock_init(&dhd->wakelock_spinlock);
	dhd->wakelock_counter = 0;
	dhd->wakelock_wd_counter = 0;
	dhd->wakelock_rx_timeout_enable = 0;
	dhd->wakelock_ctrl_timeout_enable = 0;
#ifdef CONFIG_HAS_WAKELOCK
	wake_lock_init(&dhd->wl_wifi, WAKE_LOCK_SUSPEND, "wlan_wake");
	wake_lock_init(&dhd->wl_rxwake, WAKE_LOCK_SUSPEND, "wlan_rx_wake");
	wake_lock_init(&dhd->wl_ctrlwake, WAKE_LOCK_SUSPEND, "wlan_ctrl_wake");
	wake_lock_init(&dhd->wl_wdwake, WAKE_LOCK_SUSPEND, "wlan_wd_wake");
#ifdef BCMPCIE_OOB_HOST_WAKE
	wake_lock_init(&dhd->wl_intrwake, WAKE_LOCK_SUSPEND, "wlan_oob_irq_wake");
#endif 
#ifdef CUSTOMER_HW_ONE
	wake_lock_init(&dhd->wl_htc, WAKE_LOCK_SUSPEND, "wlan_htc");
#endif 
#endif 
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)) && 1
	mutex_init(&dhd->dhd_net_if_mutex);
	mutex_init(&dhd->dhd_suspend_mutex);
#endif
	dhd_state |= DHD_ATTACH_STATE_WAKELOCKS_INIT;

	
	if (dhd_prot_attach(&dhd->pub) != 0) {
		DHD_ERROR(("dhd_prot_attach failed\n"));
		goto fail;
	}
	dhd_state |= DHD_ATTACH_STATE_PROT_ATTACH;

#ifdef WL_CFG80211
	
	if (unlikely(wl_cfg80211_attach(net, &dhd->pub))) {
		DHD_ERROR(("wl_cfg80211_attach failed\n"));
		goto fail;
	}

	dhd_monitor_init(&dhd->pub);
	dhd_state |= DHD_ATTACH_STATE_CFG80211;
#endif
#if defined(WL_WIRELESS_EXT)
	
	if (!(dhd_state &  DHD_ATTACH_STATE_CFG80211)) {
		if (wl_iw_attach(net, (void *)&dhd->pub) != 0) {
		DHD_ERROR(("wl_iw_attach failed\n"));
		goto fail;
	}
	dhd_state |= DHD_ATTACH_STATE_WL_ATTACH;
	}
#endif 

#ifdef SHOW_LOGTRACE
	dhd_init_logstrs_array(&dhd->event_data);
#endif 

	if (dhd_sta_pool_init(&dhd->pub, DHD_MAX_STA) != BCME_OK) {
		DHD_ERROR(("%s: Initializing %u sta\n", __FUNCTION__, DHD_MAX_STA));
		goto fail;
	}


	
	init_timer(&dhd->timer);
	dhd->timer.data = (ulong)dhd;
	dhd->timer.function = dhd_watchdog;
	dhd->default_wd_interval = dhd_watchdog_ms;

	if (dhd_watchdog_prio >= 0) {
		
		PROC_START(dhd_watchdog_thread, dhd, &dhd->thr_wdt_ctl, 0, "dhd_watchdog_thread");

	} else {
		dhd->thr_wdt_ctl.thr_pid = -1;
	}

#ifdef DEBUGGER
	debugger_init((void *) bus);
#endif

#ifdef CUSTOMER_HW_ONE
	dhd->dhd_force_exit = FALSE;
#endif
	
	if (dhd_dpc_prio >= 0) {
		
		PROC_START(dhd_dpc_thread, dhd, &dhd->thr_dpc_ctl, 0, "dhd_dpc");
	} else {
		
		tasklet_init(&dhd->tasklet, dhd_dpc, (ulong)dhd);
		dhd->thr_dpc_ctl.thr_pid = -1;
	}

	if (dhd->rxthread_enabled) {
		bzero(&dhd->pub.skbbuf[0], sizeof(void *) * MAXSKBPEND);
		
		PROC_START(dhd_rxf_thread, dhd, &dhd->thr_rxf_ctl, 0, "dhd_rxf");
	}

	dhd_state |= DHD_ATTACH_STATE_THREADS_CREATED;

#if defined(CONFIG_PM_SLEEP)
	if (!dhd_pm_notifier_registered) {
		dhd_pm_notifier_registered = TRUE;
		register_pm_notifier(&dhd_pm_notifier);
	}
#endif 

#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(DHD_USE_EARLYSUSPEND)
	dhd->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 20;
	dhd->early_suspend.suspend = dhd_early_suspend;
	dhd->early_suspend.resume = dhd_late_resume;
	register_early_suspend(&dhd->early_suspend);
	dhd_state |= DHD_ATTACH_STATE_EARLYSUSPEND_DONE;
#endif 

#ifdef ARP_OFFLOAD_SUPPORT
	dhd->pend_ipaddr = 0;
	if (!dhd_inetaddr_notifier_registered) {
		dhd_inetaddr_notifier_registered = TRUE;
		register_inetaddr_notifier(&dhd_inetaddr_notifier);
	}
#endif 
#ifdef CONFIG_IPV6
	if (!dhd_inet6addr_notifier_registered) {
		dhd_inet6addr_notifier_registered = TRUE;
		register_inet6addr_notifier(&dhd_inet6addr_notifier);
	}
#endif
	dhd->dhd_deferred_wq = dhd_deferred_work_init((void *)dhd);
#ifdef DEBUG_CPU_FREQ
	dhd->new_freq = alloc_percpu(int);
	dhd->freq_trans.notifier_call = dhd_cpufreq_notifier;
	cpufreq_register_notifier(&dhd->freq_trans, CPUFREQ_TRANSITION_NOTIFIER);
#endif
#ifdef DHDTCPACK_SUPPRESS
#ifdef BCMSDIO
	dhd_tcpack_suppress_set(&dhd->pub, TCPACK_SUP_DELAYTX);
#elif defined(BCMPCIE)
	dhd_tcpack_suppress_set(&dhd->pub, TCPACK_SUP_HOLD);
#else
	dhd_tcpack_suppress_set(&dhd->pub, TCPACK_SUP_OFF);
#endif 
#endif 

	dhd_state |= DHD_ATTACH_STATE_DONE;
	dhd->dhd_state = dhd_state;

	dhd_found++;
#if defined(USE_STATIC_MEMDUMP)
	g_dhdp = &dhd->pub;
#endif 
#if defined(CUSTOMER_HW20) && defined(WLANAUDIO)
	dhd_global = dhd;
#endif 
	return &dhd->pub;

fail:
	if (dhd_state >= DHD_ATTACH_STATE_DHD_ALLOC) {
		DHD_TRACE(("%s: Calling dhd_detach dhd_state 0x%x &dhd->pub %p\n",
			__FUNCTION__, dhd_state, &dhd->pub));
		dhd->dhd_state = dhd_state;
		dhd_detach(&dhd->pub);
		dhd_free(&dhd->pub);
	}

	return NULL;
}

int dhd_get_fw_mode(dhd_info_t *dhdinfo)
{
	if (strstr(dhdinfo->fw_path, "_apsta") != NULL)
		return DHD_FLAG_HOSTAP_MODE;
	if (strstr(dhdinfo->fw_path, "_p2p") != NULL)
		return DHD_FLAG_P2P_MODE;
	if (strstr(dhdinfo->fw_path, "_ibss") != NULL)
		return DHD_FLAG_IBSS_MODE;
	if (strstr(dhdinfo->fw_path, "_mfg") != NULL)
		return DHD_FLAG_MFG_MODE;

	return DHD_FLAG_STA_MODE;
}

bool dhd_update_fw_nv_path(dhd_info_t *dhdinfo)
{
	int fw_len;
	int nv_len;
	const char *fw = NULL;
	const char *nv = NULL;
	wifi_adapter_info_t *adapter = dhdinfo->adapter;



	
	if (!dhd_download_fw_on_driverload) {
#ifdef CONFIG_BCMDHD_FW_PATH
		fw = CONFIG_BCMDHD_FW_PATH;
#endif 
#ifdef CONFIG_BCMDHD_NVRAM_PATH
		nv = CONFIG_BCMDHD_NVRAM_PATH;
#endif 
	}

	
	if (dhdinfo->fw_path[0] == '\0') {
		if (adapter && adapter->fw_path && adapter->fw_path[0] != '\0')
			fw = adapter->fw_path;

	}
	if (dhdinfo->nv_path[0] == '\0') {
		if (adapter && adapter->nv_path && adapter->nv_path[0] != '\0')
			nv = adapter->nv_path;
	}

	if (firmware_path[0] != '\0')
		fw = firmware_path;
	if (nvram_path[0] != '\0')
		nv = nvram_path;

	if (fw && fw[0] != '\0') {
		fw_len = strlen(fw);
		if (fw_len >= sizeof(dhdinfo->fw_path)) {
			DHD_ERROR(("fw path len exceeds max len of dhdinfo->fw_path\n"));
			return FALSE;
		}
		strncpy(dhdinfo->fw_path, fw, sizeof(dhdinfo->fw_path));
		if (dhdinfo->fw_path[fw_len-1] == '\n')
		       dhdinfo->fw_path[fw_len-1] = '\0';
	}
	if (nv && nv[0] != '\0') {
		nv_len = strlen(nv);
		if (nv_len >= sizeof(dhdinfo->nv_path)) {
			DHD_ERROR(("nvram path len exceeds max len of dhdinfo->nv_path\n"));
			return FALSE;
		}
		strncpy(dhdinfo->nv_path, nv, sizeof(dhdinfo->nv_path));
		if (dhdinfo->nv_path[nv_len-1] == '\n')
		       dhdinfo->nv_path[nv_len-1] = '\0';
	}
#ifndef CUSTOMER_HW_ONE
	
	firmware_path[0] = '\0';
	nvram_path[0] = '\0';
#endif 

#ifndef BCMEMBEDIMAGE
	
	if (dhdinfo->fw_path[0] == '\0') {
		DHD_ERROR(("firmware path not found\n"));
		return FALSE;
	}
	if (dhdinfo->nv_path[0] == '\0') {
		DHD_ERROR(("nvram path not found\n"));
		return FALSE;
	}
#endif 

	return TRUE;
}


int
dhd_bus_start(dhd_pub_t *dhdp)
{
	int ret = -1;
	dhd_info_t *dhd = (dhd_info_t*)dhdp->info;
	unsigned long flags;

	ASSERT(dhd);

	DHD_TRACE(("Enter %s:\n", __FUNCTION__));

	DHD_PERIM_LOCK(dhdp);

	
	if  (dhd->pub.busstate == DHD_BUS_DOWN && dhd_update_fw_nv_path(dhd)) {
		DHD_ERROR(("%s download fw %s, nv %s\n", __FUNCTION__, dhd->fw_path, dhd->nv_path));
		ret = dhd_bus_download_firmware(dhd->pub.bus, dhd->pub.osh,
		                                dhd->fw_path, dhd->nv_path);
		if (ret < 0) {
			DHD_ERROR(("%s: failed to download firmware %s\n",
			          __FUNCTION__, dhd->fw_path));
			DHD_PERIM_UNLOCK(dhdp);
			return ret;
		}
	}
	if (dhd->pub.busstate != DHD_BUS_LOAD) {
		DHD_PERIM_UNLOCK(dhdp);
		return -ENETDOWN;
	}

	dhd_os_sdlock(dhdp);

	
	dhd->pub.tickcnt = 0;
	dhd_os_wd_timer(&dhd->pub, dhd_watchdog_ms);

	
	if ((ret = dhd_bus_init(&dhd->pub, FALSE)) != 0) {

		DHD_ERROR(("%s, dhd_bus_init failed %d\n", __FUNCTION__, ret));
		dhd_os_sdunlock(dhdp);
		DHD_PERIM_UNLOCK(dhdp);
		return ret;
	}
#if defined(OOB_INTR_ONLY) || defined(BCMPCIE_OOB_HOST_WAKE)
#if defined(BCMPCIE_OOB_HOST_WAKE)
	dhd_os_sdunlock(dhdp);
#endif 
	
	if (dhd_bus_oob_intr_register(dhdp)) {
		
#if !defined(BCMPCIE_OOB_HOST_WAKE)
		DHD_GENERAL_LOCK(&dhd->pub, flags);
		dhd->wd_timer_valid = FALSE;
		DHD_GENERAL_UNLOCK(&dhd->pub, flags);
		del_timer_sync(&dhd->timer);

		dhd_os_sdunlock(dhdp);
#endif 
		DHD_PERIM_UNLOCK(dhdp);
		DHD_OS_WD_WAKE_UNLOCK(&dhd->pub);
		DHD_ERROR(("%s Host failed to register for OOB\n", __FUNCTION__));
		return -ENODEV;
	}

#if defined(BCMPCIE_OOB_HOST_WAKE)
	dhd_os_sdlock(dhdp);
	dhd_bus_oob_intr_set(dhdp, TRUE);
#else
	
	dhd_enable_oob_intr(dhd->pub.bus, TRUE);
#endif 
#endif 
#ifdef PCIE_FULL_DONGLE
	{
		uint8 txpush = 0;
		uint32 num_flowrings; 
		num_flowrings = dhd_bus_max_h2d_queues(dhd->pub.bus, &txpush);
		DHD_ERROR(("%s: Initializing %u flowrings\n", __FUNCTION__,
			num_flowrings));
		if ((ret = dhd_flow_rings_init(&dhd->pub, num_flowrings)) != BCME_OK) {
			dhd_os_sdunlock(dhdp);
			DHD_PERIM_UNLOCK(dhdp);
			return ret;
		}
	}
#endif 

	
	dhd_prot_init(&dhd->pub);

	
	if (dhd->pub.busstate != DHD_BUS_DATA) {
		DHD_GENERAL_LOCK(&dhd->pub, flags);
		dhd->wd_timer_valid = FALSE;
		DHD_GENERAL_UNLOCK(&dhd->pub, flags);
		del_timer_sync(&dhd->timer);
		DHD_ERROR(("%s failed bus is not ready\n", __FUNCTION__));
		dhd_os_sdunlock(dhdp);
		DHD_PERIM_UNLOCK(dhdp);
		DHD_OS_WD_WAKE_UNLOCK(&dhd->pub);
		return -ENODEV;
	}

	dhd_os_sdunlock(dhdp);

	
	if ((ret = dhd_sync_with_dongle(&dhd->pub)) < 0) {
		DHD_PERIM_UNLOCK(dhdp);
		return ret;
	}

#ifdef CUSTOMER_HW_ONE
	priv_dhdp = dhdp;
	dhd->dhd_force_exit = FALSE;
#endif
#ifdef ARP_OFFLOAD_SUPPORT
	if (dhd->pend_ipaddr) {
#ifdef AOE_IP_ALIAS_SUPPORT
		aoe_update_host_ipv4_table(&dhd->pub, dhd->pend_ipaddr, TRUE, 0);
#endif 
		dhd->pend_ipaddr = 0;
	}
#endif 

	DHD_PERIM_UNLOCK(dhdp);
	return 0;
}
#ifdef WLTDLS
int _dhd_tdls_enable(dhd_pub_t *dhd, bool tdls_on, bool auto_on, struct ether_addr *mac)
{
	char iovbuf[WLC_IOCTL_SMLEN];
	uint32 tdls = tdls_on;
	int ret = 0;
	uint32 tdls_auto_op = 0;
	uint32 tdls_idle_time = CUSTOM_TDLS_IDLE_MODE_SETTING;
	int32 tdls_rssi_high = CUSTOM_TDLS_RSSI_THRESHOLD_HIGH;
	int32 tdls_rssi_low = CUSTOM_TDLS_RSSI_THRESHOLD_LOW;
	BCM_REFERENCE(mac);
	if (!FW_SUPPORTED(dhd, tdls))
		return BCME_ERROR;

	if (dhd->tdls_enable == tdls_on)
		goto auto_mode;
	bcm_mkiovar("tdls_enable", (char *)&tdls, sizeof(tdls), iovbuf, sizeof(iovbuf));
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0) {
		DHD_ERROR(("%s: tdls %d failed %d\n", __FUNCTION__, tdls, ret));
		goto exit;
	}
	dhd->tdls_enable = tdls_on;
auto_mode:

	tdls_auto_op = auto_on;
	bcm_mkiovar("tdls_auto_op", (char *)&tdls_auto_op, sizeof(tdls_auto_op),
		iovbuf, sizeof(iovbuf));
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf,
		sizeof(iovbuf), TRUE, 0)) < 0) {
		DHD_ERROR(("%s: tdls_auto_op failed %d\n", __FUNCTION__, ret));
		goto exit;
	}

	if (tdls_auto_op) {
		bcm_mkiovar("tdls_idle_time", (char *)&tdls_idle_time,
			sizeof(tdls_idle_time),	iovbuf, sizeof(iovbuf));
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf,
			sizeof(iovbuf), TRUE, 0)) < 0) {
			DHD_ERROR(("%s: tdls_idle_time failed %d\n", __FUNCTION__, ret));
			goto exit;
		}
		bcm_mkiovar("tdls_rssi_high", (char *)&tdls_rssi_high, 4, iovbuf, sizeof(iovbuf));
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf,
			sizeof(iovbuf), TRUE, 0)) < 0) {
			DHD_ERROR(("%s: tdls_rssi_high failed %d\n", __FUNCTION__, ret));
			goto exit;
		}
		bcm_mkiovar("tdls_rssi_low", (char *)&tdls_rssi_low, 4, iovbuf, sizeof(iovbuf));
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf,
			sizeof(iovbuf), TRUE, 0)) < 0) {
			DHD_ERROR(("%s: tdls_rssi_low failed %d\n", __FUNCTION__, ret));
			goto exit;
		}
	}

exit:
	return ret;
}
int dhd_tdls_enable(struct net_device *dev, bool tdls_on, bool auto_on, struct ether_addr *mac)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	int ret = 0;
	if (dhd)
		ret = _dhd_tdls_enable(&dhd->pub, tdls_on, auto_on, mac);
	else
		ret = BCME_ERROR;
	return ret;
}
#ifdef PCIE_FULL_DONGLE
void dhd_tdls_update_peer_info(struct net_device *dev, bool connect, uint8 *da)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	dhd_pub_t *dhdp =  (dhd_pub_t *)&dhd->pub;
	tdls_peer_node_t *cur = dhdp->peer_tbl.node;
	tdls_peer_node_t *new = NULL, *prev = NULL;
	dhd_if_t *dhdif;
	uint8 sa[ETHER_ADDR_LEN];
	int ifidx = dhd_net2idx(dhd, dev);

	if (ifidx == DHD_BAD_IF)
		return;

	dhdif = dhd->iflist[ifidx];
	memcpy(sa, dhdif->mac_addr, ETHER_ADDR_LEN);

	if (connect) {
		while (cur != NULL) {
			if (!memcmp(da, cur->addr, ETHER_ADDR_LEN)) {
				DHD_ERROR(("%s: TDLS Peer exist already %d\n",
					__FUNCTION__, __LINE__));
				return;
			}
			cur = cur->next;
		}

		new = MALLOC(dhdp->osh, sizeof(tdls_peer_node_t));
		if (new == NULL) {
			DHD_ERROR(("%s: Failed to allocate memory\n", __FUNCTION__));
			return;
		}
		memcpy(new->addr, da, ETHER_ADDR_LEN);
		new->next = dhdp->peer_tbl.node;
		dhdp->peer_tbl.node = new;
		dhdp->peer_tbl.tdls_peer_count++;

	} else {
		while (cur != NULL) {
			if (!memcmp(da, cur->addr, ETHER_ADDR_LEN)) {
				dhd_flow_rings_delete_for_peer(dhdp, ifidx, da);
				if (prev)
					prev->next = cur->next;
				else
					dhdp->peer_tbl.node = cur->next;
				MFREE(dhdp->osh, cur, sizeof(tdls_peer_node_t));
				dhdp->peer_tbl.tdls_peer_count--;
				return;
			}
			prev = cur;
			cur = cur->next;
		}
		DHD_ERROR(("%s: TDLS Peer Entry Not found\n", __FUNCTION__));
	}
}
#endif 
#endif 

bool dhd_is_concurrent_mode(dhd_pub_t *dhd)
{
	if (!dhd)
		return FALSE;

	if (dhd->op_mode & DHD_FLAG_CONCURR_MULTI_CHAN_MODE)
		return TRUE;
	else if ((dhd->op_mode & DHD_FLAG_CONCURR_SINGLE_CHAN_MODE) ==
		DHD_FLAG_CONCURR_SINGLE_CHAN_MODE)
		return TRUE;
	else
		return FALSE;
}
#if !defined(AP) && defined(WLP2P)
uint32
dhd_get_concurrent_capabilites(dhd_pub_t *dhd)
{
	int32 ret = 0;
	char buf[WLC_IOCTL_SMLEN];
	bool mchan_supported = FALSE;
	if (dhd->op_mode & (DHD_FLAG_HOSTAP_MODE | DHD_FLAG_MFG_MODE))
		return 0;
	if (FW_SUPPORTED(dhd, vsdb)) {
		mchan_supported = TRUE;
	}
	if (!FW_SUPPORTED(dhd, p2p)) {
		DHD_TRACE(("Chip does not support p2p\n"));
		return 0;
	}
	else {
		
		memset(buf, 0, sizeof(buf));
		bcm_mkiovar("p2p", 0, 0, buf, sizeof(buf));
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, buf, sizeof(buf),
			FALSE, 0)) < 0) {
			DHD_ERROR(("%s: Get P2P failed (error=%d)\n", __FUNCTION__, ret));
			return 0;
		}
		else {
			if (buf[0] == 1) {
				ret = DHD_FLAG_CONCURR_SINGLE_CHAN_MODE;
				if (mchan_supported)
					ret |= DHD_FLAG_CONCURR_MULTI_CHAN_MODE;
#if defined(WL_ENABLE_P2P_IF) || defined(WL_CFG80211_P2P_DEV_IF)
				return ret;
#else
				return 0;
#endif 
			}
		}
	}
	return 0;
}
#endif 

#ifdef SUPPORT_AP_POWERSAVE
#define RXCHAIN_PWRSAVE_PPS			10
#define RXCHAIN_PWRSAVE_QUIET_TIME		10
#define RXCHAIN_PWRSAVE_STAS_ASSOC_CHECK	0
int dhd_set_ap_powersave(dhd_pub_t *dhdp, int ifidx, int enable)
{
	char iovbuf[128];
	int32 pps = RXCHAIN_PWRSAVE_PPS;
	int32 quiet_time = RXCHAIN_PWRSAVE_QUIET_TIME;
	int32 stas_assoc_check = RXCHAIN_PWRSAVE_STAS_ASSOC_CHECK;

	if (enable) {
		bcm_mkiovar("rxchain_pwrsave_enable", (char *)&enable, 4, iovbuf, sizeof(iovbuf));
		if (dhd_wl_ioctl_cmd(dhdp, WLC_SET_VAR,
		    iovbuf, sizeof(iovbuf), TRUE, 0) != BCME_OK) {
			DHD_ERROR(("Failed to enable AP power save"));
		}
		bcm_mkiovar("rxchain_pwrsave_pps", (char *)&pps, 4, iovbuf, sizeof(iovbuf));
		if (dhd_wl_ioctl_cmd(dhdp, WLC_SET_VAR,
		    iovbuf, sizeof(iovbuf), TRUE, 0) != BCME_OK) {
			DHD_ERROR(("Failed to set pps"));
		}
		bcm_mkiovar("rxchain_pwrsave_quiet_time", (char *)&quiet_time,
		4, iovbuf, sizeof(iovbuf));
		if (dhd_wl_ioctl_cmd(dhdp, WLC_SET_VAR,
		    iovbuf, sizeof(iovbuf), TRUE, 0) != BCME_OK) {
			DHD_ERROR(("Failed to set quiet time"));
		}
		bcm_mkiovar("rxchain_pwrsave_stas_assoc_check", (char *)&stas_assoc_check,
		4, iovbuf, sizeof(iovbuf));
		if (dhd_wl_ioctl_cmd(dhdp, WLC_SET_VAR,
		    iovbuf, sizeof(iovbuf), TRUE, 0) != BCME_OK) {
			DHD_ERROR(("Failed to set stas assoc check"));
		}
	} else {
		bcm_mkiovar("rxchain_pwrsave_enable", (char *)&enable, 4, iovbuf, sizeof(iovbuf));
		if (dhd_wl_ioctl_cmd(dhdp, WLC_SET_VAR,
		    iovbuf, sizeof(iovbuf), TRUE, 0) != BCME_OK) {
			DHD_ERROR(("Failed to disable AP power save"));
		}
	}

	return 0;
}
#endif 


#if defined(READ_CONFIG_FROM_FILE)
#include <linux/fs.h>
#include <linux/ctype.h>

#define strtoul(nptr, endptr, base) bcm_strtoul((nptr), (endptr), (base))
bool PM_control = TRUE;

static int dhd_preinit_proc(dhd_pub_t *dhd, int ifidx, char *name, char *value)
{
	int var_int;
	wl_country_t cspec = {{0}, -1, {0}};
	char *revstr;
	char *endptr = NULL;
	int iolen;
	char smbuf[WLC_IOCTL_SMLEN*2];

	if (!strcmp(name, "country")) {
		revstr = strchr(value, '/');
		if (revstr) {
			cspec.rev = strtoul(revstr + 1, &endptr, 10);
			memcpy(cspec.country_abbrev, value, WLC_CNTRY_BUF_SZ);
			cspec.country_abbrev[2] = '\0';
			memcpy(cspec.ccode, cspec.country_abbrev, WLC_CNTRY_BUF_SZ);
		} else {
			cspec.rev = -1;
			memcpy(cspec.country_abbrev, value, WLC_CNTRY_BUF_SZ);
			memcpy(cspec.ccode, value, WLC_CNTRY_BUF_SZ);
			get_customized_country_code(dhd->info->adapter,
				(char *)&cspec.country_abbrev, &cspec);
		}
		memset(smbuf, 0, sizeof(smbuf));
		DHD_ERROR(("config country code is country : %s, rev : %d !!\n",
			cspec.country_abbrev, cspec.rev));
		iolen = bcm_mkiovar("country", (char*)&cspec, sizeof(cspec),
			smbuf, sizeof(smbuf));
		return dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR,
			smbuf, iolen, TRUE, 0);
	} else if (!strcmp(name, "roam_scan_period")) {
		var_int = (int)simple_strtol(value, NULL, 0);
		return dhd_wl_ioctl_cmd(dhd, WLC_SET_ROAM_SCAN_PERIOD,
			&var_int, sizeof(var_int), TRUE, 0);
	} else if (!strcmp(name, "roam_delta")) {
		struct {
			int val;
			int band;
		} x;
		x.val = (int)simple_strtol(value, NULL, 0);
		
		x.band = WLC_BAND_ALL;
		return dhd_wl_ioctl_cmd(dhd, WLC_SET_ROAM_DELTA, &x, sizeof(x), TRUE, 0);
	} else if (!strcmp(name, "roam_trigger")) {
		int ret = 0;

		roam_trigger[0] = (int)simple_strtol(value, NULL, 0);
		roam_trigger[1] = WLC_BAND_ALL;
		ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_ROAM_TRIGGER, &roam_trigger,
			sizeof(roam_trigger), TRUE, 0);

		return ret;
	} else if (!strcmp(name, "PM")) {
		int ret = 0;
		var_int = (int)simple_strtol(value, NULL, 0);

		ret =  dhd_wl_ioctl_cmd(dhd, WLC_SET_PM,
			&var_int, sizeof(var_int), TRUE, 0);

#if defined(CONFIG_PM_LOCK)
		if (var_int == 0) {
			g_pm_control = TRUE;
			DHD_ERROR(("%s var_int=%d don't control PM\n", __func__, var_int));
		} else {
			g_pm_control = FALSE;
			DHD_ERROR(("%s var_int=%d do control PM\n", __func__, var_int));
		}
#endif

		return ret;
	}
#ifdef WLBTAMP
	else if (!strcmp(name, "btamp_chan")) {
		int btamp_chan;
		int iov_len = 0;
		char iovbuf[128];
		int ret;

		btamp_chan = (int)simple_strtol(value, NULL, 0);
		iov_len = bcm_mkiovar("btamp_chan", (char *)&btamp_chan, 4, iovbuf, sizeof(iovbuf));
		if ((ret  = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, iov_len, TRUE, 0) < 0))
			DHD_ERROR(("%s btamp_chan=%d set failed code %d\n",
				__FUNCTION__, btamp_chan, ret));
		else
			DHD_ERROR(("%s btamp_chan %d set success\n",
				__FUNCTION__, btamp_chan));
	}
#endif 
	else if (!strcmp(name, "band")) {
		int ret;
		if (!strcmp(value, "auto"))
			var_int = WLC_BAND_AUTO;
		else if (!strcmp(value, "a"))
			var_int = WLC_BAND_5G;
		else if (!strcmp(value, "b"))
			var_int = WLC_BAND_2G;
		else if (!strcmp(value, "all"))
			var_int = WLC_BAND_ALL;
		else {
			DHD_ERROR((" set band value should be one of the a or b or all\n"));
			var_int = WLC_BAND_AUTO;
		}
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_BAND, &var_int,
			sizeof(var_int), TRUE, 0)) < 0)
			DHD_ERROR((" set band err=%d\n", ret));
		return ret;
	} else if (!strcmp(name, "cur_etheraddr")) {
		struct ether_addr ea;
		char buf[32];
		uint iovlen;
		int ret;

		bcm_ether_atoe(value, &ea);

		ret = memcmp(&ea.octet, dhd->mac.octet, ETHER_ADDR_LEN);
		if (ret == 0) {
			DHD_ERROR(("%s: Same Macaddr\n", __FUNCTION__));
			return 0;
		}

		DHD_ERROR(("%s: Change Macaddr = %02X:%02X:%02X:%02X:%02X:%02X\n", __FUNCTION__,
			ea.octet[0], ea.octet[1], ea.octet[2],
			ea.octet[3], ea.octet[4], ea.octet[5]));

		iovlen = bcm_mkiovar("cur_etheraddr", (char*)&ea, ETHER_ADDR_LEN, buf, 32);

		ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, buf, iovlen, TRUE, 0);
		if (ret < 0) {
			DHD_ERROR(("%s: can't set MAC address , error=%d\n", __FUNCTION__, ret));
			return ret;
		}
		else {
			memcpy(dhd->mac.octet, (void *)&ea, ETHER_ADDR_LEN);
			return ret;
		}
	} else if (!strcmp(name, "lpc")) {
		int ret = 0;
		char buf[32];
		uint iovlen;
		var_int = (int)simple_strtol(value, NULL, 0);
		if (dhd_wl_ioctl_cmd(dhd, WLC_DOWN, NULL, 0, TRUE, 0) < 0) {
			DHD_ERROR(("%s: wl down failed\n", __FUNCTION__));
		}
		iovlen = bcm_mkiovar("lpc", (char *)&var_int, 4, buf, sizeof(buf));
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, buf, iovlen, TRUE, 0)) < 0) {
			DHD_ERROR(("%s Set lpc failed  %d\n", __FUNCTION__, ret));
		}
		if (dhd_wl_ioctl_cmd(dhd, WLC_UP, NULL, 0, TRUE, 0) < 0) {
			DHD_ERROR(("%s: wl up failed\n", __FUNCTION__));
		}
		return ret;
	} else if (!strcmp(name, "vht_features")) {
		int ret = 0;
		char buf[32];
		uint iovlen;
		var_int = (int)simple_strtol(value, NULL, 0);

		if (dhd_wl_ioctl_cmd(dhd, WLC_DOWN, NULL, 0, TRUE, 0) < 0) {
			DHD_ERROR(("%s: wl down failed\n", __FUNCTION__));
		}
		iovlen = bcm_mkiovar("vht_features", (char *)&var_int, 4, buf, sizeof(buf));
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, buf, iovlen, TRUE, 0)) < 0) {
			DHD_ERROR(("%s Set vht_features failed  %d\n", __FUNCTION__, ret));
		}
		if (dhd_wl_ioctl_cmd(dhd, WLC_UP, NULL, 0, TRUE, 0) < 0) {
			DHD_ERROR(("%s: wl up failed\n", __FUNCTION__));
		}
		return ret;
	} else {
		uint iovlen;
		char iovbuf[WLC_IOCTL_SMLEN];

		
		var_int = (int)simple_strtol(value, NULL, 0);

		
		if (!strcmp(name, "roam_off")) {
			
			if (var_int) {
				uint bcn_timeout = 2;
				bcm_mkiovar("bcn_timeout", (char *)&bcn_timeout, 4,
					iovbuf, sizeof(iovbuf));
				dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
			}
		}
		

		DHD_INFO(("%s:[%s]=[%d]\n", __FUNCTION__, name, var_int));

		iovlen = bcm_mkiovar(name, (char *)&var_int, sizeof(var_int),
			iovbuf, sizeof(iovbuf));
		return dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR,
			iovbuf, iovlen, TRUE, 0);
	}

	return 0;
}

static int dhd_preinit_config(dhd_pub_t *dhd, int ifidx)
{
	mm_segment_t old_fs;
	struct kstat stat;
	struct file *fp = NULL;
	unsigned int len;
	char *buf = NULL, *p, *name, *value;
	int ret = 0;
	char *config_path;

	config_path = CONFIG_BCMDHD_CONFIG_PATH;

	if (!config_path)
	{
		DHD_ERROR(("config_path can't read. \n"));
		return 0;
	}

	old_fs = get_fs();
	set_fs(get_ds());
	if ((ret = vfs_stat(config_path, &stat))) {
		set_fs(old_fs);
		DHD_ERROR(("%s: Failed to get information (%d)\n",
			config_path, ret));
		return ret;
	}
	set_fs(old_fs);

	if (!(buf = MALLOC(dhd->osh, stat.size + 1))) {
		DHD_ERROR(("Failed to allocate memory %llu bytes\n", stat.size));
		return -ENOMEM;
	}

	DHD_ERROR(("dhd_preinit_config : config path : %s \n", config_path));

	if (!(fp = dhd_os_open_image(config_path)) ||
		(len = dhd_os_get_image_block(buf, stat.size, fp)) < 0)
		goto err;

	buf[stat.size] = '\0';
	for (p = buf; *p; p++) {
		if (isspace(*p))
			continue;
		for (name = p++; *p && !isspace(*p); p++) {
			if (*p == '=') {
				*p = '\0';
				p++;
				for (value = p; *p && !isspace(*p); p++);
				*p = '\0';
				if ((ret = dhd_preinit_proc(dhd, ifidx, name, value)) < 0) {
					DHD_ERROR(("%s: %s=%s\n",
						bcmerrorstr(ret), name, value));
				}
				break;
			}
		}
	}
	ret = 0;

out:
	if (fp)
		dhd_os_close_image(fp);
	if (buf)
		MFREE(dhd->osh, buf, stat.size+1);
	return ret;

err:
	ret = -1;
	goto out;
}
#endif 

int
dhd_preinit_ioctls(dhd_pub_t *dhd)
{
	int ret = 0;
	char eventmask[WL_EVENTING_MASK_LEN];
	char iovbuf[WL_EVENTING_MASK_LEN + 12];	
	uint32 buf_key_b4_m4 = 1;
	uint8 msglen;
	eventmsgs_ext_t *eventmask_msg = NULL;
	char* iov_buf = NULL;
	int ret2 = 0;
#ifdef WLAIBSS
	aibss_bcn_force_config_t bcn_config;
	uint32 aibss;
#ifdef WLAIBSS_PS
	uint32 aibss_ps;
#endif 
#endif 
#if defined(BCMSUP_4WAY_HANDSHAKE) && defined(WLAN_AKM_SUITE_FT_8021X)
	uint32 sup_wpa = 0;
#endif
#if defined(CUSTOM_AMPDU_BA_WSIZE) || (defined(WLAIBSS) && \
	defined(CUSTOM_IBSS_AMPDU_BA_WSIZE))
	uint32 ampdu_ba_wsize = 0;
#endif 
#if defined(CUSTOM_AMPDU_MPDU)
	int32 ampdu_mpdu = 0;
#endif
#if defined(CUSTOM_AMPDU_RELEASE)
	int32 ampdu_release = 0;
#endif
#if defined(CUSTOM_AMSDU_AGGSF)
	int32 amsdu_aggsf = 0;
#endif

#if defined(BCMSDIO)
#ifdef PROP_TXSTATUS
	int wlfc_enable = TRUE;
#ifndef DISABLE_11N
	uint32 hostreorder = 1;
#endif 
#endif 
#endif 
#ifdef PCIE_FULL_DONGLE
	uint32 wl_ap_isolate;
#endif 

#ifdef DHD_ENABLE_LPC
	uint32 lpc = 1;
#endif 
	uint power_mode = PM_FAST;
#if defined(BCMSDIO)
	uint32 dongle_align = DHD_SDALIGN;
	uint32 glom = CUSTOM_GLOM_SETTING;
#endif 
#if defined(CUSTOMER_HW2) && defined(USE_WL_CREDALL)
	uint32 credall = 1;
#endif
#if defined(VSDB) || defined(ROAM_ENABLE)
	uint bcn_timeout = CUSTOM_BCN_TIMEOUT;
#else
	uint bcn_timeout = 4;
#endif 
#ifdef CUSTOMER_HW_ONE
	uint retry_max = 10;
#else
	uint retry_max = 3;
#endif
#if defined(ARP_OFFLOAD_SUPPORT)
	int arpoe = 1;
#endif
	int scan_assoc_time = DHD_SCAN_ASSOC_ACTIVE_TIME;
	int scan_unassoc_time = DHD_SCAN_UNASSOC_ACTIVE_TIME;
	int scan_passive_time = DHD_SCAN_PASSIVE_TIME;
	char buf[WLC_IOCTL_SMLEN];
	char *ptr;
	uint32 listen_interval = CUSTOM_LISTEN_INTERVAL; 
#ifdef ROAM_ENABLE
	uint roamvar = 0;
	int roam_trigger[2] = {CUSTOM_ROAM_TRIGGER_SETTING, WLC_BAND_ALL};
#ifdef CUSTOMER_HW_ONE
	int roam_scan_period[2] = {30, WLC_BAND_ALL};
#else
	int roam_scan_period[2] = {10, WLC_BAND_ALL};
#endif
	int roam_delta[2] = {CUSTOM_ROAM_DELTA_SETTING, WLC_BAND_ALL};
#ifdef FULL_ROAMING_SCAN_PERIOD_60_SEC
	int roam_fullscan_period = 60;
#else 
	int roam_fullscan_period = 120;
#endif 
#else
#ifdef DISABLE_BUILTIN_ROAM
	uint roamvar = 1;
#endif 
#endif 

#if defined(SOFTAP)
	uint dtim = 1;
#endif
#if (defined(AP) && !defined(WLP2P)) || (!defined(AP) && defined(WL_CFG80211))
	uint32 mpc = 0; 
	struct ether_addr p2p_ea;
#endif
#ifdef BCMCCX
	uint32 ccx = 1;
#endif
#ifdef SOFTAP_UAPSD_OFF
	uint32 wme_apsd = 0;
#endif 
#if (defined(AP) || defined(WLP2P)) && !defined(SOFTAP_AND_GC)
	uint32 apsta = 1; 
#elif defined(SOFTAP_AND_GC)
	uint32 apsta = 0;
	int ap_mode = 1;
#endif 
#ifdef GET_CUSTOM_MAC_ENABLE
	struct ether_addr ea_addr;
#endif 

#ifdef DISABLE_11N
	uint32 nmode = 0;
#endif 
#if defined(DISABLE_LDPC)
	uint32 ldpc = 0;
#endif 

#if defined(DISABLE_11AC)
	uint32 vhtmode = 0;
#endif 
#ifdef USE_WL_TXBF
	uint32 txbf = 1;
#endif 
#ifdef AMPDU_VO_ENABLE
	struct ampdu_tid_control tid;
#endif
#ifdef USE_WL_FRAMEBURST
	uint32 frameburst = 1;
#endif 
#ifdef DHD_SET_FW_HIGHSPEED
	uint32 ack_ratio = 250;
	uint32 ack_ratio_depth = 64;
#endif 
#ifdef CUSTOMER_HW_ONE
	int ht_wsec_restrict = WLC_HT_TKIP_RESTRICT | WLC_HT_WEP_RESTRICT;
	
	
	int txchain = 1;
	int rxchain = 1;
#endif
#ifdef SUPPORT_2G_HT40
	struct {
		u32 band;
		u32 bw_cap;
	} param = {0, 0};
#endif 
#ifdef SUPPORT_2G_VHT
	uint32 vht_features = 0x3; 
#endif 
#ifdef CUSTOM_PSPRETEND_THR
	uint32 pspretend_thr = CUSTOM_PSPRETEND_THR;
#endif
#ifdef PKT_FILTER_SUPPORT
	dhd_pkt_filter_enable = TRUE;
#endif 
#ifdef WLTDLS
	dhd->tdls_enable = FALSE;
#endif 
#ifdef DONGLE_ENABLE_ISOLATION
	dhd->dongle_isolation = TRUE;
#endif 
	dhd->suspend_bcn_li_dtim = CUSTOM_SUSPEND_BCN_LI_DTIM;
	DHD_TRACE(("Enter %s\n", __FUNCTION__));
	dhd->op_mode = 0;
#if defined(USE_STATIC_MEMDUMP)
	dhd->memdump_enabled = DUMP_MEMFILE_BUGON;
#endif 
	if ((!op_mode && dhd_get_fw_mode(dhd->info) == DHD_FLAG_MFG_MODE) ||
		(op_mode == DHD_FLAG_MFG_MODE)) {
		
		dhd_os_set_ioctl_resp_timeout(MFG_IOCTL_RESP_TIMEOUT);
		DHD_ERROR(("%s : Set IOCTL response time for Manufactring Firmware\n",
			__FUNCTION__));
	}
	else {
		dhd_os_set_ioctl_resp_timeout(IOCTL_RESP_TIMEOUT);
		DHD_INFO(("%s : Set IOCTL response time.\n", __FUNCTION__));
	}
#ifdef CUSTOMER_HW_ONE
	
	{
		int cis_source = 0;

		bcm_mkiovar("cis_source", 0, 0, iovbuf, sizeof(iovbuf));
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, iovbuf,
			sizeof(iovbuf), FALSE, 0)) < 0) {
			DHD_ERROR(("%s: Get cis_source failed (error=%d)\n",
				__FUNCTION__, ret));
		} else {
			cis_source = *(int*)iovbuf;
			DHD_ERROR(("%s: cis_source = %d\n", __FUNCTION__, cis_source));
		}
	}

	
	ret = dhd_wl_ioctl_cmd(dhd, WLC_DOWN, NULL, 0, TRUE, 0);
	if (ret < 0) {
		DHD_ERROR(("%s Setting WL DOWN failed %d\n", __FUNCTION__, ret));
		goto done;
	}

	
	bcm_mkiovar("ht_wsec_restrict", (char *)&ht_wsec_restrict, 4, iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);

	
	ret = 3;
	bcm_mkiovar("tc_period", (char *)&ret, 4, iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
	ret = TRAFFIC_LOW_WATER_MARK;
	bcm_mkiovar("tc_lo_wm", (char *)&ret, 4, iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
	ret = TRAFFIC_HIGH_WATER_MARK;
	bcm_mkiovar("tc_hi_wm", (char *)&ret, 4, iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
	ret = 1;
	bcm_mkiovar("tc_enable", (char *)&ret, 4, iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
#if defined(HTC_TX_TRACKING)
{
	
	uint tx_stat_chk = 0; 
	uint tx_stat_chk_prd = 5; 
	uint tx_stat_chk_ratio = 8; 
	uint tx_stat_chk_num = 5; 

	bcm_mkiovar("tx_stat_chk_num", (char *)&tx_stat_chk_num, 4, iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
	bcm_mkiovar("tx_stat_chk_ratio", (char *)&tx_stat_chk_ratio, 4, iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
	bcm_mkiovar("tx_stat_chk_prd", (char *)&tx_stat_chk_prd, 4, iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
	bcm_mkiovar("tx_stat_chk", (char *)&tx_stat_chk, 4, iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
}
#endif 

	
	
	

	DHD_ERROR(("%s: set txchain %d\n", __FUNCTION__, txchain));
	bcm_mkiovar("txchain", (char *)&txchain, 4, iovbuf, sizeof(iovbuf));
	ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
	if (ret < 0) {
		DHD_ERROR(("%s: set txchain fail, error=%d\n", __FUNCTION__, ret));
	}
	DHD_ERROR(("%s: set rxchain %d\n", __FUNCTION__, rxchain));
	bcm_mkiovar("rxchain", (char *)&rxchain, 4, iovbuf, sizeof(iovbuf));
	ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
	if (ret < 0) {
		DHD_ERROR(("%s: set rxchain fail, error=%d\n", __FUNCTION__, ret));
	}
#endif 
#ifdef GET_CUSTOM_MAC_ENABLE
	ret = wifi_platform_get_mac_addr(dhd->info->adapter, ea_addr.octet);
	if (!ret) {
		memset(buf, 0, sizeof(buf));
		bcm_mkiovar("cur_etheraddr", (void *)&ea_addr, ETHER_ADDR_LEN, buf, sizeof(buf));
		ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, buf, sizeof(buf), TRUE, 0);
		if (ret < 0) {
			DHD_ERROR(("%s: can't set MAC address , error=%d\n", __FUNCTION__, ret));
			ret = BCME_NOTUP;
			goto done;
		}
		memcpy(dhd->mac.octet, ea_addr.octet, ETHER_ADDR_LEN);
	} else {
#endif 
		
		memset(buf, 0, sizeof(buf));
		bcm_mkiovar("cur_etheraddr", 0, 0, buf, sizeof(buf));
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, buf, sizeof(buf),
			FALSE, 0)) < 0) {
			DHD_ERROR(("%s: can't get MAC address , error=%d\n", __FUNCTION__, ret));
			ret = BCME_NOTUP;
			goto done;
		}
		
		memcpy(dhd->mac.octet, buf, ETHER_ADDR_LEN);

#ifdef GET_CUSTOM_MAC_ENABLE
	}
#endif 

	
	memset(dhd->fw_capabilities, 0, sizeof(dhd->fw_capabilities));
	bcm_mkiovar("cap", 0, 0, dhd->fw_capabilities, sizeof(dhd->fw_capabilities));
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, dhd->fw_capabilities,
		sizeof(dhd->fw_capabilities), FALSE, 0)) < 0) {
		DHD_ERROR(("%s: Get Capability failed (error=%d)\n",
			__FUNCTION__, ret));
		goto done;
	}
	if ((!op_mode && dhd_get_fw_mode(dhd->info) == DHD_FLAG_HOSTAP_MODE) ||
		(op_mode == DHD_FLAG_HOSTAP_MODE)) {
#ifdef CUSTOMR_HW_ONE
		ampdu_ba_wsize = CUSTOM_AP_AMPDU_BA_WSIZE;
#endif
#ifdef SET_RANDOM_MAC_SOFTAP
		uint rand_mac;
#endif
		dhd->op_mode = DHD_FLAG_HOSTAP_MODE;
#if defined(ARP_OFFLOAD_SUPPORT)
			arpoe = 0;
#endif
#ifdef PKT_FILTER_SUPPORT
			dhd_pkt_filter_enable = FALSE;
#endif
#ifdef SET_RANDOM_MAC_SOFTAP
		SRANDOM32((uint)jiffies);
		rand_mac = RANDOM32();
		iovbuf[0] = 0x02;			   
		iovbuf[1] = 0x1A;
		iovbuf[2] = 0x11;
		iovbuf[3] = (unsigned char)(rand_mac & 0x0F) | 0xF0;
		iovbuf[4] = (unsigned char)(rand_mac >> 8);
		iovbuf[5] = (unsigned char)(rand_mac >> 16);

		bcm_mkiovar("cur_etheraddr", (void *)iovbuf, ETHER_ADDR_LEN, buf, sizeof(buf));
		ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, buf, sizeof(buf), TRUE, 0);
		if (ret < 0) {
			DHD_ERROR(("%s: can't set MAC address , error=%d\n", __FUNCTION__, ret));
		} else
			memcpy(dhd->mac.octet, iovbuf, ETHER_ADDR_LEN);
#endif 
#if !defined(AP) && defined(WL_CFG80211)
		
		bcm_mkiovar("mpc", (char *)&mpc, 4, iovbuf, sizeof(iovbuf));
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf,
			sizeof(iovbuf), TRUE, 0)) < 0) {
			DHD_ERROR(("%s mpc for HostAPD failed  %d\n", __FUNCTION__, ret));
		}
#endif
#ifdef SUPPORT_AP_POWERSAVE
		dhd_set_ap_powersave(dhd, 0, TRUE);
#endif
#ifdef SOFTAP_UAPSD_OFF
	bcm_mkiovar("wme_apsd", (char *)&wme_apsd, 4, iovbuf, sizeof(iovbuf));
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0)
		DHD_ERROR(("%s: set wme_apsd 0 fail (error=%d)\n", __FUNCTION__, ret));
#endif 
	} else if ((!op_mode && dhd_get_fw_mode(dhd->info) == DHD_FLAG_MFG_MODE) ||
		(op_mode == DHD_FLAG_MFG_MODE)) {
#if defined(ARP_OFFLOAD_SUPPORT)
		arpoe = 0;
#endif 
#ifdef PKT_FILTER_SUPPORT
		dhd_pkt_filter_enable = FALSE;
#endif 
		dhd->op_mode = DHD_FLAG_MFG_MODE;
	} else {
		uint32 concurrent_mode = 0;
		if ((!op_mode && dhd_get_fw_mode(dhd->info) == DHD_FLAG_P2P_MODE) ||
			(op_mode == DHD_FLAG_P2P_MODE)) {
#if defined(ARP_OFFLOAD_SUPPORT)
			arpoe = 0;
#endif
#ifdef PKT_FILTER_SUPPORT
			dhd_pkt_filter_enable = FALSE;
#endif
			dhd->op_mode = DHD_FLAG_P2P_MODE;
		} else if ((!op_mode && dhd_get_fw_mode(dhd->info) == DHD_FLAG_IBSS_MODE) ||
			(op_mode == DHD_FLAG_IBSS_MODE)) {
			dhd->op_mode = DHD_FLAG_IBSS_MODE;
		} else
			dhd->op_mode = DHD_FLAG_STA_MODE;
#if !defined(AP) && defined(WLP2P)
		if (dhd->op_mode != DHD_FLAG_IBSS_MODE &&
			(concurrent_mode = dhd_get_concurrent_capabilites(dhd))) {
#if defined(ARP_OFFLOAD_SUPPORT)
			arpoe = 1;
#endif
			dhd->op_mode |= concurrent_mode;
		}

		
		if (dhd->op_mode & DHD_FLAG_P2P_MODE) {
			bcm_mkiovar("apsta", (char *)&apsta, 4, iovbuf, sizeof(iovbuf));
			if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR,
				iovbuf, sizeof(iovbuf), TRUE, 0)) < 0) {
				DHD_ERROR(("%s APSTA for P2P failed ret= %d\n", __FUNCTION__, ret));
			}

#if defined(SOFTAP_AND_GC)
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_AP,
			(char *)&ap_mode, sizeof(ap_mode), TRUE, 0)) < 0) {
				DHD_ERROR(("%s WLC_SET_AP failed %d\n", __FUNCTION__, ret));
		}
#endif
			memcpy(&p2p_ea, &dhd->mac, ETHER_ADDR_LEN);
			ETHER_SET_LOCALADDR(&p2p_ea);
			bcm_mkiovar("p2p_da_override", (char *)&p2p_ea,
				ETHER_ADDR_LEN, iovbuf, sizeof(iovbuf));
			if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR,
				iovbuf, sizeof(iovbuf), TRUE, 0)) < 0) {
				DHD_ERROR(("%s p2p_da_override ret= %d\n", __FUNCTION__, ret));
			} else {
				DHD_INFO(("dhd_preinit_ioctls: p2p_da_override succeeded\n"));
			}
		}
#else
	(void)concurrent_mode;
#endif 
	}

#ifdef CUSTOMER_HW_ONE
	DHD_ERROR(("Firmware up: op_mode=0x%04x, Broadcom Dongle Host Driver\n",
		dhd->op_mode));
#else
	DHD_ERROR(("Firmware up: op_mode=0x%04x, MAC="MACDBG"\n",
		dhd->op_mode, MAC2STRDBG(dhd->mac.octet)));
#endif
	
	if (dhd->dhd_cspec.ccode[0] != 0) {
		DHD_ERROR(("set country \n"));
		bcm_mkiovar("country", (char *)&dhd->dhd_cspec,
			sizeof(wl_country_t), iovbuf, sizeof(iovbuf));
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0)
			DHD_ERROR(("%s: country code setting failed\n", __FUNCTION__));
	}

#if defined(DISABLE_11AC)
	bcm_mkiovar("vhtmode", (char *)&vhtmode, 4, iovbuf, sizeof(iovbuf));
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0)
		DHD_ERROR(("%s wl vhtmode 0 failed %d\n", __FUNCTION__, ret));
#endif 

	
	bcm_mkiovar("assoc_listen", (char *)&listen_interval, 4, iovbuf, sizeof(iovbuf));
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0)
		DHD_ERROR(("%s assoc_listen failed %d\n", __FUNCTION__, ret));

#if defined(ROAM_ENABLE) || defined(DISABLE_BUILTIN_ROAM)
	
	bcm_mkiovar("roam_off", (char *)&roamvar, 4, iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
#endif 
#if defined(ROAM_ENABLE)
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_ROAM_TRIGGER, roam_trigger,
		sizeof(roam_trigger), TRUE, 0)) < 0)
		DHD_ERROR(("%s: roam trigger set failed %d\n", __FUNCTION__, ret));
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_ROAM_SCAN_PERIOD, roam_scan_period,
		sizeof(roam_scan_period), TRUE, 0)) < 0)
		DHD_ERROR(("%s: roam scan period set failed %d\n", __FUNCTION__, ret));
	if ((dhd_wl_ioctl_cmd(dhd, WLC_SET_ROAM_DELTA, roam_delta,
		sizeof(roam_delta), TRUE, 0)) < 0)
		DHD_ERROR(("%s: roam delta set failed %d\n", __FUNCTION__, ret));
	bcm_mkiovar("fullroamperiod", (char *)&roam_fullscan_period, 4, iovbuf, sizeof(iovbuf));
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0)
		DHD_ERROR(("%s: roam fullscan period set failed %d\n", __FUNCTION__, ret));
#endif 

#ifdef BCMCCX
	bcm_mkiovar("ccx_enable", (char *)&ccx, 4, iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
#endif 
#ifdef WLTDLS
	
	_dhd_tdls_enable(dhd, true, false, NULL);
#endif 

#ifdef DHD_ENABLE_LPC
	
	bcm_mkiovar("lpc", (char *)&lpc, 4, iovbuf, sizeof(iovbuf));
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf,
		sizeof(iovbuf), TRUE, 0)) < 0) {
		DHD_ERROR(("%s Set lpc failed  %d\n", __FUNCTION__, ret));
	}
#endif 

	
	dhd_wl_ioctl_cmd(dhd, WLC_SET_PM, (char *)&power_mode, sizeof(power_mode), TRUE, 0);

#if defined(BCMSDIO)
	
	bcm_mkiovar("bus:txglomalign", (char *)&dongle_align, 4, iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);

#if defined(CUSTOMER_HW2) && defined(USE_WL_CREDALL)
	
	bcm_mkiovar("bus:credall", (char *)&credall, 4, iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
#endif

	if (glom != DEFAULT_GLOM_VALUE) {
		DHD_INFO(("%s set glom=0x%X\n", __FUNCTION__, glom));
		bcm_mkiovar("bus:txglom", (char *)&glom, 4, iovbuf, sizeof(iovbuf));
		dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
	}
#endif 

	
	bcm_mkiovar("bcn_timeout", (char *)&bcn_timeout, 4, iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
	
	bcm_mkiovar("assoc_retry_max", (char *)&retry_max, 4, iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
#if defined(AP) && !defined(WLP2P)
	
	bcm_mkiovar("mpc", (char *)&mpc, 4, iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
	bcm_mkiovar("apsta", (char *)&apsta, 4, iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
#endif 


#if defined(SOFTAP)
	if (ap_fw_loaded == TRUE) {
		dhd_wl_ioctl_cmd(dhd, WLC_SET_DTIMPRD, (char *)&dtim, sizeof(dtim), TRUE, 0);
	}
#endif 

#if defined(KEEP_ALIVE)
	{
	
	int res;

#if defined(SOFTAP)
	if (ap_fw_loaded == FALSE)
#endif 
		if (!(dhd->op_mode &
			(DHD_FLAG_HOSTAP_MODE | DHD_FLAG_MFG_MODE))) {
			if ((res = dhd_keep_alive_onoff(dhd)) < 0)
				DHD_ERROR(("%s set keeplive failed %d\n",
				__FUNCTION__, res));
		}
#ifdef CUSTOMER_HW_ONE
#ifdef PKT_FILTER_SUPPORT
		
		dhd_pkt_filter_enable = TRUE;
		
#endif 
#endif 
	}
#endif 

#ifdef USE_WL_TXBF
	bcm_mkiovar("txbf", (char *)&txbf, 4, iovbuf, sizeof(iovbuf));
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf,
		sizeof(iovbuf), TRUE, 0)) < 0) {
		DHD_ERROR(("%s Set txbf failed  %d\n", __FUNCTION__, ret));
	}
#endif 
#ifdef USE_WL_FRAMEBURST
	
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_FAKEFRAG, (char *)&frameburst,
		sizeof(frameburst), TRUE, 0)) < 0) {
		DHD_ERROR(("%s Set frameburst failed  %d\n", __FUNCTION__, ret));
	}
#endif 
#ifdef DHD_SET_FW_HIGHSPEED
	
	bcm_mkiovar("ack_ratio", (char *)&ack_ratio, 4, iovbuf, sizeof(iovbuf));
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf,
		sizeof(iovbuf), TRUE, 0)) < 0) {
		DHD_ERROR(("%s Set ack_ratio failed  %d\n", __FUNCTION__, ret));
	}

	
	bcm_mkiovar("ack_ratio_depth", (char *)&ack_ratio_depth, 4, iovbuf, sizeof(iovbuf));
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf,
		sizeof(iovbuf), TRUE, 0)) < 0) {
		DHD_ERROR(("%s Set ack_ratio_depth failed  %d\n", __FUNCTION__, ret));
	}
#endif 
#if defined(CUSTOM_AMPDU_BA_WSIZE) || (defined(WLAIBSS) && \
	defined(CUSTOM_IBSS_AMPDU_BA_WSIZE))
	
#ifdef CUSTOM_AMPDU_BA_WSIZE
	ampdu_ba_wsize = CUSTOM_AMPDU_BA_WSIZE;
#endif
#if defined(WLAIBSS) && defined(CUSTOM_IBSS_AMPDU_BA_WSIZE)
	if (dhd->op_mode == DHD_FLAG_IBSS_MODE)
		ampdu_ba_wsize = CUSTOM_IBSS_AMPDU_BA_WSIZE;
#endif 
	if (ampdu_ba_wsize != 0) {
		bcm_mkiovar("ampdu_ba_wsize", (char *)&ampdu_ba_wsize, 4, iovbuf, sizeof(iovbuf));
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf,
			sizeof(iovbuf), TRUE, 0)) < 0) {
			DHD_ERROR(("%s Set ampdu_ba_wsize to %d failed  %d\n",
				__FUNCTION__, ampdu_ba_wsize, ret));
		}
	}
#endif 

	iov_buf = (char*)kmalloc(WLC_IOCTL_SMLEN, GFP_KERNEL);
	if (iov_buf == NULL) {
		DHD_ERROR(("failed to allocate %d bytes for iov_buf\n", WLC_IOCTL_SMLEN));
		ret = BCME_NOMEM;
		goto done;
	}
#ifdef WLAIBSS
	
	if (dhd->op_mode & DHD_FLAG_IBSS_MODE)
	{
		aibss = 1;
		bcm_mkiovar("aibss", (char *)&aibss, 4, iovbuf, sizeof(iovbuf));
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf,
			sizeof(iovbuf), TRUE, 0)) < 0) {
			DHD_ERROR(("%s Set aibss to %d failed  %d\n",
				__FUNCTION__, aibss, ret));
		}
#ifdef WLAIBSS_PS
		aibss_ps = 1;
		bcm_mkiovar("aibss_ps", (char *)&aibss_ps, 4, iovbuf, sizeof(iovbuf));
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf,
			sizeof(iovbuf), TRUE, 0)) < 0) {
			DHD_ERROR(("%s Set aibss PS to %d failed  %d\n",
				__FUNCTION__, aibss, ret));
		}
#endif 
	}
	memset(&bcn_config, 0, sizeof(bcn_config));
	bcn_config.initial_min_bcn_dur = AIBSS_INITIAL_MIN_BCN_DUR;
	bcn_config.min_bcn_dur = AIBSS_MIN_BCN_DUR;
	bcn_config.bcn_flood_dur = AIBSS_BCN_FLOOD_DUR;
	bcn_config.version = AIBSS_BCN_FORCE_CONFIG_VER_0;
	bcn_config.len = sizeof(bcn_config);

	bcm_mkiovar("aibss_bcn_force_config", (char *)&bcn_config,
		sizeof(aibss_bcn_force_config_t), iov_buf, WLC_IOCTL_SMLEN);
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iov_buf,
		WLC_IOCTL_SMLEN, TRUE, 0)) < 0) {
		DHD_ERROR(("%s Set aibss_bcn_force_config to %d, %d, %d failed %d\n",
			__FUNCTION__, AIBSS_INITIAL_MIN_BCN_DUR, AIBSS_MIN_BCN_DUR,
			AIBSS_BCN_FLOOD_DUR, ret));
	}
#endif 

#if defined(CUSTOM_AMPDU_MPDU)
	ampdu_mpdu = CUSTOM_AMPDU_MPDU;
	if (ampdu_mpdu != 0 && (ampdu_mpdu <= ampdu_ba_wsize)) {
		bcm_mkiovar("ampdu_mpdu", (char *)&ampdu_mpdu, 4, iovbuf, sizeof(iovbuf));
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf,
			sizeof(iovbuf), TRUE, 0)) < 0) {
			DHD_ERROR(("%s Set ampdu_mpdu to %d failed  %d\n",
				__FUNCTION__, CUSTOM_AMPDU_MPDU, ret));
		}
	}
#endif 

#if defined(CUSTOM_AMPDU_RELEASE)
	ampdu_release = CUSTOM_AMPDU_RELEASE;
	if (ampdu_release != 0 && (ampdu_release <= ampdu_ba_wsize)) {
		bcm_mkiovar("ampdu_release", (char *)&ampdu_release, 4, iovbuf, sizeof(iovbuf));
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf,
			sizeof(iovbuf), TRUE, 0)) < 0) {
			DHD_ERROR(("%s Set ampdu_release to %d failed  %d\n",
				__FUNCTION__, CUSTOM_AMPDU_RELEASE, ret));
		}
	}
#endif 

#if defined(CUSTOM_AMSDU_AGGSF)
	amsdu_aggsf = CUSTOM_AMSDU_AGGSF;
	if (amsdu_aggsf != 0) {
		bcm_mkiovar("amsdu_aggsf", (char *)&amsdu_aggsf, 4, iovbuf, sizeof(iovbuf));
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf,
			sizeof(iovbuf), TRUE, 0)) < 0) {
			DHD_ERROR(("%s Set amsdu_aggsf to %d failed  %d\n",
				__FUNCTION__, CUSTOM_AMSDU_AGGSF, ret));
		}
	}
#endif 

#if defined(BCMSUP_4WAY_HANDSHAKE) && defined(WLAN_AKM_SUITE_FT_8021X)
	
	if (dhd_use_idsup == 1) {
		bcm_mkiovar("sup_wpa", (char *)&sup_wpa, 4, iovbuf, sizeof(iovbuf));
		ret = dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, iovbuf, sizeof(iovbuf), FALSE, 0);
		if (ret >= 0 || ret == BCME_NOTREADY)
			dhd->fw_4way_handshake = TRUE;
		DHD_TRACE(("4-way handshake mode is: %d\n", dhd->fw_4way_handshake));
	}
#endif 
#ifdef SUPPORT_2G_HT40
	param.band = WLC_BAND_2G;
	param.bw_cap = WLC_BW_CAP_40MHZ;
	bcm_mkiovar("bw_cap", (char *)&param, sizeof(param), iovbuf, sizeof(iovbuf));
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0) {
		DHD_ERROR(("%s bw_cap set failed %d\n", __FUNCTION__, ret));
	}
#endif 
#ifdef SUPPORT_2G_VHT
	bcm_mkiovar("vht_features", (char *)&vht_features, 4, iovbuf, sizeof(iovbuf));
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0) {
		DHD_ERROR(("%s vht_features set failed %d\n", __FUNCTION__, ret));
	}
#endif 
#ifdef CUSTOM_PSPRETEND_THR
	
	bcm_mkiovar("pspretend_threshold", (char *)&pspretend_thr, 4,
		iovbuf, sizeof(iovbuf));
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf,
		sizeof(iovbuf), TRUE, 0)) < 0) {
		DHD_ERROR(("%s pspretend_threshold for HostAPD failed  %d\n",
			__FUNCTION__, ret));
	}
#endif

	bcm_mkiovar("buf_key_b4_m4", (char *)&buf_key_b4_m4, 4, iovbuf, sizeof(iovbuf));
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf,
		sizeof(iovbuf), TRUE, 0)) < 0) {
		DHD_ERROR(("%s buf_key_b4_m4 set failed %d\n", __FUNCTION__, ret));
	}

	
	bcm_mkiovar("event_msgs", eventmask, WL_EVENTING_MASK_LEN, iovbuf, sizeof(iovbuf));
	if ((ret  = dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, iovbuf, sizeof(iovbuf), FALSE, 0)) < 0) {
		DHD_ERROR(("%s read Event mask failed %d\n", __FUNCTION__, ret));
		goto done;
	}
	bcopy(iovbuf, eventmask, WL_EVENTING_MASK_LEN);

	
	setbit(eventmask, WLC_E_SET_SSID);
	setbit(eventmask, WLC_E_PRUNE);
	setbit(eventmask, WLC_E_AUTH);
	setbit(eventmask, WLC_E_AUTH_IND);
	setbit(eventmask, WLC_E_ASSOC);
	setbit(eventmask, WLC_E_REASSOC);
	setbit(eventmask, WLC_E_REASSOC_IND);
	setbit(eventmask, WLC_E_DEAUTH);
	setbit(eventmask, WLC_E_DEAUTH_IND);
	setbit(eventmask, WLC_E_DISASSOC_IND);
	setbit(eventmask, WLC_E_DISASSOC);
	setbit(eventmask, WLC_E_JOIN);
	setbit(eventmask, WLC_E_START);
	setbit(eventmask, WLC_E_ASSOC_IND);
	setbit(eventmask, WLC_E_PSK_SUP);
	setbit(eventmask, WLC_E_LINK);
	setbit(eventmask, WLC_E_NDIS_LINK);
	setbit(eventmask, WLC_E_MIC_ERROR);
#ifndef CUSTOMER_HW_ONE
	setbit(eventmask, WLC_E_ASSOC_REQ_IE);
	setbit(eventmask, WLC_E_ASSOC_RESP_IE);
#endif
#ifndef WL_CFG80211
	setbit(eventmask, WLC_E_PMKID_CACHE);
#ifndef CUSTOMER_HW_ONE
	setbit(eventmask, WLC_E_TXFAIL);
#endif 
#endif 
	setbit(eventmask, WLC_E_JOIN_START);
	setbit(eventmask, WLC_E_SCAN_COMPLETE);
#ifdef WLMEDIA_HTSF
	setbit(eventmask, WLC_E_HTSFSYNC);
#endif 
#ifdef PNO_SUPPORT
	setbit(eventmask, WLC_E_PFN_NET_FOUND);
	setbit(eventmask, WLC_E_PFN_BEST_BATCHING);
	setbit(eventmask, WLC_E_PFN_BSSID_NET_FOUND);
	setbit(eventmask, WLC_E_PFN_BSSID_NET_LOST);
#endif 
	
	setbit(eventmask, WLC_E_ROAM);
	setbit(eventmask, WLC_E_BSSID);
#ifdef BCMCCX
	setbit(eventmask, WLC_E_ADDTS_IND);
	setbit(eventmask, WLC_E_DELTS_IND);
#endif 
#ifdef WLTDLS
	setbit(eventmask, WLC_E_TDLS_PEER_EVENT);
#endif 
#ifdef WL_CFG80211
	setbit(eventmask, WLC_E_ESCAN_RESULT);
#ifdef CUSTOMER_HW_ONE
	if (!is_screen_off) {
#endif
	if (dhd->op_mode & DHD_FLAG_P2P_MODE) {
		setbit(eventmask, WLC_E_ACTION_FRAME_RX);
		setbit(eventmask, WLC_E_P2P_DISC_LISTEN_COMPLETE);
	}
#ifdef CUSTOMER_HW_ONE
	} else {
		DHD_ERROR(("screen is off, don't register some events\n"));
	}
#endif
#endif 
#ifdef WLAIBSS
	setbit(eventmask, WLC_E_AIBSS_TXFAIL);
#endif 
#ifdef CUSTOMER_HW10
	clrbit(eventmask, WLC_E_TRACE);
#else
	setbit(eventmask, WLC_E_TRACE);
#endif
	
	bcm_mkiovar("event_msgs", eventmask, WL_EVENTING_MASK_LEN, iovbuf, sizeof(iovbuf));
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0) {
		DHD_ERROR(("%s Set Event mask failed %d\n", __FUNCTION__, ret));
		goto done;
	}

	
	msglen = ROUNDUP(WLC_E_LAST, NBBY)/NBBY + EVENTMSGS_EXT_STRUCT_SIZE;
	eventmask_msg = (eventmsgs_ext_t*)kmalloc(msglen, GFP_KERNEL);
	if (eventmask_msg == NULL) {
		DHD_ERROR(("failed to allocate %d bytes for event_msg_ext\n", msglen));
		ret = BCME_NOMEM;
		goto done;
	}
	bzero(eventmask_msg, msglen);
	eventmask_msg->ver = EVENTMSGS_VER;
	eventmask_msg->len = ROUNDUP(WLC_E_LAST, NBBY)/NBBY;

	
	bcm_mkiovar("event_msgs_ext", (char *)eventmask_msg, msglen, iov_buf, WLC_IOCTL_SMLEN);
	ret2  = dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, iov_buf, WLC_IOCTL_SMLEN, FALSE, 0);
	if (ret2 != BCME_UNSUPPORTED)
		ret = ret2;
	if (ret2 == 0) { 
		bcopy(iov_buf, eventmask_msg, msglen);

#ifdef BT_WIFI_HANDOVER
		setbit(eventmask_msg->mask, WLC_E_BT_WIFI_HANDOVER_REQ);
#endif 
#ifdef CUSTOMER_HW_ONE
#ifdef WLC_E_RSSI_LOW
		setbit(eventmask_msg->mask, WLC_E_RSSI_LOW);
#endif 
		setbit(eventmask_msg->mask, WLC_E_LOAD_IND);
#if defined(HTC_TX_TRACKING)
		setbit(eventmask_msg->mask, WLC_E_TX_STAT_ERROR);
#endif
#endif 
		
		eventmask_msg->ver = EVENTMSGS_VER;
		eventmask_msg->command = EVENTMSGS_SET_MASK;
		eventmask_msg->len = ROUNDUP(WLC_E_LAST, NBBY)/NBBY;
		bcm_mkiovar("event_msgs_ext", (char *)eventmask_msg,
			msglen, iov_buf, WLC_IOCTL_SMLEN);
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR,
			iov_buf, WLC_IOCTL_SMLEN, TRUE, 0)) < 0) {
			DHD_ERROR(("%s write event mask ext failed %d\n", __FUNCTION__, ret));
			goto done;
		}
	} else if (ret2 < 0 && ret2 != BCME_UNSUPPORTED) {
		DHD_ERROR(("%s read event mask ext failed %d\n", __FUNCTION__, ret2));
		goto done;
	} 

	dhd_wl_ioctl_cmd(dhd, WLC_SET_SCAN_CHANNEL_TIME, (char *)&scan_assoc_time,
		sizeof(scan_assoc_time), TRUE, 0);
	dhd_wl_ioctl_cmd(dhd, WLC_SET_SCAN_UNASSOC_TIME, (char *)&scan_unassoc_time,
		sizeof(scan_unassoc_time), TRUE, 0);
	dhd_wl_ioctl_cmd(dhd, WLC_SET_SCAN_PASSIVE_TIME, (char *)&scan_passive_time,
		sizeof(scan_passive_time), TRUE, 0);

#ifdef ARP_OFFLOAD_SUPPORT
	
#if defined(SOFTAP)
	if (arpoe && !ap_fw_loaded) {
#else
	if (arpoe) {
#endif 
		dhd_arp_offload_enable(dhd, TRUE);
		dhd_arp_offload_set(dhd, dhd_arp_mode);
	} else {
		dhd_arp_offload_enable(dhd, FALSE);
		dhd_arp_offload_set(dhd, 0);
	}
	dhd_arp_enable = arpoe;
#endif 

#ifdef PKT_FILTER_SUPPORT
#ifdef CUSTOMER_HW_ONE
	mutex_init(&enable_pktfilter_mutex);
#endif
	
	dhd->pktfilter_count = 6;
#if defined(CUSTOMER_HW_ONE) && defined(PACKET_FILTER2)
	
	
	dhd->pktfilter[DHD_UNICAST_FILTER_NUM] = "100 0 0 0 0x01 0x00 1 0 30 0xFFFFFFFF 0xEFFFFFFA";
#else
	
	dhd->pktfilter[DHD_UNICAST_FILTER_NUM] = "100 0 0 0 0x01 0x00";
#endif
	dhd->pktfilter[DHD_BROADCAST_FILTER_NUM] = NULL;
	dhd->pktfilter[DHD_MULTICAST4_FILTER_NUM] = NULL;
	dhd->pktfilter[DHD_MULTICAST6_FILTER_NUM] = NULL;
	
	dhd->pktfilter[DHD_MDNS_FILTER_NUM] = "104 0 0 0 0xFFFFFFFFFFFF 0x01005E0000FB";
	
	dhd->pktfilter[DHD_ARP_FILTER_NUM] = "105 0 0 12 0xFFFF 0x0806";


#if defined(SOFTAP)
	if (ap_fw_loaded) {
		dhd_enable_packet_filter(0, dhd);
	}
#endif 
	dhd_set_packet_filter(dhd);
#endif 
#ifdef DISABLE_11N
	bcm_mkiovar("nmode", (char *)&nmode, 4, iovbuf, sizeof(iovbuf));
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0)
		DHD_ERROR(("%s wl nmode 0 failed %d\n", __FUNCTION__, ret));
#endif 
#ifdef DISABLE_LDPC
	bcm_mkiovar("ldpc_cap", (char *)&ldpc, 4, iovbuf, sizeof(iovbuf));
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0)
		DHD_ERROR(("%s wl ldpc_cap 0 failed %d\n", __FUNCTION__, ret));
#endif 

#ifdef AMPDU_VO_ENABLE
	tid.tid = PRIO_8021D_VO; 
	tid.enable = TRUE;
	bcm_mkiovar("ampdu_tid", (char *)&tid, sizeof(tid), iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);

	tid.tid = PRIO_8021D_NC; 
	tid.enable = TRUE;
	bcm_mkiovar("ampdu_tid", (char *)&tid, sizeof(tid), iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
#endif
#if defined(SOFTAP_TPUT_ENHANCE)
	if (dhd->op_mode & DHD_FLAG_HOSTAP_MODE) {
		dhd_bus_setidletime(dhd, (int)100);
#ifdef DHDTCPACK_SUPPRESS
		dhd->tcpack_sup_enabled = FALSE;
#endif
#if defined(DHD_TCP_WINSIZE_ADJUST)
		dhd_use_tcp_window_size_adjust = TRUE;
#endif

		memset(buf, 0, sizeof(buf));
		bcm_mkiovar("bus:txglom_auto_control", 0, 0, buf, sizeof(buf));
		if ((ret  = dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, buf, sizeof(buf), FALSE, 0)) < 0) {
			glom = 0;
			bcm_mkiovar("bus:txglom", (char *)&glom, 4, iovbuf, sizeof(iovbuf));
			dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
		}
		else {
			if (buf[0] == 0) {
				glom = 1;
				bcm_mkiovar("bus:txglom_auto_control", (char *)&glom, 4, iovbuf,
				sizeof(iovbuf));
				dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
			}
		}
	}
#endif 
#ifdef CUSTOMER_HW_ONE
	ret = dhd_wl_ioctl_cmd(dhd, WLC_UP, NULL, 0, TRUE, 0);
	if (ret < 0) {
		DHD_ERROR(("%s Setting WL UP failed %d\n", __FUNCTION__, ret));
		goto done;
	}
#endif
	
	memset(buf, 0, sizeof(buf));
	ptr = buf;
	bcm_mkiovar("ver", (char *)&buf, 4, buf, sizeof(buf));
	if ((ret  = dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, buf, sizeof(buf), FALSE, 0)) < 0)
		DHD_ERROR(("%s failed %d\n", __FUNCTION__, ret));
	else {
		bcmstrtok(&ptr, "\n", 0);
		
		DHD_ERROR(("Firmware version = %s\n", buf));
#if defined(BCMSDIO)
		dhd_set_version_info(dhd, buf);
#endif 
	}

#if defined(BCMSDIO)
	dhd_txglom_enable(dhd, TRUE);
#endif 

#if defined(BCMSDIO)
#ifdef PROP_TXSTATUS
	if (disable_proptx ||
#ifdef PROP_TXSTATUS_VSDB
		
		(dhd->op_mode != DHD_FLAG_HOSTAP_MODE &&
		 dhd->op_mode != DHD_FLAG_IBSS_MODE) ||
#endif 
		FALSE) {
		wlfc_enable = FALSE;
	}

#ifndef DISABLE_11N
	bcm_mkiovar("ampdu_hostreorder", (char *)&hostreorder, 4, iovbuf, sizeof(iovbuf));
	if ((ret2 = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0) {
		DHD_ERROR(("%s wl ampdu_hostreorder failed %d\n", __FUNCTION__, ret2));
		if (ret2 != BCME_UNSUPPORTED)
			ret = ret2;
		if (ret2 != BCME_OK)
			hostreorder = 0;
	}
#endif 

#ifdef READ_CONFIG_FROM_FILE
	dhd_preinit_config(dhd, 0);
#endif 

	if (wlfc_enable)
		dhd_wlfc_init(dhd);
#ifndef DISABLE_11N
	else if (hostreorder)
		dhd_wlfc_hostreorder_init(dhd);
#endif 

#endif 
#endif 
#ifdef PCIE_FULL_DONGLE
	
	if (FW_SUPPORTED(dhd, ap)) {
		wl_ap_isolate = AP_ISOLATE_SENDUP_ALL;
		bcm_mkiovar("ap_isolate", (char *)&wl_ap_isolate, 4, iovbuf, sizeof(iovbuf));
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0)) < 0)
			DHD_ERROR(("%s failed %d\n", __FUNCTION__, ret));
	}
#endif 
#ifdef PNO_SUPPORT
	if (!dhd->pno_state) {
		dhd_pno_init(dhd);
	}
#endif
#ifdef WL11U
	dhd_interworking_enable(dhd);
#endif 

done:

	if (eventmask_msg)
		kfree(eventmask_msg);
	if (iov_buf)
		kfree(iov_buf);

	return ret;
}


int
dhd_iovar(dhd_pub_t *pub, int ifidx, char *name, char *cmd_buf, uint cmd_len, int set)
{
	char buf[strlen(name) + 1 + cmd_len];
	int len = sizeof(buf);
	wl_ioctl_t ioc;
	int ret;

	len = bcm_mkiovar(name, cmd_buf, cmd_len, buf, len);

	memset(&ioc, 0, sizeof(ioc));

	ioc.cmd = set? WLC_SET_VAR : WLC_GET_VAR;
	ioc.buf = buf;
	ioc.len = len;
	ioc.set = set;

	ret = dhd_wl_ioctl(pub, ifidx, &ioc, ioc.buf, ioc.len);
	if (!set && ret >= 0)
		memcpy(cmd_buf, buf, cmd_len);

	return ret;
}

int dhd_change_mtu(dhd_pub_t *dhdp, int new_mtu, int ifidx)
{
	struct dhd_info *dhd = dhdp->info;
	struct net_device *dev = NULL;

	ASSERT(dhd && dhd->iflist[ifidx]);
	dev = dhd->iflist[ifidx]->net;
	ASSERT(dev);

	if (netif_running(dev)) {
		DHD_ERROR(("%s: Must be down to change its MTU", dev->name));
		return BCME_NOTDOWN;
	}

#define DHD_MIN_MTU 1500
#define DHD_MAX_MTU 1752

	if ((new_mtu < DHD_MIN_MTU) || (new_mtu > DHD_MAX_MTU)) {
		DHD_ERROR(("%s: MTU size %d is invalid.\n", __FUNCTION__, new_mtu));
		return BCME_BADARG;
	}

	dev->mtu = new_mtu;
	return 0;
}

#ifdef ARP_OFFLOAD_SUPPORT
void
aoe_update_host_ipv4_table(dhd_pub_t *dhd_pub, u32 ipa, bool add, int idx)
{
	u32 ipv4_buf[MAX_IPV4_ENTRIES]; 
	int i;
	int ret;

	bzero(ipv4_buf, sizeof(ipv4_buf));

	
	ret = dhd_arp_get_arp_hostip_table(dhd_pub, ipv4_buf, sizeof(ipv4_buf), idx);
	DHD_ARPOE(("%s: hostip table read from Dongle:\n", __FUNCTION__));
#ifdef AOE_DBG
	dhd_print_buf(ipv4_buf, 32, 4); 
#endif
	
	dhd_aoe_hostip_clr(dhd_pub, idx);

	if (ret) {
		DHD_ERROR(("%s failed\n", __FUNCTION__));
		return;
	}

	for (i = 0; i < MAX_IPV4_ENTRIES; i++) {
		if (add && (ipv4_buf[i] == 0)) {
				ipv4_buf[i] = ipa;
				add = FALSE; 
				DHD_ARPOE(("%s: Saved new IP in temp arp_hostip[%d]\n",
				__FUNCTION__, i));
		} else if (ipv4_buf[i] == ipa) {
			ipv4_buf[i]	= 0;
			DHD_ARPOE(("%s: removed IP:%x from temp table %d\n",
				__FUNCTION__, ipa, i));
		}

		if (ipv4_buf[i] != 0) {
			
			dhd_arp_offload_add_ip(dhd_pub, ipv4_buf[i], idx);
			DHD_ARPOE(("%s: added IP:%x to dongle arp_hostip[%d]\n\n",
				__FUNCTION__, ipv4_buf[i], i));
		}
	}
#ifdef AOE_DBG
	
	dhd_arp_get_arp_hostip_table(dhd_pub, ipv4_buf, sizeof(ipv4_buf), idx);
	DHD_ARPOE(("%s: read back arp_hostip table:\n", __FUNCTION__));
	dhd_print_buf(ipv4_buf, 32, 4); 
#endif
}

static int dhd_inetaddr_notifier_call(struct notifier_block *this,
	unsigned long event,
	void *ptr)
{
	struct in_ifaddr *ifa = (struct in_ifaddr *)ptr;

	dhd_info_t *dhd;
	dhd_pub_t *dhd_pub;
	int idx;

	if (!dhd_arp_enable)
		return NOTIFY_DONE;
	if (!ifa || !(ifa->ifa_dev->dev))
		return NOTIFY_DONE;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31))
	
	if ((ifa->ifa_dev->dev->netdev_ops != &dhd_ops_pri) &&
	    (ifa->ifa_dev->dev->netdev_ops != &dhd_ops_virt)) {
#if defined(WL_ENABLE_P2P_IF)
		if (!wl_cfgp2p_is_ifops(ifa->ifa_dev->dev->netdev_ops))
#endif 
			return NOTIFY_DONE;
	}
#endif 

	dhd = DHD_DEV_INFO(ifa->ifa_dev->dev);
	if (!dhd)
		return NOTIFY_DONE;

	dhd_pub = &dhd->pub;

	if (dhd_pub->arp_version == 1) {
		idx = 0;
	}
	else {
		for (idx = 0; idx < DHD_MAX_IFS; idx++) {
			if (dhd->iflist[idx] && dhd->iflist[idx]->net == ifa->ifa_dev->dev)
			break;
		}
		if (idx < DHD_MAX_IFS)
			DHD_TRACE(("ifidx : %p %s %d\n", dhd->iflist[idx]->net,
				dhd->iflist[idx]->name, dhd->iflist[idx]->idx));
		else {
			DHD_ERROR(("Cannot find ifidx for(%s) set to 0\n", ifa->ifa_label));
			idx = 0;
		}
	}

	switch (event) {
		case NETDEV_UP:
			DHD_ARPOE(("%s: [%s] Up IP: 0x%x\n",
				__FUNCTION__, ifa->ifa_label, ifa->ifa_address));

			if (dhd->pub.busstate != DHD_BUS_DATA) {
				DHD_ERROR(("%s: bus not ready, exit\n", __FUNCTION__));
				if (dhd->pend_ipaddr) {
					DHD_ERROR(("%s: overwrite pending ipaddr: 0x%x\n",
						__FUNCTION__, dhd->pend_ipaddr));
				}
				dhd->pend_ipaddr = ifa->ifa_address;
				break;
			}

#ifdef AOE_IP_ALIAS_SUPPORT
			DHD_ARPOE(("%s:add aliased IP to AOE hostip cache\n",
				__FUNCTION__));
			aoe_update_host_ipv4_table(dhd_pub, ifa->ifa_address, TRUE, idx);
#endif 
			break;

		case NETDEV_DOWN:
			DHD_ARPOE(("%s: [%s] Down IP: 0x%x\n",
				__FUNCTION__, ifa->ifa_label, ifa->ifa_address));
			dhd->pend_ipaddr = 0;
#ifdef AOE_IP_ALIAS_SUPPORT
			DHD_ARPOE(("%s:interface is down, AOE clr all for this if\n",
				__FUNCTION__));
			aoe_update_host_ipv4_table(dhd_pub, ifa->ifa_address, FALSE, idx);
#else
			dhd_aoe_hostip_clr(&dhd->pub, idx);
			dhd_aoe_arp_clr(&dhd->pub, idx);
#endif 
			break;

		default:
			DHD_ARPOE(("%s: do noting for [%s] Event: %lu\n",
				__func__, ifa->ifa_label, event));
			break;
	}
	return NOTIFY_DONE;
}
#endif 

#ifdef CONFIG_IPV6
static void
dhd_inet6_work_handler(void *dhd_info, void *event_data, u8 event)
{
	struct ipv6_work_info_t *ndo_work = (struct ipv6_work_info_t *)event_data;
	dhd_pub_t	*pub = &((dhd_info_t *)dhd_info)->pub;
	int		ret;

	if (event != DHD_WQ_WORK_IPV6_NDO) {
		DHD_ERROR(("%s: unexpected event \n", __FUNCTION__));
		return;
	}

	if (!ndo_work) {
		DHD_ERROR(("%s: ipv6 work info is not initialized \n", __FUNCTION__));
		return;
	}

	if (!pub) {
		DHD_ERROR(("%s: dhd pub is not initialized \n", __FUNCTION__));
		return;
	}

	if (ndo_work->if_idx) {
		DHD_ERROR(("%s: idx %d \n", __FUNCTION__, ndo_work->if_idx));
		return;
	}

	switch (ndo_work->event) {
		case NETDEV_UP:
			DHD_TRACE(("%s: Enable NDO and add ipv6 into table \n ", __FUNCTION__));
			ret = dhd_ndo_enable(pub, TRUE);
			if (ret < 0) {
				DHD_ERROR(("%s: Enabling NDO Failed %d\n", __FUNCTION__, ret));
			}

			ret = dhd_ndo_add_ip(pub, &ndo_work->ipv6_addr[0], ndo_work->if_idx);
			if (ret < 0) {
				DHD_ERROR(("%s: Adding host ip for NDO failed %d\n",
					__FUNCTION__, ret));
			}
			break;
		case NETDEV_DOWN:
			DHD_TRACE(("%s: clear ipv6 table \n", __FUNCTION__));
			ret = dhd_ndo_remove_ip(pub, ndo_work->if_idx);
			if (ret < 0) {
				DHD_ERROR(("%s: Removing host ip for NDO failed %d\n",
					__FUNCTION__, ret));
				goto done;
			}

			ret = dhd_ndo_enable(pub, FALSE);
			if (ret < 0) {
				DHD_ERROR(("%s: disabling NDO Failed %d\n", __FUNCTION__, ret));
				goto done;
			}
			break;
		default:
			DHD_ERROR(("%s: unknown notifier event \n", __FUNCTION__));
			break;
	}
done:
	
	kfree(ndo_work);

	return;
}

static int dhd_inet6addr_notifier_call(struct notifier_block *this,
	unsigned long event,
	void *ptr)
{
	dhd_info_t *dhd;
	dhd_pub_t *dhd_pub;
	struct inet6_ifaddr *inet6_ifa = ptr;
	struct in6_addr *ipv6_addr = &inet6_ifa->addr;
	struct ipv6_work_info_t *ndo_info;
	int idx = 0; 

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31))
	
	if (inet6_ifa->idev->dev->netdev_ops != &dhd_ops_pri) {
			return NOTIFY_DONE;
	}
#endif 

	dhd = DHD_DEV_INFO(inet6_ifa->idev->dev);
	if (!dhd)
		return NOTIFY_DONE;

	if (dhd->iflist[idx] && dhd->iflist[idx]->net != inet6_ifa->idev->dev)
		return NOTIFY_DONE;
	dhd_pub = &dhd->pub;
	if (!FW_SUPPORTED(dhd_pub, ndoe))
		return NOTIFY_DONE;

	ndo_info = (struct ipv6_work_info_t *)kzalloc(sizeof(struct ipv6_work_info_t), GFP_ATOMIC);
	if (!ndo_info) {
		DHD_ERROR(("%s: ipv6 work alloc failed\n", __FUNCTION__));
		return NOTIFY_DONE;
	}

	ndo_info->event = event;
	ndo_info->if_idx = idx;
	memcpy(&ndo_info->ipv6_addr[0], ipv6_addr, IPV6_ADDR_LEN);

	
	dhd_deferred_schedule_work(dhd->dhd_deferred_wq, (void *)ndo_info, DHD_WQ_WORK_IPV6_NDO,
		dhd_inet6_work_handler, DHD_WORK_PRIORITY_LOW);
	return NOTIFY_DONE;
}
#endif 

int
dhd_register_if(dhd_pub_t *dhdp, int ifidx, bool need_rtnl_lock)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;
	dhd_if_t *ifp;
	struct net_device *net = NULL;
	int err = 0;
	uint8 temp_addr[ETHER_ADDR_LEN] = { 0x00, 0x90, 0x4c, 0x11, 0x22, 0x33 };

	DHD_TRACE(("%s: ifidx %d\n", __FUNCTION__, ifidx));

	ASSERT(dhd && dhd->iflist[ifidx]);
	ifp = dhd->iflist[ifidx];
	net = ifp->net;
	ASSERT(net && (ifp->idx == ifidx));

#ifndef  P2PONEINT
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31))
	ASSERT(!net->open);
	net->get_stats = dhd_get_stats;
	net->do_ioctl = dhd_ioctl_entry;
	net->hard_start_xmit = dhd_start_xmit;
	net->set_mac_address = dhd_set_mac_address;
	net->set_multicast_list = dhd_set_multicast_list;
	net->open = net->stop = NULL;
#else
	ASSERT(!net->netdev_ops);
	net->netdev_ops = &dhd_ops_virt;
#endif 
#else
	net->netdev_ops = &dhd_cfgp2p_ops_virt;
#endif 

	
	if (ifidx == 0) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31))
		net->open = dhd_open;
		net->stop = dhd_stop;
#else
		net->netdev_ops = &dhd_ops_pri;
#endif 
		if (!ETHER_ISNULLADDR(dhd->pub.mac.octet))
			memcpy(temp_addr, dhd->pub.mac.octet, ETHER_ADDR_LEN);
	} else {
		memcpy(temp_addr, ifp->mac_addr, ETHER_ADDR_LEN);
		if (!memcmp(temp_addr, dhd->iflist[0]->mac_addr,
			ETHER_ADDR_LEN)) {
			DHD_ERROR(("%s interface [%s]: set locally administered bit in MAC\n",
			__func__, net->name));
			temp_addr[0] |= 0x02;
		}
	}

	net->hard_header_len = ETH_HLEN + dhd->pub.hdrlen;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
	net->ethtool_ops = &dhd_ethtool_ops;
#endif 

#if defined(WL_WIRELESS_EXT)
#if WIRELESS_EXT < 19
	net->get_wireless_stats = dhd_get_wireless_stats;
#endif 
#if WIRELESS_EXT > 12
	net->wireless_handlers = (struct iw_handler_def *)&wl_iw_handler_def;
#endif 
#endif 

	dhd->pub.rxsz = DBUS_RX_BUFFER_SIZE_DHD(net);

	memcpy(net->dev_addr, temp_addr, ETHER_ADDR_LEN);

	if (ifidx == 0)
		DHD_ERROR(("%s\n", dhd_version));

	if (need_rtnl_lock)
		err = register_netdev(net);
	else
		err = register_netdevice(net);

	if (err != 0) {
		DHD_ERROR(("couldn't register the net device [%s], err %d\n", net->name, err));
		goto fail;
	}

#ifdef SET_RPS_CPUS
	err = custom_rps_map_set(net->_rx, RPS_CPUS_MASK, strlen(RPS_CPUS_MASK));
	if (err < 0)
		DHD_ERROR(("%s : custom_rps_map_set done. error : %d\n", __FUNCTION__, err));
#endif 

	
	
	
	
	
	DHD_ERROR(("Register interface [%s]  MAC: %02x:%02x:%02x:%02x:%02x:%02x \n\n", net->name,
		~net->dev_addr[0]&0xff, ~net->dev_addr[1]&0xff, ~net->dev_addr[2]&0xff,
		~net->dev_addr[3]&0xff, ~net->dev_addr[4]&0xff, ~net->dev_addr[5]&0xff));
	

#if defined(SOFTAP) && defined(WL_WIRELESS_EXT) && !defined(WL_CFG80211)
		wl_iw_iscan_set_scan_broadcast_prep(net, 1);
#endif

#if 1 && (defined(BCMPCIE) || (defined(BCMLXSDMMC) && (LINUX_VERSION_CODE >= \
	KERNEL_VERSION(2, 6, 27))))
	if (ifidx == 0) {
#if defined(BCMLXSDMMC) || defined(BCMPCIE)
		up(&dhd_registration_sem);
#endif
		if (!dhd_download_fw_on_driverload) {
			dhd_net_bus_devreset(net, TRUE);
#ifdef BCMLXSDMMC
			dhd_net_bus_suspend(net);
#endif 
			wifi_platform_set_power(dhdp->info->adapter, FALSE, WIFI_TURNOFF_DELAY);
		}
	}
#endif 
	return 0;

fail:
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31)
	net->open = NULL;
#else
	net->netdev_ops = NULL;
#endif
	return err;
}

void
dhd_bus_detach(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (dhdp) {
		dhd = (dhd_info_t *)dhdp->info;
		if (dhd) {

			if (dhd->pub.busstate != DHD_BUS_DOWN) {
				
				dhd_prot_stop(&dhd->pub);

				
				dhd_bus_stop(dhd->pub.bus, TRUE);
			}

#if defined(OOB_INTR_ONLY) || defined(BCMPCIE_OOB_HOST_WAKE)
			dhd_bus_oob_intr_unregister(dhdp);
#endif 
		}
	}
}


void dhd_detach(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd;
	unsigned long flags;
	int timer_valid = FALSE;

	if (!dhdp)
		return;

	dhd = (dhd_info_t *)dhdp->info;
	if (!dhd)
		return;

#if defined(CUSTOMER_HW20) && defined(WLANAUDIO)
	dhd_global = NULL;
#endif 

	DHD_TRACE(("%s: Enter state 0x%x\n", __FUNCTION__, dhd->dhd_state));

	dhd->pub.up = 0;
	if (!(dhd->dhd_state & DHD_ATTACH_STATE_DONE)) {
		OSL_SLEEP(100);
	}

	if (dhd->dhd_state & DHD_ATTACH_STATE_PROT_ATTACH) {
		dhd_bus_detach(dhdp);
#ifdef PCIE_FULL_DONGLE
		dhd_flow_rings_deinit(dhdp);
#endif

		if (dhdp->prot)
			dhd_prot_detach(dhdp);
	}

#ifdef ARP_OFFLOAD_SUPPORT
	if (dhd_inetaddr_notifier_registered) {
		dhd_inetaddr_notifier_registered = FALSE;
		unregister_inetaddr_notifier(&dhd_inetaddr_notifier);
	}
#endif 
#ifdef CONFIG_IPV6
	if (dhd_inet6addr_notifier_registered) {
		dhd_inet6addr_notifier_registered = FALSE;
		unregister_inet6addr_notifier(&dhd_inet6addr_notifier);
	}
#endif
#ifdef CUSTOMER_HW_ONE
	dhd->dhd_force_exit = TRUE;
#endif

#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(DHD_USE_EARLYSUSPEND)
	if (dhd->dhd_state & DHD_ATTACH_STATE_EARLYSUSPEND_DONE) {
		if (dhd->early_suspend.suspend)
			unregister_early_suspend(&dhd->early_suspend);
	}
#endif 

#if defined(WL_WIRELESS_EXT)
	if (dhd->dhd_state & DHD_ATTACH_STATE_WL_ATTACH) {
		
		wl_iw_detach();
	}
#endif 

	
	if (dhd->dhd_state & DHD_ATTACH_STATE_ADD_IF) {
		int i = 1;
		dhd_if_t *ifp;

		
		dhd_net_if_lock_local(dhd);
		for (i = 1; i < DHD_MAX_IFS; i++) {
			if (dhd->iflist[i])
				dhd_remove_if(&dhd->pub, i, TRUE);
		}
		dhd_net_if_unlock_local(dhd);

		
		ifp = dhd->iflist[0];
		ASSERT(ifp);
		ASSERT(ifp->net);
		if (ifp && ifp->net) {



			if (ifp->net->reg_state == NETREG_UNINITIALIZED)
				free_netdev(ifp->net);
			else {
#ifdef SET_RPS_CPUS
				custom_rps_map_clear(ifp->net->_rx);
#endif 
				unregister_netdev(ifp->net);
			}
			ifp->net = NULL;
#ifdef DHD_WMF
			dhd_wmf_cleanup(dhdp, 0);
#endif 

			dhd_if_del_sta_list(ifp);

			MFREE(dhd->pub.osh, ifp, sizeof(*ifp));
			dhd->iflist[0] = NULL;
		}
	}

	
	DHD_GENERAL_LOCK(&dhd->pub, flags);
	timer_valid = dhd->wd_timer_valid;
	dhd->wd_timer_valid = FALSE;
	DHD_GENERAL_UNLOCK(&dhd->pub, flags);
	if (timer_valid)
		del_timer_sync(&dhd->timer);

	if (dhd->dhd_state & DHD_ATTACH_STATE_THREADS_CREATED) {
		if (dhd->thr_wdt_ctl.thr_pid >= 0) {
			PROC_STOP(&dhd->thr_wdt_ctl);
		}

		if (dhd->rxthread_enabled && dhd->thr_rxf_ctl.thr_pid >= 0) {
			PROC_STOP(&dhd->thr_rxf_ctl);
		}

		if (dhd->thr_dpc_ctl.thr_pid >= 0) {
			PROC_STOP(&dhd->thr_dpc_ctl);
		} else
			tasklet_kill(&dhd->tasklet);
	}
#ifdef WL_CFG80211
	if (dhd->dhd_state & DHD_ATTACH_STATE_CFG80211) {
		wl_cfg80211_detach(NULL);
		dhd_monitor_uninit();
	}
#endif
	
	dhd_deferred_work_deinit(dhd->dhd_deferred_wq);
	dhd->dhd_deferred_wq = NULL;

#ifdef SHOW_LOGTRACE
	if (dhd->event_data.fmts)
		kfree(dhd->event_data.fmts);
	if (dhd->event_data.raw_fmts)
		kfree(dhd->event_data.raw_fmts);
#endif 

#ifdef PNO_SUPPORT
	if (dhdp->pno_state)
		dhd_pno_deinit(dhdp);
#endif
#if defined(CONFIG_PM_SLEEP)
	if (dhd_pm_notifier_registered) {
		unregister_pm_notifier(&dhd_pm_notifier);
		dhd_pm_notifier_registered = FALSE;
	}
#endif 
#ifdef DEBUG_CPU_FREQ
		if (dhd->new_freq)
			free_percpu(dhd->new_freq);
		dhd->new_freq = NULL;
		cpufreq_unregister_notifier(&dhd->freq_trans, CPUFREQ_TRANSITION_NOTIFIER);
#endif
	if (dhd->dhd_state & DHD_ATTACH_STATE_WAKELOCKS_INIT) {
		DHD_TRACE(("wd wakelock count:%d\n", dhd->wakelock_wd_counter));
#ifdef CONFIG_HAS_WAKELOCK
		dhd->wakelock_counter = 0;
		dhd->wakelock_wd_counter = 0;
		dhd->wakelock_rx_timeout_enable = 0;
		dhd->wakelock_ctrl_timeout_enable = 0;
		wake_lock_destroy(&dhd->wl_wifi);
		wake_lock_destroy(&dhd->wl_rxwake);
		wake_lock_destroy(&dhd->wl_ctrlwake);
		wake_lock_destroy(&dhd->wl_wdwake);
#ifdef BCMPCIE_OOB_HOST_WAKE
		wake_lock_destroy(&dhd->wl_intrwake);
#endif 
#ifdef CUSTOMER_HW_ONE
		wake_lock_destroy(&dhd->wl_htc);
#endif
#endif 
	}




#ifdef CUSTOMER_HW_ONE
	dhd->dhd_force_exit = FALSE;
#endif
#ifdef DHDTCPACK_SUPPRESS
	
	dhd_tcpack_suppress_set(&dhd->pub, TCPACK_SUP_OFF);
#endif 
}


void
dhd_free(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd;
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (dhdp) {
		int i;
		for (i = 0; i < ARRAYSIZE(dhdp->reorder_bufs); i++) {
			if (dhdp->reorder_bufs[i]) {
				reorder_info_t *ptr;
				uint32 buf_size = sizeof(struct reorder_info);

				ptr = dhdp->reorder_bufs[i];

				buf_size += ((ptr->max_idx + 1) * sizeof(void*));
				DHD_REORDER(("free flow id buf %d, maxidx is %d, buf_size %d\n",
					i, ptr->max_idx, buf_size));

				MFREE(dhdp->osh, dhdp->reorder_bufs[i], buf_size);
				dhdp->reorder_bufs[i] = NULL;
			}
		}

		dhd_sta_pool_fini(dhdp, DHD_MAX_STA);

		dhd = (dhd_info_t *)dhdp->info;
#ifdef USE_STATIC_MEMDUMP
		if (dhdp->soc_ram) {
			DHD_OS_PREFREE(dhdp, dhdp->soc_ram, dhdp->soc_ram_length);
			dhdp->soc_ram = NULL;
		}
#endif 

		
		if (dhd &&
			dhd != (dhd_info_t *)dhd_os_prealloc(dhdp, DHD_PREALLOC_DHD_INFO, 0, FALSE))
			MFREE(dhd->pub.osh, dhd, sizeof(*dhd));
		dhd = NULL;
#if defined(USE_STATIC_MEMDUMP)
		g_dhdp = NULL;
#endif 
	}
}

void
dhd_clear(dhd_pub_t *dhdp)
{
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (dhdp) {
		int i;
		dhd_if_t *ifp;
		for (i = 0; i < ARRAYSIZE(dhdp->reorder_bufs); i++) {
			if (dhdp->reorder_bufs[i]) {
				reorder_info_t *ptr;
				uint32 buf_size = sizeof(struct reorder_info);

				ptr = dhdp->reorder_bufs[i];

				buf_size += ((ptr->max_idx + 1) * sizeof(void*));
				DHD_REORDER(("free flow id buf %d, maxidx is %d, buf_size %d\n",
					i, ptr->max_idx, buf_size));

				MFREE(dhdp->osh, dhdp->reorder_bufs[i], buf_size);
				dhdp->reorder_bufs[i] = NULL;
			}
		}

		ifp = dhdp->info->iflist[0];
		if (ifp && ifp->net) {
			DHD_ERROR(("%s: Delete sta list before clear\n", __FUNCTION__));
			dhd_if_del_sta_list(ifp);
		}

		dhd_sta_pool_clear(dhdp, DHD_MAX_STA);

#ifdef USE_STATIC_MEMDUMP
		if (dhdp->soc_ram) {
			DHD_OS_PREFREE(dhdp, dhdp->soc_ram, dhdp->soc_ram_length);
			dhdp->soc_ram = NULL;
		}
#endif 
	}
}

static void
dhd_module_cleanup(void)
{
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

#ifdef CUSTOMER_HW_ONE
	if (priv_dhdp) {
		dhd_net_if_lock_local(priv_dhdp->info);
		priv_dhdp->os_stopped = 1;
		dhd_net_if_unlock_local(priv_dhdp->info);
	}
	msleep(1000);
#endif
	dhd_bus_unregister();

	wl_android_exit();

	dhd_wifi_platform_unregister_drv();
#ifdef CUSTOMER_HW_ONE
	DHD_ERROR(("[ATS][press_widget][turn_off]\n")); 
#endif
}

static void __exit
dhd_module_exit(void)
{
	dhd_module_cleanup();
	unregister_reboot_notifier(&dhd_reboot_notifier);
}

#ifdef MODULE_INIT_ASYNC
static void __init
dhd_module_init_async(void *unused, async_cookie_t cookie);

static int __init
dhd_module_init(void)
{
	async_schedule(dhd_module_init_async, NULL);
	return 0;
}

static void __init
dhd_module_init_async(void *unused, async_cookie_t cookie)
{
#else
static int __init
dhd_module_init(void)
{
#endif 
	int err;
	int retry = POWERUP_MAX_RETRY;

	DHD_ERROR(("%s in\n", __FUNCTION__));

	DHD_PERIM_RADIO_INIT();

	if (firmware_path[0] != '\0') {
		strncpy(fw_bak_path, firmware_path, MOD_PARAM_PATHLEN);
		fw_bak_path[MOD_PARAM_PATHLEN-1] = '\0';
	}

	if (nvram_path[0] != '\0') {
		strncpy(nv_bak_path, nvram_path, MOD_PARAM_PATHLEN);
		nv_bak_path[MOD_PARAM_PATHLEN-1] = '\0';
	}

	do {
		err = dhd_wifi_platform_register_drv();
		if (!err) {
			register_reboot_notifier(&dhd_reboot_notifier);
			break;
		}
		else {
			DHD_ERROR(("%s: Failed to load the driver, try cnt %d\n",
				__FUNCTION__, retry));
			strncpy(firmware_path, fw_bak_path, MOD_PARAM_PATHLEN);
			firmware_path[MOD_PARAM_PATHLEN-1] = '\0';
			strncpy(nvram_path, nv_bak_path, MOD_PARAM_PATHLEN);
			nvram_path[MOD_PARAM_PATHLEN-1] = '\0';
		}
	} while (retry--);

	if (err)
		DHD_ERROR(("%s: Failed to load driver max retry reached**\n", __FUNCTION__));

	DHD_ERROR(("%s out, err=%d\n", __FUNCTION__, err));
#ifndef MODULE_INIT_ASYNC
	return err;
#endif
}

static int
dhd_reboot_callback(struct notifier_block *this, unsigned long code, void *unused)
{
	DHD_TRACE(("%s: code = %ld\n", __FUNCTION__, code));
	if (code == SYS_RESTART) {
	}

	return NOTIFY_DONE;
}


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
#if defined(CONFIG_DEFERRED_INITCALLS)
deferred_module_init(dhd_module_init);
#elif defined(USE_LATE_INITCALL_SYNC)
late_initcall_sync(dhd_module_init);
#else
late_initcall(dhd_module_init);
#endif 
#else
module_init(dhd_module_init);
#endif 

module_exit(dhd_module_exit);

int
dhd_os_proto_block(dhd_pub_t *pub)
{
	dhd_info_t * dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		DHD_PERIM_UNLOCK(pub);

		down(&dhd->proto_sem);

		DHD_PERIM_LOCK(pub);
		return 1;
	}

	return 0;
}

int
dhd_os_proto_unblock(dhd_pub_t *pub)
{
	dhd_info_t * dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
		up(&dhd->proto_sem);
		return 1;
	}

	return 0;
}

unsigned int
dhd_os_get_ioctl_resp_timeout(void)
{
	return ((unsigned int)dhd_ioctl_timeout_msec);
}

void
dhd_os_set_ioctl_resp_timeout(unsigned int timeout_msec)
{
	dhd_ioctl_timeout_msec = (int)timeout_msec;
}

int
dhd_os_ioctl_resp_wait(dhd_pub_t *pub, uint *condition, bool *pending)
{
	dhd_info_t * dhd = (dhd_info_t *)(pub->info);
	int timeout;

	
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	timeout = msecs_to_jiffies(dhd_ioctl_timeout_msec);
#else
	timeout = dhd_ioctl_timeout_msec * HZ / 1000;
#endif

	DHD_PERIM_UNLOCK(pub);

	timeout = wait_event_timeout(dhd->ioctl_resp_wait, (*condition), timeout);

	DHD_PERIM_LOCK(pub);

	return timeout;
}

int
dhd_os_ioctl_resp_wake(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	wake_up(&dhd->ioctl_resp_wait);
	return 0;
}

int
dhd_os_d3ack_wait(dhd_pub_t *pub, uint *condition, bool *pending)
{
	dhd_info_t * dhd = (dhd_info_t *)(pub->info);
	int timeout;

	
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	timeout = msecs_to_jiffies(dhd_ioctl_timeout_msec);
#else
	timeout = dhd_ioctl_timeout_msec * HZ / 1000;
#endif

	DHD_PERIM_UNLOCK(pub);

	timeout = wait_event_timeout(dhd->d3ack_wait, (*condition), timeout);

	DHD_PERIM_LOCK(pub);

	return timeout;
}

int
dhd_os_d3ack_wake(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	wake_up(&dhd->d3ack_wait);
	return 0;
}

void
dhd_os_wd_timer_extend(void *bus, bool extend)
{
	dhd_pub_t *pub = bus;
	dhd_info_t *dhd = (dhd_info_t *)pub->info;

	if (extend)
		dhd_os_wd_timer(bus, WATCHDOG_EXTEND_INTERVAL);
	else
		dhd_os_wd_timer(bus, dhd->default_wd_interval);
}


void
dhd_os_wd_timer(void *bus, uint wdtick)
{
	dhd_pub_t *pub = bus;
	dhd_info_t *dhd = (dhd_info_t *)pub->info;
	unsigned long flags;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (!dhd) {
		DHD_ERROR(("%s: dhd NULL\n", __FUNCTION__));
		return;
	}

	DHD_GENERAL_LOCK(pub, flags);

	
	if (pub->busstate == DHD_BUS_DOWN) {
		DHD_GENERAL_UNLOCK(pub, flags);
		if (!wdtick)
			DHD_OS_WD_WAKE_UNLOCK(pub);
		return;
	}

	
	if (!wdtick && dhd->wd_timer_valid == TRUE) {
		dhd->wd_timer_valid = FALSE;
		DHD_GENERAL_UNLOCK(pub, flags);
		del_timer_sync(&dhd->timer);
		DHD_OS_WD_WAKE_UNLOCK(pub);
		return;
	}

	if (wdtick) {
		DHD_OS_WD_WAKE_LOCK(pub);
		dhd_watchdog_ms = (uint)wdtick;
		
		mod_timer(&dhd->timer, jiffies + msecs_to_jiffies(dhd_watchdog_ms));
		dhd->wd_timer_valid = TRUE;
	}
	DHD_GENERAL_UNLOCK(pub, flags);
}

void *
dhd_os_open_image(char *filename)
{
	struct file *fp;

	fp = filp_open(filename, O_RDONLY, 0);
	 if (IS_ERR(fp))
		 fp = NULL;

	 return fp;
}

int
dhd_os_get_image_block(char *buf, int len, void *image)
{
	struct file *fp = (struct file *)image;
	int rdlen;

	if (!image)
		return 0;

	rdlen = kernel_read(fp, fp->f_pos, buf, len);
	if (rdlen > 0)
		fp->f_pos += rdlen;

	return rdlen;
}

void
dhd_os_close_image(void *image)
{
	if (image)
		filp_close((struct file *)image, NULL);
}

void
dhd_os_sdlock(dhd_pub_t *pub)
{
	dhd_info_t *dhd;

	dhd = (dhd_info_t *)(pub->info);

	if (dhd_dpc_prio >= 0)
		down(&dhd->sdsem);
	else
		spin_lock_bh(&dhd->sdlock);
}

void
dhd_os_sdunlock(dhd_pub_t *pub)
{
	dhd_info_t *dhd;

	dhd = (dhd_info_t *)(pub->info);

	if (dhd_dpc_prio >= 0)
		up(&dhd->sdsem);
	else
		spin_unlock_bh(&dhd->sdlock);
}

void
dhd_os_sdlock_txq(dhd_pub_t *pub)
{
	dhd_info_t *dhd;

	dhd = (dhd_info_t *)(pub->info);
	spin_lock_bh(&dhd->txqlock);
}

void
dhd_os_sdunlock_txq(dhd_pub_t *pub)
{
	dhd_info_t *dhd;

	dhd = (dhd_info_t *)(pub->info);
	spin_unlock_bh(&dhd->txqlock);
}

void
dhd_os_sdlock_rxq(dhd_pub_t *pub)
{
}

void
dhd_os_sdunlock_rxq(dhd_pub_t *pub)
{
}

static void
dhd_os_rxflock(dhd_pub_t *pub)
{
	dhd_info_t *dhd;

	dhd = (dhd_info_t *)(pub->info);
	spin_lock_bh(&dhd->rxf_lock);

}

static void
dhd_os_rxfunlock(dhd_pub_t *pub)
{
	dhd_info_t *dhd;

	dhd = (dhd_info_t *)(pub->info);
	spin_unlock_bh(&dhd->rxf_lock);
}

#ifdef DHDTCPACK_SUPPRESS
unsigned long
dhd_os_tcpacklock(dhd_pub_t *pub)
{
	dhd_info_t *dhd;
	unsigned long flags = 0;

	dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
#ifdef BCMSDIO
		spin_lock_bh(&dhd->tcpack_lock);
#else
		spin_lock_irqsave(&dhd->tcpack_lock, flags);
#endif 
	}

	return flags;
}

void
dhd_os_tcpackunlock(dhd_pub_t *pub, unsigned long flags)
{
	dhd_info_t *dhd;

#ifdef BCMSDIO
	BCM_REFERENCE(flags);
#endif 

	dhd = (dhd_info_t *)(pub->info);

	if (dhd) {
#ifdef BCMSDIO
		spin_lock_bh(&dhd->tcpack_lock);
#else
		spin_unlock_irqrestore(&dhd->tcpack_lock, flags);
#endif 
	}
}
#endif 

uint8* dhd_os_prealloc(dhd_pub_t *dhdpub, int section, uint size, bool kmalloc_if_fail)
{
	uint8* buf;
	gfp_t flags = CAN_SLEEP() ? GFP_KERNEL: GFP_ATOMIC;

	buf = (uint8*)wifi_platform_prealloc(dhdpub->info->adapter, section, size);
	if (buf == NULL) {
		DHD_ERROR(("%s: failed to alloc memory, section: %d,"
			" size: %dbytes", __FUNCTION__, section, size));
		if (kmalloc_if_fail)
			buf = kmalloc(size, flags);
	}

	return buf;
}

void dhd_os_prefree(dhd_pub_t *dhdpub, void *addr, uint size)
{
}

#if defined(WL_WIRELESS_EXT)
struct iw_statistics *
dhd_get_wireless_stats(struct net_device *dev)
{
	int res = 0;
	dhd_info_t *dhd = DHD_DEV_INFO(dev);

	if (!dhd->pub.up) {
		return NULL;
	}

	res = wl_iw_get_wireless_stats(dev, &dhd->iw.wstats);

	if (res == 0)
		return &dhd->iw.wstats;
	else
		return NULL;
}
#endif 

#if defined(CUSTOMER_HW20) && defined(WLANAUDIO)
static int
dhd_wlanaudio_event(dhd_info_t *dhd, int *ifidx, void *pktdata,
                    wl_event_msg_t *event, void **data)
{
	int cnt;
	char eabuf[ETHER_ADDR_STR_LEN];
	struct ether_addr *addr = &event->addr;
	uint32 type = ntoh32_ua((void *)&event->event_type);

	switch (type) {
	case WLC_E_TXFAIL:
		if (addr != NULL)
			bcm_ether_ntoa(addr, eabuf);
		else
			return (BCME_ERROR);

		for (cnt = 0; cnt < MAX_WLANAUDIO_BLACKLIST; cnt++) {
			if (dhd->wlanaudio_blist[cnt].is_blacklist)
				break;

			if (!bcmp(&dhd->wlanaudio_blist[cnt].blacklist_addr,
			          addr, ETHER_ADDR_LEN)) {
				
				dhd->wlanaudio_blist[cnt].cnt++;

				if (dhd->wlanaudio_blist[cnt].cnt < 15) {
					
					if ((dhd->wlanaudio_blist[cnt].cnt > 10) &&
					    (jiffies - dhd->wlanaudio_blist[cnt].txfail_jiffies
					     < 100)) {
						dhd->wlanaudio_blist[cnt].is_blacklist = true;
						dhd->is_wlanaudio_blist = true;
					}
				} else {
					if ((!dhd->wlanaudio_blist[cnt].is_blacklist) &&
					   (jiffies - dhd->wlanaudio_blist[cnt].txfail_jiffies
					    > 100)) {

						bzero(&dhd->wlanaudio_blist[cnt],
						      sizeof(struct wlanaudio_blacklist));
					}
				}
				break;
			} else if ((!dhd->wlanaudio_blist[cnt].is_blacklist) &&
			           (!dhd->wlanaudio_blist[cnt].cnt)) {
				bcopy(addr,
				      (char*)&dhd->wlanaudio_blist[cnt].blacklist_addr,
				      ETHER_ADDR_LEN);
				dhd->wlanaudio_blist[cnt].cnt++;
				dhd->wlanaudio_blist[cnt].txfail_jiffies = jiffies;

				bcm_ether_ntoa(&dhd->wlanaudio_blist[cnt].blacklist_addr, eabuf);
				break;
			}
		}
		break;
	case WLC_E_AUTH	 :
	case WLC_E_AUTH_IND :
	case WLC_E_DEAUTH :
	case WLC_E_DEAUTH_IND :
	case WLC_E_ASSOC:
	case WLC_E_ASSOC_IND:
	case WLC_E_REASSOC:
	case WLC_E_REASSOC_IND:
	case WLC_E_DISASSOC:
	case WLC_E_DISASSOC_IND:
		{
			int bl_cnt = 0;

			if (addr != NULL)
				bcm_ether_ntoa(addr, eabuf);
			else
				return (BCME_ERROR);

			for (cnt = 0; cnt < MAX_WLANAUDIO_BLACKLIST; cnt++) {
				if (!bcmp(&dhd->wlanaudio_blist[cnt].blacklist_addr,
				          addr, ETHER_ADDR_LEN)) {
					
					if (dhd->wlanaudio_blist[cnt].is_blacklist) {
						
						bzero(&dhd->wlanaudio_blist[cnt],
						      sizeof(struct wlanaudio_blacklist));
					}
				}
			}

			for (cnt = 0; cnt < MAX_WLANAUDIO_BLACKLIST; cnt++) {
				if (dhd->wlanaudio_blist[cnt].is_blacklist)
					bl_cnt++;
			}

			if (!bl_cnt)
			{
				dhd->is_wlanaudio_blist = false;
			}

			break;
		}
	}
	return BCME_OK;
}
#endif 
static int
dhd_wl_host_event(dhd_info_t *dhd, int *ifidx, void *pktdata,
	wl_event_msg_t *event, void **data)
{
	int bcmerror = 0;

	ASSERT(dhd != NULL);

#if defined(CUSTOMER_HW20) && defined(WLANAUDIO)
	bcmerror = dhd_wlanaudio_event(dhd, ifidx, pktdata, event, data);

	if (bcmerror != BCME_OK)
		return (bcmerror);
#endif 

#ifdef SHOW_LOGTRACE
	bcmerror = wl_host_event(&dhd->pub, ifidx, pktdata, event, data, &dhd->event_data);
#else
	bcmerror = wl_host_event(&dhd->pub, ifidx, pktdata, event, data, NULL);
#endif 

	if (bcmerror != BCME_OK)
		return (bcmerror);

#if defined(WL_WIRELESS_EXT)
	if (event->bsscfgidx == 0) {

	ASSERT(dhd->iflist[*ifidx] != NULL);
	ASSERT(dhd->iflist[*ifidx]->net != NULL);

		if (dhd->iflist[*ifidx]->net) {
		wl_iw_event(dhd->iflist[*ifidx]->net, event, *data);
		}
	}
#endif 

#ifdef WL_CFG80211
	ASSERT(dhd->iflist[*ifidx] != NULL);
	ASSERT(dhd->iflist[*ifidx]->net != NULL);
	if (dhd->iflist[*ifidx]->net)
		wl_cfg80211_event(dhd->iflist[*ifidx]->net, event, *data);
#endif 

	return (bcmerror);
}

void
dhd_sendup_event(dhd_pub_t *dhdp, wl_event_msg_t *event, void *data)
{
	switch (ntoh32(event->event_type)) {
#ifdef WLBTAMP
	
	case WLC_E_BTA_HCI_EVENT: {
		struct sk_buff *p, *skb;
		bcm_event_t *msg;
		wl_event_msg_t *p_bcm_event;
		char *ptr;
		uint32 len;
		uint32 pktlen;
		dhd_if_t *ifp;
		dhd_info_t *dhd;
		uchar *eth;
		int ifidx;

		len = ntoh32(event->datalen);
		pktlen = sizeof(bcm_event_t) + len + 2;
		dhd = dhdp->info;
		ifidx = dhd_ifname2idx(dhd, event->ifname);

		if ((p = PKTGET(dhdp->osh, pktlen, FALSE))) {
			ASSERT(ISALIGNED((uintptr)PKTDATA(dhdp->osh, p), sizeof(uint32)));

			msg = (bcm_event_t *) PKTDATA(dhdp->osh, p);

			bcopy(&dhdp->mac, &msg->eth.ether_dhost, ETHER_ADDR_LEN);
			bcopy(&dhdp->mac, &msg->eth.ether_shost, ETHER_ADDR_LEN);
			ETHER_TOGGLE_LOCALADDR(&msg->eth.ether_shost);

			msg->eth.ether_type = hton16(ETHER_TYPE_BRCM);

			
			msg->bcm_hdr.subtype = hton16(BCMILCP_SUBTYPE_VENDOR_LONG);
			msg->bcm_hdr.version = BCMILCP_BCM_SUBTYPEHDR_VERSION;
			bcopy(BRCM_OUI, &msg->bcm_hdr.oui[0], DOT11_OUI_LEN);

			msg->bcm_hdr.length = hton16(BCMILCP_BCM_SUBTYPEHDR_MINLENGTH +
				BCM_MSG_LEN + sizeof(wl_event_msg_t) + (uint16)len);
			msg->bcm_hdr.usr_subtype = hton16(BCMILCP_BCM_SUBTYPE_EVENT);

			PKTSETLEN(dhdp->osh, p, (sizeof(bcm_event_t) + len + 2));

			

			
			p_bcm_event = &msg->event;
			bcopy(event, p_bcm_event, sizeof(wl_event_msg_t));

			
			bcopy(data, (p_bcm_event + 1), len);

			msg->bcm_hdr.length  = hton16(sizeof(wl_event_msg_t) +
				ntoh16(msg->bcm_hdr.length));
			PKTSETLEN(dhdp->osh, p, (sizeof(bcm_event_t) + len + 2));

			ptr = (char *)(msg + 1);
			ptr[len+0] = 0x00;
			ptr[len+1] = 0x00;

			skb = PKTTONATIVE(dhdp->osh, p);
			eth = skb->data;
			len = skb->len;

			ifp = dhd->iflist[ifidx];
			if (ifp == NULL)
			     ifp = dhd->iflist[0];

			ASSERT(ifp);
			skb->dev = ifp->net;
			skb->protocol = eth_type_trans(skb, skb->dev);

			skb->data = eth;
			skb->len = len;

			
			skb_pull(skb, ETH_HLEN);

			
			if (in_interrupt()) {
				netif_rx(skb);
			} else {
				netif_rx_ni(skb);
			}
		}
		else {
			
			DHD_ERROR(("%s: unable to alloc sk_buf", __FUNCTION__));
		}
		break;
	} 
#endif 

	default:
		break;
	}
}

#ifdef LOG_INTO_TCPDUMP
void
dhd_sendup_log(dhd_pub_t *dhdp, void *data, int data_len)
{
	struct sk_buff *p, *skb;
	uint32 pktlen;
	int len;
	dhd_if_t *ifp;
	dhd_info_t *dhd;
	uchar *skb_data;
	int ifidx = 0;
	struct ether_header eth;

	pktlen = sizeof(eth) + data_len;
	dhd = dhdp->info;

	if ((p = PKTGET(dhdp->osh, pktlen, FALSE))) {
		ASSERT(ISALIGNED((uintptr)PKTDATA(dhdp->osh, p), sizeof(uint32)));

		bcopy(&dhdp->mac, &eth.ether_dhost, ETHER_ADDR_LEN);
		bcopy(&dhdp->mac, &eth.ether_shost, ETHER_ADDR_LEN);
		ETHER_TOGGLE_LOCALADDR(&eth.ether_shost);
		eth.ether_type = hton16(ETHER_TYPE_BRCM);

		bcopy((void *)&eth, PKTDATA(dhdp->osh, p), sizeof(eth));
		bcopy(data, PKTDATA(dhdp->osh, p) + sizeof(eth), data_len);
		skb = PKTTONATIVE(dhdp->osh, p);
		skb_data = skb->data;
		len = skb->len;

		ifidx = dhd_ifname2idx(dhd, "wlan0");
		ifp = dhd->iflist[ifidx];
		if (ifp == NULL)
			 ifp = dhd->iflist[0];

		ASSERT(ifp);
		skb->dev = ifp->net;
		skb->protocol = eth_type_trans(skb, skb->dev);
		skb->data = skb_data;
		skb->len = len;

		
		skb_pull(skb, ETH_HLEN);

		
		if (in_interrupt()) {
			netif_rx(skb);
		} else {
			netif_rx_ni(skb);
		}
	}
	else {
		
		DHD_ERROR(("%s: unable to alloc sk_buf", __FUNCTION__));
	}
}
#endif 

void dhd_wait_for_event(dhd_pub_t *dhd, bool *lockvar)
{
#if defined(BCMSDIO) && (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
	struct dhd_info *dhdinfo =  dhd->info;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	int timeout = msecs_to_jiffies(IOCTL_RESP_TIMEOUT);
#else
	int timeout = (IOCTL_RESP_TIMEOUT / 1000) * HZ;
#endif 

	dhd_os_sdunlock(dhd);
	wait_event_timeout(dhdinfo->ctrl_wait, (*lockvar == FALSE), timeout);
	dhd_os_sdlock(dhd);
#endif 
	return;
}

void dhd_wait_event_wakeup(dhd_pub_t *dhd)
{
#if defined(BCMSDIO) && (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
	struct dhd_info *dhdinfo =  dhd->info;
	if (waitqueue_active(&dhdinfo->ctrl_wait))
		wake_up(&dhdinfo->ctrl_wait);
#endif
	return;
}

#if defined(BCMSDIO) || defined(BCMPCIE)
int
dhd_net_bus_devreset(struct net_device *dev, uint8 flag)
{
	int ret = 0;
	dhd_info_t *dhd = DHD_DEV_INFO(dev);

	if (flag == TRUE) {
		
		if (dhd_wl_ioctl_cmd(&dhd->pub, WLC_DOWN, NULL, 0, TRUE, 0) < 0) {
			DHD_TRACE(("%s: wl down failed\n", __FUNCTION__));
		}
#ifdef PROP_TXSTATUS
		if (dhd->pub.wlfc_enabled)
			dhd_wlfc_deinit(&dhd->pub);
#endif 
#ifdef PNO_SUPPORT
	if (dhd->pub.pno_state)
		dhd_pno_deinit(&dhd->pub);
#endif
	}

#ifdef BCMSDIO
	if (!flag) {
		dhd_update_fw_nv_path(dhd);
		
		dhd_bus_update_fw_nv_path(dhd->pub.bus,
			dhd->fw_path, dhd->nv_path);
	}
#endif 

	ret = dhd_bus_devreset(&dhd->pub, flag);
	if (ret) {
		DHD_ERROR(("%s: dhd_bus_devreset: %d\n", __FUNCTION__, ret));
		return ret;
	}

	return ret;
}

#ifdef BCMSDIO
int
dhd_net_bus_suspend(struct net_device *dev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	return dhd_bus_suspend(&dhd->pub);
}

int
dhd_net_bus_resume(struct net_device *dev, uint8 stage)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	return dhd_bus_resume(&dhd->pub, stage);
}

#endif 
#endif 

int net_os_set_suspend_disable(struct net_device *dev, int val)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	int ret = 0;

	if (dhd) {
		ret = dhd->pub.suspend_disable_flag;
		dhd->pub.suspend_disable_flag = val;
	}
	return ret;
}

int net_os_set_suspend(struct net_device *dev, int val, int force)
{
	int ret = 0;
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
#ifdef CUSTOMER_HW_ONE
	dhd_pub_t *dhdp = &dhd->pub;
#endif

	if (dhd) {
#ifdef CUSTOMER_HW_ONE
		dhdp->in_suspend = val;
#endif
#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(DHD_USE_EARLYSUSPEND)
		ret = dhd_set_suspend(val, &dhd->pub);
#else
		ret = dhd_suspend_resume_helper(dhd, val, force);
#endif
#ifdef WL_CFG80211
		wl_cfg80211_update_power_mode(dev);
#endif
	}
	
#ifdef CUSTOMER_HW_ONE
	if (val == 1) {
		wl_android_traffic_monitor(dev);
	}
#endif
	
	return ret;
}

int net_os_set_suspend_bcn_li_dtim(struct net_device *dev, int val)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);

	if (dhd)
		dhd->pub.suspend_bcn_li_dtim = val;

	return 0;
}

#ifdef PKT_FILTER_SUPPORT
int net_os_rxfilter_add_remove(struct net_device *dev, int add_remove, int num)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	char *filterp = NULL;
	int filter_id = 0;
	int ret = 0;

	if (!dhd || (num == DHD_UNICAST_FILTER_NUM) ||
		(num == DHD_MDNS_FILTER_NUM))
		return ret;
	if (num >= dhd->pub.pktfilter_count)
		return -EINVAL;
	switch (num) {
		case DHD_BROADCAST_FILTER_NUM:
			filterp = "101 0 0 0 0xFFFFFFFFFFFF 0xFFFFFFFFFFFF";
			filter_id = 101;
			break;
		case DHD_MULTICAST4_FILTER_NUM:
			filterp = "102 0 0 0 0xFFFFFF 0x01005E";
			filter_id = 102;
			break;
		case DHD_MULTICAST6_FILTER_NUM:
			filterp = "103 0 0 0 0xFFFF 0x3333";
			filter_id = 103;
			break;
		default:
			return -EINVAL;
	}

	
	if (add_remove) {
		dhd->pub.pktfilter[num] = filterp;
		dhd_pktfilter_offload_set(&dhd->pub, dhd->pub.pktfilter[num]);
	} else { 
		if (dhd->pub.pktfilter[num] != NULL) {
			dhd_pktfilter_offload_delete(&dhd->pub, filter_id);
			dhd->pub.pktfilter[num] = NULL;
		}
	}
	return ret;
}

int dhd_os_enable_packet_filter(dhd_pub_t *dhdp, int val)

{
	int ret = 0;

	if (dhdp && dhdp->up) {
		if (dhdp->in_suspend) {
			if (!val || (val && !dhdp->suspend_disable_flag))
				dhd_enable_packet_filter(val, dhdp);
		}
	}
	return ret;
}

int net_os_enable_packet_filter(struct net_device *dev, int val)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);

	return dhd_os_enable_packet_filter(&dhd->pub, val);
}
#endif 

int
dhd_dev_init_ioctl(struct net_device *dev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	int ret;

	if ((ret = dhd_sync_with_dongle(&dhd->pub)) < 0)
		goto done;

done:
	return ret;
}

#ifdef PNO_SUPPORT
int
dhd_dev_pno_stop_for_ssid(struct net_device *dev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);

	return (dhd_pno_stop_for_ssid(&dhd->pub));
}
int
dhd_dev_pno_set_for_ssid(struct net_device *dev, wlc_ssid_t* ssids_local, int nssid,
	uint16  scan_fr, int pno_repeat, int pno_freq_expo_max, uint16 *channel_list, int nchan)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);

	return (dhd_pno_set_for_ssid(&dhd->pub, ssids_local, nssid, scan_fr,
		pno_repeat, pno_freq_expo_max, channel_list, nchan));
}

int
dhd_dev_pno_enable(struct net_device *dev, int enable)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);

	return (dhd_pno_enable(&dhd->pub, enable));
}

int
dhd_dev_pno_set_for_hotlist(struct net_device *dev, wl_pfn_bssid_t *p_pfn_bssid,
	struct dhd_pno_hotlist_params *hotlist_params)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	return (dhd_pno_set_for_hotlist(&dhd->pub, p_pfn_bssid, hotlist_params));
}
int
dhd_dev_pno_stop_for_batch(struct net_device *dev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	return (dhd_pno_stop_for_batch(&dhd->pub));
}
int
dhd_dev_pno_set_for_batch(struct net_device *dev, struct dhd_pno_batch_params *batch_params)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	return (dhd_pno_set_for_batch(&dhd->pub, batch_params));
}
int
dhd_dev_pno_get_for_batch(struct net_device *dev, char *buf, int bufsize)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	return (dhd_pno_get_for_batch(&dhd->pub, buf, bufsize, PNO_STATUS_NORMAL));
}
#endif 

#ifdef PCIE_FULL_DONGLE
void dhdpcie_pub_intr_disable(dhd_pub_t *dhd);
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && (1)
static void dhd_hang_process(void *dhd_info, void *event_info, u8 event)
{
	dhd_info_t *dhd;
	struct net_device *dev;
#if defined(PCIE_FULL_DONGLE)
	dhd_pub_t *dhdp = NULL;
#endif
	dhd = (dhd_info_t *)dhd_info;
	dev = dhd->iflist[0]->net;

	if (dev) {
#ifdef CUSTOMER_HW_ONE
		DHD_ERROR(("%s call netif_stop_queue to stop traffic\n", __FUNCTION__));
		netif_stop_queue(dev);
#if defined(PCIE_FULL_DONGLE)
		dhdp = &dhd->pub;
		if (dhdp) {
			if (!dhdp->bus->islinkdown) {
				DHD_ERROR(("%s: suspend PCIE bus\n", __FUNCTION__));
				dhd_bus_suspend(dhdp);
			} else {
				DHD_ERROR(("%s: disable PCIE interrupts\n", __FUNCTION__));
				dhdpcie_pub_intr_disable(dhdp);
			}
		}
        if (otp_write)
		    DHD_ERROR(("%s before send hang, do bus down to prevent"
			    " additional event from firmware\n", __FUNCTION__));
        else
            DHD_ERROR(("%s beforce hang OTP is empty \n", __FUNCTION__));
		dhd->pub.busstate = DHD_BUS_DOWN;
#endif 
#else
		rtnl_lock();
		dev_close(dev);
		rtnl_unlock();
#endif 
#if defined(WL_WIRELESS_EXT)
		wl_iw_send_priv_event(dev, "HANG");
#endif
#ifdef CUSTOMER_HW_ONE
		wl_cfg80211_send_priv_event(dev, "HANG");
#endif 
#if defined(WL_CFG80211)
		wl_cfg80211_hang(dev, WLAN_REASON_UNSPECIFIED);
#endif
	}
}


int dhd_os_send_hang_message(dhd_pub_t *dhdp)
{
	int ret = 0;
	if (dhdp) {
		if (!dhdp->hang_was_sent) {
			dhdp->hang_was_sent = 1;
			dhd_deferred_schedule_work(dhdp->info->dhd_deferred_wq, (void *)dhdp,
				DHD_WQ_WORK_HANG_MSG, dhd_hang_process, DHD_WORK_PRIORITY_HIGH);
		}
	}
	return ret;
}

int net_os_send_hang_message(struct net_device *dev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	int ret = 0;

	if (dhd) {
#ifdef CUSTOMER_HW_ONE
		if (dhd->pub.os_stopped) {
			DHD_ERROR(("%s: module removed. Do not send hang event.\n", __FUNCTION__));
			return ret;
		}
		dhd->pub.txdesc_no_res = 0;
#endif
		
		if (dhd->pub.hang_report) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
			ret = dhd_os_send_hang_message(&dhd->pub);
#else
			ret = wl_cfg80211_hang(dev, WLAN_REASON_UNSPECIFIED);
#endif
		} else {
			DHD_ERROR(("%s: FW HANG ignored (for testing purpose) and not sent up\n",
				__FUNCTION__));
			
			dhd->pub.busstate = DHD_BUS_DOWN;
		}
	}
	return ret;
}
#endif 


int dhd_net_wifi_platform_set_power(struct net_device *dev, bool on, unsigned long delay_msec)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	return wifi_platform_set_power(dhd->adapter, on, delay_msec);
}

void dhd_get_customized_country_code(struct net_device *dev, char *country_iso_code,
	wl_country_t *cspec)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	get_customized_country_code(dhd->adapter, country_iso_code, cspec);
}
void dhd_bus_country_set(struct net_device *dev, wl_country_t *cspec, bool notify)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	if (dhd && dhd->pub.up) {
		memcpy(&dhd->pub.dhd_cspec, cspec, sizeof(wl_country_t));
#ifdef WL_CFG80211
		wl_update_wiphybands(NULL, notify);
#endif
	}
}

void dhd_bus_band_set(struct net_device *dev, uint band)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	if (dhd && dhd->pub.up) {
#ifdef WL_CFG80211
		wl_update_wiphybands(NULL, true);
#endif
	}
}

int dhd_net_set_fw_path(struct net_device *dev, char *fw)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);

	if (!fw || fw[0] == '\0')
		return -EINVAL;

	strncpy(dhd->fw_path, fw, sizeof(dhd->fw_path) - 1);
	dhd->fw_path[sizeof(dhd->fw_path)-1] = '\0';

#if defined(SOFTAP)
	if (strstr(fw, "apsta") != NULL) {
		DHD_INFO(("GOT APSTA FIRMWARE\n"));
		ap_fw_loaded = TRUE;
	} else {
		DHD_INFO(("GOT STA FIRMWARE\n"));
		ap_fw_loaded = FALSE;
	}
#endif 
	return 0;
}

void dhd_net_if_lock(struct net_device *dev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	dhd_net_if_lock_local(dhd);
}

void dhd_net_if_unlock(struct net_device *dev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	dhd_net_if_unlock_local(dhd);
}

static void dhd_net_if_lock_local(dhd_info_t *dhd)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)) && 1
	if (dhd)
		mutex_lock(&dhd->dhd_net_if_mutex);
#endif
}

static void dhd_net_if_unlock_local(dhd_info_t *dhd)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)) && 1
	if (dhd)
		mutex_unlock(&dhd->dhd_net_if_mutex);
#endif
}

#ifndef CUSTOMER_HW_ONE
static void dhd_suspend_lock(dhd_pub_t *pub)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)) && 1
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	if (dhd)
		mutex_lock(&dhd->dhd_suspend_mutex);
#endif
}

static void dhd_suspend_unlock(dhd_pub_t *pub)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)) && 1
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	if (dhd)
		mutex_unlock(&dhd->dhd_suspend_mutex);
#endif
}
#endif 

unsigned long dhd_os_general_spin_lock(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags = 0;

	if (dhd)
		spin_lock_irqsave(&dhd->dhd_lock, flags);

	return flags;
}

void dhd_os_general_spin_unlock(dhd_pub_t *pub, unsigned long flags)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

	if (dhd)
		spin_unlock_irqrestore(&dhd->dhd_lock, flags);
}

void *
dhd_os_spin_lock_init(osl_t *osh)
{
	
	
	
	spinlock_t * lock = MALLOC(osh, sizeof(spinlock_t) + 4);
	if (lock)
		spin_lock_init(lock);
	return ((void *)lock);
}
void
dhd_os_spin_lock_deinit(osl_t *osh, void *lock)
{
	MFREE(osh, lock, sizeof(spinlock_t) + 4);
}
unsigned long
dhd_os_spin_lock(void *lock)
{
	unsigned long flags = 0;

	if (lock)
		spin_lock_irqsave((spinlock_t *)lock, flags);

	return flags;
}
void
dhd_os_spin_unlock(void *lock, unsigned long flags)
{
	if (lock)
		spin_unlock_irqrestore((spinlock_t *)lock, flags);
}

static int
dhd_get_pend_8021x_cnt(dhd_info_t *dhd)
{
	return (atomic_read(&dhd->pend_8021x_cnt));
}

#define MAX_WAIT_FOR_8021X_TX	100

int
dhd_wait_pend8021x(struct net_device *dev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	int timeout = msecs_to_jiffies(10);
	int ntimes = MAX_WAIT_FOR_8021X_TX;
	int pend = dhd_get_pend_8021x_cnt(dhd);

	while (ntimes && pend) {
		if (pend) {
			set_current_state(TASK_INTERRUPTIBLE);
			DHD_PERIM_UNLOCK(&dhd->pub);
			schedule_timeout(timeout);
			DHD_PERIM_LOCK(&dhd->pub);
			set_current_state(TASK_RUNNING);
			ntimes--;
		}
		pend = dhd_get_pend_8021x_cnt(dhd);
	}
	if (ntimes == 0)
	{
		atomic_set(&dhd->pend_8021x_cnt, 0);
		DHD_ERROR(("%s: TIMEOUT\n", __FUNCTION__));
	}
	return pend;
}

#ifdef DHD_DEBUG
#define RAMDUMP_PATH "/data/ramdump/"
#ifdef CUSTOMER_HW_ONE
#define USER_RAMDUMP_PATH "/data/htclog/brcm_mem_dump_bg/"
#define FW_RAMDUMP_PATH "/data/htclog/brcm_mem_dump_fw/"
#else
#define USER_RAMDUMP_PATH "/data/ramdump/user_trigger/"
#define FW_RAMDUMP_PATH "/data/ramdump/brcm_mem_dump/"
#endif
void _mkdir(const char *dir)
{
	char tmp[512];
	char *p = NULL;
	size_t len;

	snprintf(tmp, sizeof(tmp), "%s", dir);
	len = strlen(tmp);
	if (tmp[len - 1] == '/')
		tmp[len - 1] = 0;

	for (p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = 0;
			if (sys_mkdir(tmp, S_IRWXU | S_IRWXG | S_IRWXO) == -1) {
				if (sys_chmod(tmp, S_IRWXU | S_IRWXG | S_IRWXO) == -1) {
					DHD_ERROR(("%s: fail to chmod for %s.\n", __func__, tmp));
				}
			}
			*p = '/';
		}
	}

	if (sys_mkdir(tmp, S_IRWXU | S_IRWXG | S_IRWXO) == -1) {
		if (sys_chmod(tmp, S_IRWXU | S_IRWXG | S_IRWXO) == -1) {
			DHD_ERROR(("%s: fail to chmod for %s.\n", __func__, tmp));
		}
	}
}

#ifdef CUSTOMER_HW_ONE
atomic_t  ramdump_in_progress = ATOMIC_INIT(0);
#define MAX_BUFF_SIZE 1024
#define RECORD_SLOT "#record_slot"
#define TIME_SLOT "#time_slot"
#define TIME_SLOT1 "#time_slot1"
#define TIME_SLOT2 "#time_slot2"
#define TIME_SLOT3 "#time_slot3"
#define TIME_SLOT4 "#time_slot4"
#define TIME_SLOT5 "#time_slot5"
#define PDATE  "yyyy-mm-dd-hh-mm-ss-uuu."
int parse_timetag(char *pbuf, int *slot, char *pdate, int fwtrigger)
{
	struct file *fp;
	loff_t pos = 0;
	int len = 0;
	int ret = 0;


#ifdef CUSTOMER_HW_ONE
	
	if (fwtrigger)
		fp = filp_open(FW_RAMDUMP_PATH"fwtimetag.txt", O_RDWR|O_CREAT, 0666);
	else
		fp = filp_open(USER_RAMDUMP_PATH"timetag.txt", O_RDWR|O_CREAT, 0666);
	
#else
	if (fwtrigger)
		fp = filp_open("/data/ramdump/brcm_mem_dump/timetag.txt", O_RDWR|O_CREAT, 0666);
	else
		fp = filp_open("/data/ramdump/user_trigger/timetag.txt", O_RDWR|O_CREAT, 0666);
#endif
	if (IS_ERR(fp)) {
		fp = NULL;
		ret = -1;
		return ret;
	}
	len = dhd_os_get_image_block(pbuf, MAX_BUFF_SIZE, fp);
	DHD_ERROR(("%s: here get image len = %d \n ", __FUNCTION__, len));
	if (len == 0) {
		DHD_ERROR(("1.enter %s here len = %d first time trigger!!!!\n ",
			__FUNCTION__, len));
		*slot = *slot + 1;
		DHD_ERROR(("%s : *slot = %d !\n ", __FUNCTION__, *slot));
		snprintf(pbuf, MAX_BUFF_SIZE, "%s=%d \n%s=%s \n%s=%s \n%s=%s \n%s=%s \n%s=%s \n",
			RECORD_SLOT, *slot, TIME_SLOT1, pdate,
			TIME_SLOT2, PDATE, TIME_SLOT3, PDATE,
			TIME_SLOT4, PDATE, TIME_SLOT5, PDATE);
	} else {
		int count = 0, index = 0;
		char *ppos[6];
		char buf[32];
		int i =  0;

		DHD_ERROR(("%s: File exist parse start!!\n", __FUNCTION__));
		ppos[0] = bcmstrstr(pbuf, RECORD_SLOT);
		index = ppos[0] - pbuf + sizeof(RECORD_SLOT);
		count = bcm_atoi(pbuf + index);
		*slot = (count + 1) % 6;
		if (*slot == 0)
			*slot = 1;
		for (i = 1; i < 6; i++) {
			memset(buf, 0x0, 32);
			snprintf(buf, 32, "time_slot%d", i);
			ppos[i] = strstr(pbuf, buf);
		}
		if (ppos[0] && ppos[*slot]) {
			DHD_ERROR(("%s update time and slot information \n", __FUNCTION__));
			snprintf(buf, 32, "%d", *slot);
			strncpy(ppos[0] + sizeof(RECORD_SLOT), buf, 1);
			snprintf(buf, 32, "%s", TIME_SLOT);
			strncpy(ppos[*slot] + sizeof(TIME_SLOT), pdate, strlen(pdate));
		} else {
			DHD_ERROR(("%s : pos null not found string!!\n ", __FUNCTION__));
			ret = -1;
		}
	}
	
	fp->f_op->write(fp, pbuf, MAX_BUFF_SIZE, &pos);
#ifdef USE_STATIC_MEMDUMP
	fp->f_op->fsync(fp, 0, MAX_BUFF_SIZE-1, 1);
#endif 
	if (fp)
		filp_close(fp, current->files);
	return ret;
}

int
user_triger_write_to_file(dhd_pub_t *dhd, uint8 *buf, int size)
{
	int ret = 0;
	int record_slot = 0;
	char filename[64];
	struct file *fp_dumpfile;
	mm_segment_t old_fs;
	loff_t pos = 0;
	struct timespec ts;
	struct rtc_time tm;
	char *memblock = NULL;
	char recordtime[64];
	int fwtrigger = 0;
	getnstimeofday(&ts);
	ts.tv_sec -= sys_tz.tz_minuteswest * 60;
	rtc_time_to_tm(ts.tv_sec, &tm);

	DHD_ERROR(("%s: dump brcm_mem_dump at %02d-%02d-%02d-%02d:%02d:%02d.%03lu.\n",
		__func__, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
		tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000));

	snprintf(recordtime, sizeof(recordtime), "%02d-%02d-%02d-%02d:%02d:%02d.%03lu.",
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
		tm.tm_sec, ts.tv_nsec / 1000000);

	memblock = kzalloc(MAX_BUFF_SIZE, GFP_KERNEL);
	if (memblock == NULL) {
		DHD_ERROR(("%s: Failed to allocate memory %d bytes\n",
			__FUNCTION__, MAX_BUFF_SIZE));
		ret = -1;
		goto exit;
	}
	
	old_fs = get_fs();
	set_fs(KERNEL_DS);

	
	_mkdir(USER_RAMDUMP_PATH);
	if (!(ret = parse_timetag(memblock, &record_slot, recordtime, fwtrigger))) {
#ifdef CUSTOMER_HW_ONE
		
		snprintf(filename, sizeof(filename),
			USER_RAMDUMP_PATH"dumpfile%d", record_slot);
		
#else
		snprintf(filename, sizeof(filename),
			"/data/ramdump/user_trigger/dumpfile%d", record_slot);
#endif
		DHD_ERROR(("%s : dump save filename[%s]\n ", __FUNCTION__, filename));
	} else {
		DHD_ERROR(("%s parse_timetag error return\n ", __FUNCTION__));
	}
	fp_dumpfile = filp_open(filename, O_WRONLY|O_CREAT, 0666);
	if (!fp_dumpfile) {
		DHD_ERROR(("%s: open file error\n", __FUNCTION__));
		ret = -1;
		goto exit;
	}
	
	fp_dumpfile->f_op->write(fp_dumpfile, buf, size, &pos);
#ifdef USE_STATIC_MEMDUMP
	fp_dumpfile->f_op->fsync(fp_dumpfile, 0, size-1, 1);
#endif 

exit:
	if (memblock) {
		kfree(memblock);
		memblock = NULL;
	}
	
#ifdef USE_STATIC_MEMDUMP
	DHD_OS_PREFREE(dhd, buf, size);
#else
	MFREE(dhd->osh, buf, size);
#endif 
	
	if (fp_dumpfile)
		filp_close(fp_dumpfile, current->files);
	
	set_fs(old_fs);
	return ret;
}
#endif 

int
write_to_file(dhd_pub_t *dhd, uint8 *buf, int size)
{
	int ret = 0;
	struct file *fp;
	mm_segment_t old_fs;
	loff_t pos = 0;
#ifdef CUSTOMER_HW_ONE
	struct timespec ts;
	struct rtc_time tm;
	char *memblock = NULL;
	char recordtime[64];
	int fwtrigger = 1;
	int record_slot = 0;
	char filename[64];

	getnstimeofday(&ts);
	ts.tv_sec -= sys_tz.tz_minuteswest * 60;
	rtc_time_to_tm(ts.tv_sec, &tm);
	DHD_ERROR(("%s: dump brcm_mem_dump at %02d-%02d-%02d-%02d:%02d:%02d.%03lu.\n",
		__FUNCTION__, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
		tm.tm_sec, ts.tv_nsec / 1000000));

	snprintf(recordtime, sizeof(recordtime), "%02d-%02d-%02d-%02d:%02d:%02d.%03lu.",
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
		tm.tm_sec, ts.tv_nsec / 1000000);

	memblock = kzalloc(MAX_BUFF_SIZE, GFP_KERNEL);
	if (memblock == NULL) {
		DHD_ERROR(("%s: Failed to allocate memory %d bytes\n",
		__FUNCTION__, MAX_BUFF_SIZE));
		goto exit;
	}
#endif 

	
	old_fs = get_fs();
	set_fs(KERNEL_DS);

	
#ifdef CUSTOMER_HW_ONE
	_mkdir(FW_RAMDUMP_PATH);
	if (!(ret = parse_timetag(memblock, &record_slot, recordtime, fwtrigger))) {
		snprintf(filename, sizeof(filename),
			FW_RAMDUMP_PATH"fwdumpfile%d", record_slot);
	} else {
		DHD_ERROR(("%s parse_timetag error return\n ", __FUNCTION__));
		goto exit;
	}
	fp = filp_open(filename, O_WRONLY|O_CREAT, 0666);
#else
	fp = filp_open("/tmp/mem_dump", O_WRONLY|O_CREAT, 0666);
#endif 
	if (!fp) {
		DHD_ERROR(("%s: open file error\n", __FUNCTION__));
		ret = -1;
		goto exit;
	}

	
	fp->f_op->write(fp, buf, size, &pos);
#ifdef USE_STATIC_MEMDUMP
	fp->f_op->fsync(fp, 0, size-1, 1);
#endif 

exit:
#ifdef CUSTOMER_HW_ONE
	
	if (memblock) {
		kfree(memblock);
		memblock = NULL;
	}
#endif
	
#ifdef USE_STATIC_MEMDUMP
	DHD_OS_PREFREE(dhd, buf, size);
#else
	MFREE(dhd->osh, buf, size);
#endif 
	
	if (fp)
		filp_close(fp, current->files);
	
	set_fs(old_fs);

	return ret;
}
#endif 

int dhd_os_wake_lock_timeout(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;
	int ret = 0;

	if (dhd) {
		spin_lock_irqsave(&dhd->wakelock_spinlock, flags);
		ret = dhd->wakelock_rx_timeout_enable > dhd->wakelock_ctrl_timeout_enable ?
			dhd->wakelock_rx_timeout_enable : dhd->wakelock_ctrl_timeout_enable;
#ifdef CONFIG_HAS_WAKELOCK
		if (dhd->wakelock_rx_timeout_enable)
			wake_lock_timeout(&dhd->wl_rxwake,
				msecs_to_jiffies(dhd->wakelock_rx_timeout_enable));
		if (dhd->wakelock_ctrl_timeout_enable)
			wake_lock_timeout(&dhd->wl_ctrlwake,
				msecs_to_jiffies(dhd->wakelock_ctrl_timeout_enable));
#endif
		dhd->wakelock_rx_timeout_enable = 0;
		dhd->wakelock_ctrl_timeout_enable = 0;
		spin_unlock_irqrestore(&dhd->wakelock_spinlock, flags);
	}
	return ret;
}

int net_os_wake_lock_timeout(struct net_device *dev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	int ret = 0;

	if (dhd)
		ret = dhd_os_wake_lock_timeout(&dhd->pub);
	return ret;
}

int dhd_os_wake_lock_rx_timeout_enable(dhd_pub_t *pub, int val)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;

	if (dhd) {
		spin_lock_irqsave(&dhd->wakelock_spinlock, flags);
		if (val > dhd->wakelock_rx_timeout_enable)
			dhd->wakelock_rx_timeout_enable = val;
		spin_unlock_irqrestore(&dhd->wakelock_spinlock, flags);
	}
	return 0;
}

int dhd_os_wake_lock_ctrl_timeout_enable(dhd_pub_t *pub, int val)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;

	if (dhd) {
		spin_lock_irqsave(&dhd->wakelock_spinlock, flags);
		if (val > dhd->wakelock_ctrl_timeout_enable)
			dhd->wakelock_ctrl_timeout_enable = val;
		spin_unlock_irqrestore(&dhd->wakelock_spinlock, flags);
	}
	return 0;
}

int dhd_os_wake_lock_ctrl_timeout_cancel(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;

	if (dhd) {
		spin_lock_irqsave(&dhd->wakelock_spinlock, flags);
		dhd->wakelock_ctrl_timeout_enable = 0;
#ifdef CONFIG_HAS_WAKELOCK
		if (wake_lock_active(&dhd->wl_ctrlwake))
			wake_unlock(&dhd->wl_ctrlwake);
#endif
		spin_unlock_irqrestore(&dhd->wakelock_spinlock, flags);
	}
	return 0;
}

int net_os_wake_lock_rx_timeout_enable(struct net_device *dev, int val)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	int ret = 0;

	if (dhd)
		ret = dhd_os_wake_lock_rx_timeout_enable(&dhd->pub, val);
	return ret;
}

int net_os_wake_lock_ctrl_timeout_enable(struct net_device *dev, int val)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	int ret = 0;

	if (dhd)
		ret = dhd_os_wake_lock_ctrl_timeout_enable(&dhd->pub, val);
	return ret;
}

int dhd_os_wake_lock(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;
	int ret = 0;

	if (dhd) {
		spin_lock_irqsave(&dhd->wakelock_spinlock, flags);

		if (dhd->wakelock_counter == 0 && !dhd->waive_wakelock) {
#ifdef CONFIG_HAS_WAKELOCK
			wake_lock(&dhd->wl_wifi);
#elif defined(BCMSDIO) && (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 36))
			dhd_bus_dev_pm_stay_awake(pub);
#endif
		}
		dhd->wakelock_counter++;
		ret = dhd->wakelock_counter;
		spin_unlock_irqrestore(&dhd->wakelock_spinlock, flags);
	}
	return ret;
}

int net_os_wake_lock(struct net_device *dev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	int ret = 0;

	if (dhd)
		ret = dhd_os_wake_lock(&dhd->pub);
	return ret;
}

int dhd_os_wake_unlock(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;
	int ret = 0;

	dhd_os_wake_lock_timeout(pub);
	if (dhd) {
		spin_lock_irqsave(&dhd->wakelock_spinlock, flags);
		if (dhd->wakelock_counter > 0) {
			dhd->wakelock_counter--;
			if (dhd->wakelock_counter == 0 && !dhd->waive_wakelock) {
#ifdef CONFIG_HAS_WAKELOCK
				wake_unlock(&dhd->wl_wifi);
#elif defined(BCMSDIO) && (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 36))
				dhd_bus_dev_pm_relax(pub);
#endif
			}
			ret = dhd->wakelock_counter;
		}
		spin_unlock_irqrestore(&dhd->wakelock_spinlock, flags);
	}
	return ret;
}

int dhd_os_check_wakelock(dhd_pub_t *pub)
{
#if defined(CONFIG_HAS_WAKELOCK) || (defined(BCMSDIO) && (LINUX_VERSION_CODE > \
	KERNEL_VERSION(2, 6, 36)))
	dhd_info_t *dhd;

	if (!pub)
		return 0;
	dhd = (dhd_info_t *)(pub->info);
#endif 

#ifdef CONFIG_HAS_WAKELOCK
	
	if (dhd && (wake_lock_active(&dhd->wl_wifi) ||
		(wake_lock_active(&dhd->wl_wdwake))))
		return 1;
#elif defined(BCMSDIO) && (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 36))
	if (dhd && (dhd->wakelock_counter > 0) && dhd_bus_dev_pm_enabled(pub))
		return 1;
#endif
	return 0;
}

int dhd_os_check_wakelock_all(dhd_pub_t *pub)
{
#if defined(CONFIG_HAS_WAKELOCK) || (defined(BCMSDIO) && (LINUX_VERSION_CODE > \
	KERNEL_VERSION(2, 6, 36)))
	dhd_info_t *dhd;

	if (!pub)
		return 0;
	dhd = (dhd_info_t *)(pub->info);
#endif 

#ifdef CONFIG_HAS_WAKELOCK
	
	if (dhd && (wake_lock_active(&dhd->wl_wifi) ||
		wake_lock_active(&dhd->wl_wdwake) ||
		wake_lock_active(&dhd->wl_rxwake) ||
#ifdef BCMPCIE_OOB_HOST_WAKE
		wake_lock_active(&dhd->wl_intrwake) ||
#endif 
		wake_lock_active(&dhd->wl_ctrlwake))) {
		return 1;
	}
#elif defined(BCMSDIO) && (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 36))
	if (dhd && (dhd->wakelock_counter > 0) && dhd_bus_dev_pm_enabled(pub))
		return 1;
#endif 
	return 0;
}

int net_os_wake_unlock(struct net_device *dev)
{
	dhd_info_t *dhd = DHD_DEV_INFO(dev);
	int ret = 0;

	if (dhd)
		ret = dhd_os_wake_unlock(&dhd->pub);
	return ret;
}

int dhd_os_wd_wake_lock(dhd_pub_t *pub)
{
#if !defined(CUSTOMER_HW_ONE) && !defined(PCIE_FULL_DONGLE)
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;
	int ret = 0;

	if (dhd) {
		spin_lock_irqsave(&dhd->wakelock_spinlock, flags);
#ifdef CONFIG_HAS_WAKELOCK
		
		if (!dhd->wakelock_wd_counter)
			wake_lock(&dhd->wl_wdwake);
#endif
		dhd->wakelock_wd_counter++;
		ret = dhd->wakelock_wd_counter;
		spin_unlock_irqrestore(&dhd->wakelock_spinlock, flags);
	}
	return ret;
#else
	return 0;
#endif 
}

int dhd_os_wd_wake_unlock(dhd_pub_t *pub)
{
#if !defined(CUSTOMER_HW_ONE) && !defined(PCIE_FULL_DONGLE)
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;
	int ret = 0;

	if (dhd) {
		spin_lock_irqsave(&dhd->wakelock_spinlock, flags);
		if (dhd->wakelock_wd_counter) {
			dhd->wakelock_wd_counter = 0;
#ifdef CONFIG_HAS_WAKELOCK
			wake_unlock(&dhd->wl_wdwake);
#endif
		}
		spin_unlock_irqrestore(&dhd->wakelock_spinlock, flags);
	}
	return ret;
#else
	return 0;
#endif 
}

#ifdef BCMPCIE_OOB_HOST_WAKE
int dhd_os_oob_irq_wake_lock_timeout(dhd_pub_t *pub, int val)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	int ret = 0;

	if (dhd) {
#ifdef CONFIG_HAS_WAKELOCK
		DHD_INTR(("%s: wl_intrwake lock %d\n", __FUNCTION__, val));
		wake_lock_timeout(&dhd->wl_intrwake, msecs_to_jiffies(val));
#endif
	}
	return ret;
}

int dhd_os_oob_irq_wake_unlock(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	int ret = 0;

	if (dhd) {
#ifdef CONFIG_HAS_WAKELOCK
		
		if (wake_lock_active(&dhd->wl_intrwake)) {
			DHD_INTR(("%s: wl_intrwake unlock\n", __FUNCTION__));
			wake_unlock(&dhd->wl_intrwake);
		}
#endif
	}
	return ret;
}
#endif 

int dhd_os_wake_lock_waive(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;
	int ret = 0;

	if (dhd) {
		spin_lock_irqsave(&dhd->wakelock_spinlock, flags);
		
		if (dhd->waive_wakelock == FALSE) {
			
			dhd->wakelock_before_waive = dhd->wakelock_counter;
			dhd->waive_wakelock = TRUE;
		}
		ret = dhd->wakelock_wd_counter;
		spin_unlock_irqrestore(&dhd->wakelock_spinlock, flags);
	}
	return ret;
}

int dhd_os_wake_lock_restore(dhd_pub_t *pub)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);
	unsigned long flags;
	int ret = 0;

	if (!dhd)
		return 0;

	spin_lock_irqsave(&dhd->wakelock_spinlock, flags);
	
	if (!dhd->waive_wakelock)
		goto exit;

	dhd->waive_wakelock = FALSE;
	if (dhd->wakelock_before_waive == 0 && dhd->wakelock_counter > 0) {
#ifdef CONFIG_HAS_WAKELOCK
		wake_lock(&dhd->wl_wifi);
#elif defined(BCMSDIO) && (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 36))
		dhd_bus_dev_pm_stay_awake(&dhd->pub);
#endif
	} else if (dhd->wakelock_before_waive > 0 && dhd->wakelock_counter == 0) {
#ifdef CONFIG_HAS_WAKELOCK
		wake_unlock(&dhd->wl_wifi);
#elif defined(BCMSDIO) && (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 36))
		dhd_bus_dev_pm_relax(&dhd->pub);
#endif
	}
	dhd->wakelock_before_waive = 0;
exit:
	ret = dhd->wakelock_wd_counter;
	spin_unlock_irqrestore(&dhd->wakelock_spinlock, flags);
	return ret;
}

bool dhd_os_check_if_up(dhd_pub_t *pub)
{
	if (!pub)
		return FALSE;
	return pub->up;
}

#if defined(BCMSDIO)
void dhd_set_version_info(dhd_pub_t *dhdp, char *fw)
{
	int i;

	i = snprintf(info_string, sizeof(info_string),
		"  Driver: %s\n  Firmware: %s ", EPI_VERSION_STR, fw);

	if (!dhdp)
		return;

	i = snprintf(&info_string[i], sizeof(info_string) - i,
		"\n  Chip: %x Rev %x Pkg %x", dhd_bus_chip_id(dhdp),
		dhd_bus_chiprev_id(dhdp), dhd_bus_chippkg_id(dhdp));
}
#endif 
int dhd_ioctl_entry_local(struct net_device *net, wl_ioctl_t *ioc, int cmd)
{
	int ifidx;
	int ret = 0;
	dhd_info_t *dhd = NULL;

	if (!net || !DEV_PRIV(net)) {
		DHD_ERROR(("%s invalid parameter\n", __FUNCTION__));
		return -EINVAL;
	}

	dhd = DHD_DEV_INFO(net);
	if (!dhd)
		return -EINVAL;

	ifidx = dhd_net2idx(dhd, net);
	if (ifidx == DHD_BAD_IF) {
		DHD_ERROR(("%s bad ifidx\n", __FUNCTION__));
		return -ENODEV;
	}

	DHD_OS_WAKE_LOCK(&dhd->pub);
	DHD_PERIM_LOCK(&dhd->pub);

	ret = dhd_wl_ioctl(&dhd->pub, ifidx, ioc, ioc->buf, ioc->len);
	dhd_check_hang(net, &dhd->pub, ret);

	DHD_PERIM_UNLOCK(&dhd->pub);
	DHD_OS_WAKE_UNLOCK(&dhd->pub);

	return ret;
}

bool dhd_os_check_hang(dhd_pub_t *dhdp, int ifidx, int ret)
{
	struct net_device *net;

	net = dhd_idx2net(dhdp, ifidx);
	if (!net) {
		DHD_ERROR(("%s : Invalid index : %d\n", __FUNCTION__, ifidx));
		return -EINVAL;
	}

	return dhd_check_hang(net, dhdp, ret);
}

int dhd_get_instance(dhd_pub_t *dhdp)
{
	return dhdp->info->unit;
}


#ifdef PROP_TXSTATUS

void dhd_wlfc_plat_init(void *dhd)
{
	return;
}

void dhd_wlfc_plat_deinit(void *dhd)
{
	return;
}

bool dhd_wlfc_skip_fc(void)
{
	return FALSE;
}
#endif 

#ifdef BCMDBGFS

#include <linux/debugfs.h>

extern uint32 dhd_readregl(void *bp, uint32 addr);
extern uint32 dhd_writeregl(void *bp, uint32 addr, uint32 data);

typedef struct dhd_dbgfs {
	struct dentry	*debugfs_dir;
	struct dentry	*debugfs_mem;
	dhd_pub_t 	*dhdp;
	uint32 		size;
} dhd_dbgfs_t;

dhd_dbgfs_t g_dbgfs;

static int
dhd_dbg_state_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t
dhd_dbg_state_read(struct file *file, char __user *ubuf,
                       size_t count, loff_t *ppos)
{
	ssize_t rval;
	uint32 tmp;
	loff_t pos = *ppos;
	size_t ret;

	if (pos < 0)
		return -EINVAL;
	if (pos >= g_dbgfs.size || !count)
		return 0;
	if (count > g_dbgfs.size - pos)
		count = g_dbgfs.size - pos;

	
	tmp = dhd_readregl(g_dbgfs.dhdp->bus, file->f_pos & (~3));

	ret = copy_to_user(ubuf, &tmp, 4);
	if (ret == count)
		return -EFAULT;

	count -= ret;
	*ppos = pos + count;
	rval = count;

	return rval;
}


static ssize_t
dhd_debugfs_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
	loff_t pos = *ppos;
	size_t ret;
	uint32 buf;

	if (pos < 0)
		return -EINVAL;
	if (pos >= g_dbgfs.size || !count)
		return 0;
	if (count > g_dbgfs.size - pos)
		count = g_dbgfs.size - pos;

	ret = copy_from_user(&buf, ubuf, sizeof(uint32));
	if (ret == count)
		return -EFAULT;

	
	dhd_writeregl(g_dbgfs.dhdp->bus, file->f_pos & (~3), buf);

	return count;
}


loff_t
dhd_debugfs_lseek(struct file *file, loff_t off, int whence)
{
	loff_t pos = -1;

	switch (whence) {
		case 0:
			pos = off;
			break;
		case 1:
			pos = file->f_pos + off;
			break;
		case 2:
			pos = g_dbgfs.size - off;
	}
	return (pos < 0 || pos > g_dbgfs.size) ? -EINVAL : (file->f_pos = pos);
}

static const struct file_operations dhd_dbg_state_ops = {
	.read   = dhd_dbg_state_read,
	.write	= dhd_debugfs_write,
	.open   = dhd_dbg_state_open,
	.llseek	= dhd_debugfs_lseek
};

static void dhd_dbg_create(void)
{
	if (g_dbgfs.debugfs_dir) {
		g_dbgfs.debugfs_mem = debugfs_create_file("mem", 0644, g_dbgfs.debugfs_dir,
			NULL, &dhd_dbg_state_ops);
	}
}

void dhd_dbg_init(dhd_pub_t *dhdp)
{
	int err;

	g_dbgfs.dhdp = dhdp;
	g_dbgfs.size = 0x20000000; 

	g_dbgfs.debugfs_dir = debugfs_create_dir("dhd", 0);
	if (IS_ERR(g_dbgfs.debugfs_dir)) {
		err = PTR_ERR(g_dbgfs.debugfs_dir);
		g_dbgfs.debugfs_dir = NULL;
		return;
	}

	dhd_dbg_create();

	return;
}

void dhd_dbg_remove(void)
{
	debugfs_remove(g_dbgfs.debugfs_mem);
	debugfs_remove(g_dbgfs.debugfs_dir);

	bzero((unsigned char *) &g_dbgfs, sizeof(g_dbgfs));

}
#endif 

#ifdef WLMEDIA_HTSF

static
void dhd_htsf_addtxts(dhd_pub_t *dhdp, void *pktbuf)
{
	dhd_info_t *dhd = (dhd_info_t *)(dhdp->info);
	struct sk_buff *skb;
	uint32 htsf = 0;
	uint16 dport = 0, oldmagic = 0xACAC;
	char *p1;
	htsfts_t ts;

	

	p1 = (char*) PKTDATA(dhdp->osh, pktbuf);

	if (PKTLEN(dhdp->osh, pktbuf) > HTSF_MINLEN) {
		memcpy(&dport, p1+40, 2);
		dport = ntoh16(dport);
	}

	
	if (dport >= tsport && dport <= tsport + 20) {

		skb = (struct sk_buff *) pktbuf;

		htsf = dhd_get_htsf(dhd, 0);
		memset(skb->data + 44, 0, 2); 
		memcpy(skb->data+82, &oldmagic, 2);
		memcpy(skb->data+84, &htsf, 4);

		memset(&ts, 0, sizeof(htsfts_t));
		ts.magic  = HTSFMAGIC;
		ts.prio   = PKTPRIO(pktbuf);
		ts.seqnum = htsf_seqnum++;
		ts.c10    = get_cycles();
		ts.t10    = htsf;
		ts.endmagic = HTSFENDMAGIC;

		memcpy(skb->data + HTSF_HOSTOFFSET, &ts, sizeof(ts));
	}
}

static void dhd_dump_htsfhisto(histo_t *his, char *s)
{
	int pktcnt = 0, curval = 0, i;
	for (i = 0; i < (NUMBIN-2); i++) {
		curval += 500;
		printf("%d ",  his->bin[i]);
		pktcnt += his->bin[i];
	}
	printf(" max: %d TotPkt: %d neg: %d [%s]\n", his->bin[NUMBIN-2], pktcnt,
		his->bin[NUMBIN-1], s);
}

static
void sorttobin(int value, histo_t *histo)
{
	int i, binval = 0;

	if (value < 0) {
		histo->bin[NUMBIN-1]++;
		return;
	}
	if (value > histo->bin[NUMBIN-2])  
		histo->bin[NUMBIN-2] = value;

	for (i = 0; i < (NUMBIN-2); i++) {
		binval += 500; 
		if (value <= binval) {
			histo->bin[i]++;
			return;
		}
	}
	histo->bin[NUMBIN-3]++;
}

static
void dhd_htsf_addrxts(dhd_pub_t *dhdp, void *pktbuf)
{
	dhd_info_t *dhd = (dhd_info_t *)dhdp->info;
	struct sk_buff *skb;
	char *p1;
	uint16 old_magic;
	int d1, d2, d3, end2end;
	htsfts_t *htsf_ts;
	uint32 htsf;

	skb = PKTTONATIVE(dhdp->osh, pktbuf);
	p1 = (char*)PKTDATA(dhdp->osh, pktbuf);

	if (PKTLEN(osh, pktbuf) > HTSF_MINLEN) {
		memcpy(&old_magic, p1+78, 2);
		htsf_ts = (htsfts_t*) (p1 + HTSF_HOSTOFFSET - 4);
	}
	else
		return;

	if (htsf_ts->magic == HTSFMAGIC) {
		htsf_ts->tE0 = dhd_get_htsf(dhd, 0);
		htsf_ts->cE0 = get_cycles();
	}

	if (old_magic == 0xACAC) {

		tspktcnt++;
		htsf = dhd_get_htsf(dhd, 0);
		memcpy(skb->data+92, &htsf, sizeof(uint32));

		memcpy(&ts[tsidx].t1, skb->data+80, 16);

		d1 = ts[tsidx].t2 - ts[tsidx].t1;
		d2 = ts[tsidx].t3 - ts[tsidx].t2;
		d3 = ts[tsidx].t4 - ts[tsidx].t3;
		end2end = ts[tsidx].t4 - ts[tsidx].t1;

		sorttobin(d1, &vi_d1);
		sorttobin(d2, &vi_d2);
		sorttobin(d3, &vi_d3);
		sorttobin(end2end, &vi_d4);

		if (end2end > 0 && end2end >  maxdelay) {
			maxdelay = end2end;
			maxdelaypktno = tspktcnt;
			memcpy(&maxdelayts, &ts[tsidx], 16);
		}
		if (++tsidx >= TSMAX)
			tsidx = 0;
	}
}

uint32 dhd_get_htsf(dhd_info_t *dhd, int ifidx)
{
	uint32 htsf = 0, cur_cycle, delta, delta_us;
	uint32    factor, baseval, baseval2;
	cycles_t t;

	t = get_cycles();
	cur_cycle = t;

	if (cur_cycle >  dhd->htsf.last_cycle)
		delta = cur_cycle -  dhd->htsf.last_cycle;
	else {
		delta = cur_cycle + (0xFFFFFFFF -  dhd->htsf.last_cycle);
	}

	delta = delta >> 4;

	if (dhd->htsf.coef) {
		
	        factor = (dhd->htsf.coef*10 + dhd->htsf.coefdec1);
		baseval  = (delta*10)/factor;
		baseval2 = (delta*10)/(factor+1);
		delta_us  = (baseval -  (((baseval - baseval2) * dhd->htsf.coefdec2)) / 10);
		htsf = (delta_us << 4) +  dhd->htsf.last_tsf + HTSF_BUS_DELAY;
	}
	else {
		DHD_ERROR(("-------dhd->htsf.coef = 0 -------\n"));
	}

	return htsf;
}

static void dhd_dump_latency(void)
{
	int i, max = 0;
	int d1, d2, d3, d4, d5;

	printf("T1       T2       T3       T4           d1  d2   t4-t1     i    \n");
	for (i = 0; i < TSMAX; i++) {
		d1 = ts[i].t2 - ts[i].t1;
		d2 = ts[i].t3 - ts[i].t2;
		d3 = ts[i].t4 - ts[i].t3;
		d4 = ts[i].t4 - ts[i].t1;
		d5 = ts[max].t4-ts[max].t1;
		if (d4 > d5 && d4 > 0)  {
			max = i;
		}
		printf("%08X %08X %08X %08X \t%d %d %d   %d i=%d\n",
			ts[i].t1, ts[i].t2, ts[i].t3, ts[i].t4,
			d1, d2, d3, d4, i);
	}

	printf("current idx = %d \n", tsidx);

	printf("Highest latency %d pkt no.%d total=%d\n", maxdelay, maxdelaypktno, tspktcnt);
	printf("%08X %08X %08X %08X \t%d %d %d   %d\n",
	maxdelayts.t1, maxdelayts.t2, maxdelayts.t3, maxdelayts.t4,
	maxdelayts.t2 - maxdelayts.t1,
	maxdelayts.t3 - maxdelayts.t2,
	maxdelayts.t4 - maxdelayts.t3,
	maxdelayts.t4 - maxdelayts.t1);
}


static int
dhd_ioctl_htsf_get(dhd_info_t *dhd, int ifidx)
{
	wl_ioctl_t ioc;
	char buf[32];
	int ret;
	uint32 s1, s2;

	struct tsf {
		uint32 low;
		uint32 high;
	} tsf_buf;

	memset(&ioc, 0, sizeof(ioc));
	memset(&tsf_buf, 0, sizeof(tsf_buf));

	ioc.cmd = WLC_GET_VAR;
	ioc.buf = buf;
	ioc.len = (uint)sizeof(buf);
	ioc.set = FALSE;

	strncpy(buf, "tsf", sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	s1 = dhd_get_htsf(dhd, 0);
	if ((ret = dhd_wl_ioctl(&dhd->pub, ifidx, &ioc, ioc.buf, ioc.len)) < 0) {
		if (ret == -EIO) {
			DHD_ERROR(("%s: tsf is not supported by device\n",
				dhd_ifname(&dhd->pub, ifidx)));
			return -EOPNOTSUPP;
		}
		return ret;
	}
	s2 = dhd_get_htsf(dhd, 0);

	memcpy(&tsf_buf, buf, sizeof(tsf_buf));
	printf(" TSF_h=%04X lo=%08X Calc:htsf=%08X, coef=%d.%d%d delta=%d ",
		tsf_buf.high, tsf_buf.low, s2, dhd->htsf.coef, dhd->htsf.coefdec1,
		dhd->htsf.coefdec2, s2-tsf_buf.low);
	printf("lasttsf=%08X lastcycle=%08X\n", dhd->htsf.last_tsf, dhd->htsf.last_cycle);
	return 0;
}

void htsf_update(dhd_info_t *dhd, void *data)
{
	static ulong  cur_cycle = 0, prev_cycle = 0;
	uint32 htsf, tsf_delta = 0;
	uint32 hfactor = 0, cyc_delta, dec1 = 0, dec2, dec3, tmp;
	ulong b, a;
	cycles_t t;

	

	t = get_cycles();

	prev_cycle = cur_cycle;
	cur_cycle = t;

	if (cur_cycle > prev_cycle)
		cyc_delta = cur_cycle - prev_cycle;
	else {
		b = cur_cycle;
		a = prev_cycle;
		cyc_delta = cur_cycle + (0xFFFFFFFF - prev_cycle);
	}

	if (data == NULL)
		printf(" tsf update ata point er is null \n");

	memcpy(&prev_tsf, &cur_tsf, sizeof(tsf_t));
	memcpy(&cur_tsf, data, sizeof(tsf_t));

	if (cur_tsf.low == 0) {
		DHD_INFO((" ---- 0 TSF, do not update, return\n"));
		return;
	}

	if (cur_tsf.low > prev_tsf.low)
		tsf_delta = (cur_tsf.low - prev_tsf.low);
	else {
		DHD_INFO((" ---- tsf low is smaller cur_tsf= %08X, prev_tsf=%08X, \n",
		 cur_tsf.low, prev_tsf.low));
		if (cur_tsf.high > prev_tsf.high) {
			tsf_delta = cur_tsf.low + (0xFFFFFFFF - prev_tsf.low);
			DHD_INFO((" ---- Wrap around tsf coutner  adjusted TSF=%08X\n", tsf_delta));
		}
		else
			return; 
	}

	if (tsf_delta)  {
		hfactor = cyc_delta / tsf_delta;
		tmp  = 	(cyc_delta - (hfactor * tsf_delta))*10;
		dec1 =  tmp/tsf_delta;
		dec2 =  ((tmp - dec1*tsf_delta)*10) / tsf_delta;
		tmp  = 	(tmp   - (dec1*tsf_delta))*10;
		dec3 =  ((tmp - dec2*tsf_delta)*10) / tsf_delta;

		if (dec3 > 4) {
			if (dec2 == 9) {
				dec2 = 0;
				if (dec1 == 9) {
					dec1 = 0;
					hfactor++;
				}
				else {
					dec1++;
				}
			}
			else
				dec2++;
		}
	}

	if (hfactor) {
		htsf = ((cyc_delta * 10)  / (hfactor*10+dec1)) + prev_tsf.low;
		dhd->htsf.coef = hfactor;
		dhd->htsf.last_cycle = cur_cycle;
		dhd->htsf.last_tsf = cur_tsf.low;
		dhd->htsf.coefdec1 = dec1;
		dhd->htsf.coefdec2 = dec2;
	}
	else {
		htsf = prev_tsf.low;
	}
}

#endif 

#ifdef CUSTOMER_HW_ONE
static inline void set_wlan_ioprio(void)
{
	int ret, prio;

	if (wlan_ioprio_idle == 1) {
		prio = ((IOPRIO_CLASS_IDLE << IOPRIO_CLASS_SHIFT) | 0);
	} else {
		prio = ((IOPRIO_CLASS_NONE << IOPRIO_CLASS_SHIFT) | 4);
	}
	ret = set_task_ioprio(current, prio);
	DHD_INFO(("set_wlan_ioprio: prio=0x%X, ret=%d\n", prio, ret));
}

static int
rt_class(int priority)
{
	
	return ((dhd_dpc_prio < MAX_RT_PRIO) && (dhd_dpc_prio != 0))? 1: 0;
}

static int
rxf_rt_class(int priority)
{
	
	return ((dhd_rxf_prio < MAX_RT_PRIO) && (dhd_rxf_prio != 0))? 1: 0;
}

static void
adjust_thread_priority(void)
{
	struct sched_param param;
	int ret = 0;

	if (multi_core_locked) {
		if ((current->on_cpu > 0) && !rt_class(dhd_dpc_prio)) {
			param.sched_priority = dhd_dpc_prio = (MAX_RT_PRIO-1);
			ret = setScheduler(current, SCHED_FIFO, &param);
			DHD_ERROR(("change dhd_dpc to SCHED_FIFO priority: %d, ret: %d",
				param.sched_priority, ret));
		}
	} else {
		if (rt_class(dhd_dpc_prio)) {
			param.sched_priority = dhd_dpc_prio = 0;
			ret = setScheduler(current, SCHED_NORMAL, &param);
			DHD_ERROR(("change dhd_dpc to SCHED_NORMAL priority: %d, ret: %d",
				param.sched_priority, ret));
		}
	}
}

static void
adjust_rxf_thread_priority(void)
{
	struct sched_param param;
	int ret = 0;

	if (multi_core_locked) {
		if ((current->on_cpu > 0) && !rxf_rt_class(dhd_rxf_prio)) {
			
			{
				
				cpumask_t mask;
				cpumask_clear(&mask);
				cpumask_set_cpu(nr_cpu_ids-1, &mask);
				if (sched_setaffinity(0, &mask) < 0) {
					
				}
				else {
					DHD_ERROR(("[adjust_rxf_thread_priority]"
						"sched_setaffinity ok"));
					param.sched_priority = dhd_rxf_prio = (MAX_RT_PRIO-1);
					ret = setScheduler(current, SCHED_FIFO, &param);
					DHD_ERROR(("change dhd_rxf to SCHED_FIFO "
						"priority: %d, ret: %d",
						param.sched_priority, ret));
				}
			}
			
		}
	} else {
		if (rxf_rt_class(dhd_rxf_prio)) {
			param.sched_priority = dhd_rxf_prio = 0;
			ret = setScheduler(current, SCHED_NORMAL, &param);
			DHD_ERROR(("change dhd_rxf to SCHED_NORMAL priority: %d, ret: %d",
				param.sched_priority, ret));
		}
	}
}

int wl_android_set_pktfilter(struct net_device *dev, struct dd_pkt_filter_s *data)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	return dhd_set_pktfilter(&dhd->pub, data->add, data->id, data->offset,
		data->mask, data->pattern);
}

void wl_android_enable_pktfilter(struct net_device *dev, int multicastlock)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);
	multicast_lock = multicastlock;
	
	dhd_enable_packet_filter(0, &dhd->pub);
}

bool check_hang_already(struct net_device *dev)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(dev);

	if (dhd->pub.hang_was_sent)
		return TRUE;
	else
		return FALSE;
}

void dhd_htc_wake_lock_timeout(dhd_pub_t *pub, int sec)
{
	dhd_info_t *dhd = (dhd_info_t *)(pub->info);

#ifdef CONFIG_HAS_WAKELOCK
	wake_lock_timeout(&dhd->wl_htc, sec * HZ);
#endif
}

int dhd_get_txrx_stats(struct net_device *net, unsigned long *rx_packets, unsigned long *tx_packets)
{
	dhd_info_t *dhd = *(dhd_info_t **)netdev_priv(net);
	dhd_pub_t *dhdp;

	if (!dhd)
		return -1;

	dhdp = &dhd->pub;

	if (!dhdp)
		return -1;

	*rx_packets = dhdp->rx_packets;
	*tx_packets = dhdp->tx_packets;

	return 0;
}

int dhd_set_keepalive(int value)
{
	char *str;
	int str_len;
	int buf_len;
	char buf[256];
	wl_keep_alive_pkt_t keep_alive_pkt;
	wl_keep_alive_pkt_t *keep_alive_pktp;
	int ret = 0;

	dhd_pub_t *dhd = priv_dhdp;
#ifdef HTC_KlocWork
	memset(&keep_alive_pkt, 0, sizeof(keep_alive_pkt));
#endif
	
	str = "keep_alive";
	str_len = strlen(str);
	strncpy(buf, str, str_len);
	buf[str_len] = '\0';
	buf_len = str_len + 1;
	keep_alive_pktp = (wl_keep_alive_pkt_t *) (buf + str_len + 1);

	keep_alive_pkt.period_msec = htod32(55000); 
	keep_alive_pkt.len_bytes = 0;
	buf_len += WL_KEEP_ALIVE_FIXED_LEN;
	bzero(keep_alive_pkt.data, sizeof(keep_alive_pkt.data));
	memcpy((char*)keep_alive_pktp, &keep_alive_pkt, WL_KEEP_ALIVE_FIXED_LEN);

	ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, buf, buf_len, TRUE, 0);
	DHD_ERROR(("%s set result ret[%d]\n", __FUNCTION__, ret));

	return ret;
}

int dhdhtc_update_wifi_power_mode(int is_screen_off)
{
	int pm_type;
	dhd_pub_t *dhd = priv_dhdp;
	int ret = 0;

	if (!dhd) {
		DHD_ERROR(("dhd is not attached\n"));
		return -1;
	}

	if (dhdhtc_power_ctrl_mask) {
		
		
		if (!wl_cfg80211_is_vsdb_mode()) {
			DHD_ERROR(("power active. ctrl_mask: 0x%x\n", dhdhtc_power_ctrl_mask));
			pm_type = PM_OFF;
			ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_PM, (char *)&pm_type,
				sizeof(pm_type), TRUE, 0);
			if (ret < 0) {
				DHD_ERROR(("%s set PM fail ret[%d]\n", __FUNCTION__, ret));
				return ret;
			}
		}
	} else {
		
		
		if (!wl_cfg80211_is_vsdb_mode()) {
			pm_type = PM_FAST;
			DHD_ERROR(("update pm: %s, wifiLock: %d\n",
				pm_type == 1 ? "PM_MAX":"PM_FAST", dhdcdc_wifiLock));
			ret = dhd_wl_ioctl_cmd(dhd,
				WLC_SET_PM, (char *)&pm_type, sizeof(pm_type), TRUE, 0);
			if (ret < 0) {
				DHD_ERROR(("%s set PM fail ret[%d]\n", __FUNCTION__, ret));
				return ret;
			}
		}
	}

	return 0;
}

int dhdhtc_set_power_control(int power_mode, unsigned int reason)
{

	if (reason < DHDHTC_POWER_CTRL_MAX_NUM) {
		if (power_mode) {
			dhdhtc_power_ctrl_mask |= 0x1<<reason;
		} else {
			dhdhtc_power_ctrl_mask &= ~(0x1<<reason);
		}

	} else {
		DHD_ERROR(("%s: Error reason: %u", __func__, reason));
		return -1;
	}

	return 0;
}

unsigned int dhdhtc_get_cur_pwr_ctrl(void)
{
	return dhdhtc_power_ctrl_mask;
}

extern int wl_android_is_during_wifi_call(void);

int dhdhtc_update_dtim_listen_interval(int is_screen_off)
{
	char iovbuf[32];
	int bcn_li_dtim;
	int pm2_sleep_ret;
	int ret = 0;
	dhd_pub_t *dhd = priv_dhdp;

	if (!dhd) {
		DHD_ERROR(("dhd is not attached\n"));
		return -1;
	}

	if (wl_android_is_during_wifi_call() || !is_screen_off || dhdcdc_wifiLock)
		bcn_li_dtim = 0;
	else
		bcn_li_dtim = dhd_get_suspend_bcn_li_dtim(dhd);

	if (is_screen_off)
		pm2_sleep_ret = 50;
	else
		pm2_sleep_ret = 200;

	bcm_mkiovar("pm2_sleep_ret", (char *)&pm2_sleep_ret,
		4, iovbuf, sizeof(iovbuf));

	ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
	if (ret < 0) {
		DHD_ERROR(("%s set pm2_sleep_ret ret[%d] \n", __FUNCTION__, ret));
		return ret;
	}

	
	bcm_mkiovar("bcn_li_dtim", (char *)&bcn_li_dtim,
		4, iovbuf, sizeof(iovbuf));
	ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);
	if (ret < 0) {
		DHD_ERROR(("%s ret[%d] \n", __FUNCTION__, ret));
		return ret;
	}
	DHD_ERROR(("update dtim listern interval: %d\n", bcn_li_dtim));

	return ret;
}
#endif 

#ifdef CUSTOM_SET_CPUCORE
void dhd_set_cpucore(dhd_pub_t *dhd, int set)
{
	int e_dpc = 0, e_rxf = 0, retry_set = 0;

	if (!(dhd->chan_isvht80)) {
		DHD_ERROR(("%s: chan_status(%d) cpucore!!!\n", __FUNCTION__, dhd->chan_isvht80));
		return;
	}

	if (DPC_CPUCORE) {
		do {
			if (set == TRUE) {
				e_dpc = set_cpus_allowed_ptr(dhd->current_dpc,
					cpumask_of(DPC_CPUCORE));
			} else {
				e_dpc = set_cpus_allowed_ptr(dhd->current_dpc,
					cpumask_of(PRIMARY_CPUCORE));
			}
			if (retry_set++ > MAX_RETRY_SET_CPUCORE) {
				DHD_ERROR(("%s: dpc(%d) invalid cpu!\n", __FUNCTION__, e_dpc));
				return;
			}
			if (e_dpc < 0)
				OSL_SLEEP(1);
		} while (e_dpc < 0);
	}
	if (RXF_CPUCORE) {
		do {
			if (set == TRUE) {
				e_rxf = set_cpus_allowed_ptr(dhd->current_rxf,
					cpumask_of(RXF_CPUCORE));
			} else {
				e_rxf = set_cpus_allowed_ptr(dhd->current_rxf,
					cpumask_of(PRIMARY_CPUCORE));
			}
			if (retry_set++ > MAX_RETRY_SET_CPUCORE) {
				DHD_ERROR(("%s: rxf(%d) invalid cpu!\n", __FUNCTION__, e_rxf));
				return;
			}
			if (e_rxf < 0)
				OSL_SLEEP(1);
		} while (e_rxf < 0);
	}
#ifdef DHD_OF_SUPPORT
	interrupt_set_cpucore(set);
#endif 
	DHD_TRACE(("%s: set(%d) cpucore success!\n", __FUNCTION__, set));

	return;
}
#endif 
#if defined(DHD_TCP_WINSIZE_ADJUST)
static int dhd_port_list_match(int port)
{
	int i;
	for (i = 0; i < MAX_TARGET_PORTS; i++) {
		if (target_ports[i] == port)
			return 1;
	}
	return 0;
}
static void dhd_adjust_tcp_winsize(int op_mode, struct sk_buff *skb)
{
	struct iphdr *ipheader;
	struct tcphdr *tcpheader;
	uint16 win_size;
	int32 incremental_checksum;

	if (!(op_mode & DHD_FLAG_HOSTAP_MODE))
		return;
	if (skb == NULL || skb->data == NULL)
		return;

	ipheader = (struct iphdr*)(skb->data);

	if (ipheader->protocol == IPPROTO_TCP) {
		tcpheader = (struct tcphdr*) skb_pull(skb, (ipheader->ihl)<<2);
		if (tcpheader) {
			win_size = ntoh16(tcpheader->window);
			if (win_size < MIN_TCP_WIN_SIZE &&
				dhd_port_list_match(ntoh16(tcpheader->dest))) {
				incremental_checksum = ntoh16(tcpheader->check);
				incremental_checksum += win_size - win_size*WIN_SIZE_SCALE_FACTOR;
				if (incremental_checksum < 0)
					--incremental_checksum;
				tcpheader->window = hton16(win_size*WIN_SIZE_SCALE_FACTOR);
				tcpheader->check = hton16((unsigned short)incremental_checksum);
			}
		}
		skb_push(skb, (ipheader->ihl)<<2);
	}
}
#endif 

int dhd_get_ap_isolate(dhd_pub_t *dhdp, uint32 idx)
{
	dhd_info_t *dhd = dhdp->info;
	dhd_if_t *ifp;

	ASSERT(idx < DHD_MAX_IFS);

	ifp = dhd->iflist[idx];

	return ifp->ap_isolate;
}

int dhd_set_ap_isolate(dhd_pub_t *dhdp, uint32 idx, int val)
{
	dhd_info_t *dhd = dhdp->info;
	dhd_if_t *ifp;

	ASSERT(idx < DHD_MAX_IFS);

	ifp = dhd->iflist[idx];

	ifp->ap_isolate = val;

	return 0;
}

#if defined(USE_STATIC_MEMDUMP)
void dhd_schedule_memdump(dhd_pub_t *dhdp, uint8 *buf, uint32 size)
{
	dhd_dump_t *dump = NULL;
	dump = MALLOC(dhdp->osh, sizeof(dhd_dump_t));
	dump->buf = buf;
	dump->bufsize = size;

#if defined(__ARM_ARCH_7A__)
	DHD_ERROR(("%s: buf(va)=%x, buf(pa)=%x, bufsize=%d\n", __FUNCTION__,
		(uint32)buf, (uint32)__virt_to_phys((ulong)buf), size));
#endif 
	if (dhdp->memdump_enabled == DUMP_MEMONLY) {
		
	}
#ifdef CUSTOMER_HW_ONE
	if (!atomic_read(&ramdump_in_progress)) {
		atomic_set(&ramdump_in_progress, 1);
		DHD_ERROR(("%s:Ramdump in progress ramdump_in_progress =%d\n",
			__FUNCTION__, atomic_read(&ramdump_in_progress)));
#endif 
	dhd_deferred_schedule_work(dhdp->info->dhd_deferred_wq, (void *)dump,
		DHD_WQ_WORK_SOC_RAM_DUMP, dhd_mem_dump, DHD_WORK_PRIORITY_HIGH);
#ifdef CUSTOMER_HW_ONE
	} else {
		DHD_ERROR(("%s:Ramdump in progress skip this time ramdump_in_progress =%d\n",
			__FUNCTION__, atomic_read(&ramdump_in_progress)));
	}
#endif 
}

static void
dhd_mem_dump(void *handle, void *event_info, u8 event)
{
	dhd_info_t *dhd = handle;
	dhd_dump_t *dump = event_info;

	if (!dhd || !dump)
		return;
#ifdef CUSTOMER_HW_ONE
	if (dhd->pub.memdump_enabled == DUMP_MEMFILE_USR_TRIGGER) {
		DHD_ERROR(("%s: USER writing SoC_RAM dump to the file \n", __FUNCTION__));
		if (user_triger_write_to_file(&dhd->pub, dump->buf, dump->bufsize)) {
			DHD_ERROR(("%s: writing SoC_RAM dump to the file failed\n",
				__FUNCTION__));
			dhd->pub.memdump_enabled = DUMP_MEMFILE_BUGON;
			atomic_set(&ramdump_in_progress, 0);
			return;
		}
		dhd->pub.memdump_enabled = DUMP_MEMFILE_BUGON;
		atomic_set(&ramdump_in_progress, 0);
		return;
	}
#endif 
	if (write_to_file(&dhd->pub, dump->buf, dump->bufsize)) {
		DHD_ERROR(("%s: writing SoC_RAM dump to the file failed\n", __FUNCTION__));
	}
	MFREE(dhd->pub.osh, dump, sizeof(dhd_dump_t));

	if (dhd->pub.memdump_enabled == DUMP_MEMFILE_BUGON) {
		
	}
#ifdef CUSTOMER_HW_ONE
	else if (dhd->pub.memdump_enabled == DUMP_MEMFILE_HANG) {
		dhd_os_send_hang_message(&dhd->pub);
	}
	if (dhd->pub.dongle_trap_occured) {
		dhd->pub.memdump_enabled = DUMP_DISABLED;
		DHD_ERROR(("%s:Firmware hang trigger dump\n", __FUNCTION__));
	} else {
		DHD_ERROR(("%s:other path trigger dump\n", __FUNCTION__));
	}
	atomic_set(&ramdump_in_progress, 0);
#endif 
}
#endif 

#ifdef DHD_WMF
dhd_wmf_t* dhd_wmf_conf(dhd_pub_t *dhdp, uint32 idx)
{
	dhd_info_t *dhd = dhdp->info;
	dhd_if_t *ifp;

	ASSERT(idx < DHD_MAX_IFS);

	ifp = dhd->iflist[idx];
	return &ifp->wmf;
}
#endif 


#if defined(DHD_UNICAST_DHCP) || defined(DHD_L2_FILTER)
static int
dhd_get_pkt_ether_type(dhd_pub_t *pub, void *pktbuf,
	uint8 **data_ptr, int *len_ptr, uint16 *et_ptr, bool *snap_ptr)
{
	uint8 *frame = PKTDATA(pub->osh, pktbuf);
	int length = PKTLEN(pub->osh, pktbuf);
	uint8 *pt;			
	uint16 ethertype;
	bool snap = FALSE;
	
	if (length < ETHER_HDR_LEN) {
		DHD_ERROR(("dhd: %s: short eth frame (%d)\n",
		           __FUNCTION__, length));
		return BCME_ERROR;
	} else if (ntoh16_ua(frame + ETHER_TYPE_OFFSET) >= ETHER_TYPE_MIN) {
		
		pt = frame + ETHER_TYPE_OFFSET;
	} else if (length >= ETHER_HDR_LEN + SNAP_HDR_LEN + ETHER_TYPE_LEN &&
	           !bcmp(llc_snap_hdr, frame + ETHER_HDR_LEN, SNAP_HDR_LEN)) {
		pt = frame + ETHER_HDR_LEN + SNAP_HDR_LEN;
		snap = TRUE;
	} else {
		DHD_INFO(("DHD: %s: non-SNAP 802.3 frame\n",
		           __FUNCTION__));
		return BCME_ERROR;
	}

	ethertype = ntoh16_ua(pt);

	
	if (ethertype == ETHER_TYPE_8021Q) {
		pt += VLAN_TAG_LEN;

		if ((pt + ETHER_TYPE_LEN) > (frame + length)) {
			DHD_ERROR(("dhd: %s: short VLAN frame (%d)\n",
			          __FUNCTION__, length));
			return BCME_ERROR;
		}

		ethertype = ntoh16_ua(pt);
	}

	*data_ptr = pt + ETHER_TYPE_LEN;
	*len_ptr = length - (pt + ETHER_TYPE_LEN - frame);
	*et_ptr = ethertype;
	*snap_ptr = snap;
	return BCME_OK;
}

static int
dhd_get_pkt_ip_type(dhd_pub_t *pub, void *pktbuf,
	uint8 **data_ptr, int *len_ptr, uint8 *prot_ptr)
{
	struct ipv4_hdr *iph;		
	int iplen;			
	uint16 ethertype, iphdrlen, ippktlen;
	uint16 iph_frag;
	uint8 prot;
	bool snap;

	if (dhd_get_pkt_ether_type(pub, pktbuf, (uint8 **)&iph,
	    &iplen, &ethertype, &snap) != 0)
		return BCME_ERROR;

	if (ethertype != ETHER_TYPE_IP) {
		return BCME_ERROR;
	}

	
	if (iplen < IPV4_OPTIONS_OFFSET || (IP_VER(iph) != IP_VER_4)) {
		return BCME_ERROR;
	}

	
	iphdrlen = IPV4_HLEN(iph);

	ippktlen = ntoh16(iph->tot_len);
	if (ippktlen < iplen) {

		DHD_INFO(("%s: extra frame length ignored\n",
		          __FUNCTION__));
		iplen = ippktlen;
	} else if (ippktlen > iplen) {
		DHD_ERROR(("dhd: %s: truncated IP packet (%d)\n",
		           __FUNCTION__, ippktlen - iplen));
		return BCME_ERROR;
	}

	if (iphdrlen < IPV4_OPTIONS_OFFSET || iphdrlen > iplen) {
		DHD_ERROR(("DHD: %s: IP-header-len (%d) out of range (%d-%d)\n",
		           __FUNCTION__, iphdrlen, IPV4_OPTIONS_OFFSET, iplen));
		return BCME_ERROR;
	}

	iph_frag = ntoh16(iph->frag);

	if ((iph_frag & IPV4_FRAG_MORE) || (iph_frag & IPV4_FRAG_OFFSET_MASK) != 0) {
		DHD_INFO(("DHD:%s: IP fragment not handled\n",
		           __FUNCTION__));
		return BCME_ERROR;
	}

	prot = IPV4_PROT(iph);

	*data_ptr = (((uint8 *)iph) + iphdrlen);
	*len_ptr = iplen - iphdrlen;
	*prot_ptr = prot;
	return BCME_OK;
}
#endif 

#ifdef DHD_UNICAST_DHCP
static
int dhd_convert_dhcp_broadcast_ack_to_unicast(dhd_pub_t *pub, void *pktbuf, int ifidx)
{
	dhd_sta_t* stainfo;
	uint8 *eh = PKTDATA(pub->osh, pktbuf);
	uint8 *udph;
	uint8 *dhcp;
	uint8 *chaddr;
	int udpl;
	int dhcpl;
	uint16 port;
	uint8 prot;

	if (!ETHER_ISMULTI(eh + ETHER_DEST_OFFSET))
	    return BCME_ERROR;
	if (dhd_get_pkt_ip_type(pub, pktbuf, &udph, &udpl, &prot) != 0)
		return BCME_ERROR;
	if (prot != IP_PROT_UDP)
		return BCME_ERROR;
	
	if (udpl < UDP_HDR_LEN) {
		DHD_ERROR(("DHD: %s: short UDP frame, ignored\n",
		    __FUNCTION__));
		return BCME_ERROR;
	}
	port = ntoh16_ua(udph + UDP_DEST_PORT_OFFSET);
	
	if (port != DHCP_PORT_CLIENT)
		return BCME_ERROR;

	dhcp = udph + UDP_HDR_LEN;
	dhcpl = udpl - UDP_HDR_LEN;

	if (dhcpl < DHCP_CHADDR_OFFSET + ETHER_ADDR_LEN) {
		DHD_ERROR(("DHD: %s: short DHCP frame, ignored\n",
		    __FUNCTION__));
		return BCME_ERROR;
	}
	
	if (*(dhcp + DHCP_TYPE_OFFSET) != DHCP_TYPE_REPLY)
		return BCME_ERROR;
	chaddr = dhcp + DHCP_CHADDR_OFFSET;
	stainfo = dhd_find_sta(pub, ifidx, chaddr);
	if (stainfo) {
		bcopy(chaddr, eh + ETHER_DEST_OFFSET, ETHER_ADDR_LEN);
		return BCME_OK;
	}
	return BCME_ERROR;
}
#endif 
#ifdef DHD_L2_FILTER
static
int dhd_l2_filter_block_ping(dhd_pub_t *pub, void *pktbuf, int ifidx)
{
	struct bcmicmp_hdr *icmph;
	struct bcmicmp_bdy *icmpb;
	int udpl;
	uint8 prot;

	if (dhd_get_pkt_ip_type(pub, pktbuf, (uint8 **)&icmph, &udpl, &prot) != 0) {
		return BCME_ERROR;
	}
	if (prot == IP_PROT_ICMP) {
		icmpb = (void *)&icmph->body[0];
		if (pub->trace_ping) {
			DHD_ERROR(("PING: icmp %d seq %d\n", icmph->type, ntoh16_ua(&icmpb->seq)));
		}
		if (icmph->type == ICMP_TYPE_ECHO_REQUEST)
			return BCME_OK;
	}
	return BCME_ERROR;
}
#endif 

#if defined(SET_RPS_CPUS)
int custom_rps_map_set(struct netdev_rx_queue *queue, char *buf, size_t len)
{
	struct rps_map *old_map, *map;
	cpumask_var_t mask;
	int err, cpu, i;
	static DEFINE_SPINLOCK(rps_map_lock);

	DHD_INFO(("%s : Entered.\n", __FUNCTION__));

	if (!alloc_cpumask_var(&mask, GFP_KERNEL)) {
		DHD_ERROR(("%s : alloc_cpumask_var fail.\n", __FUNCTION__));
		return -ENOMEM;
	}

	err = bitmap_parse(buf, len, cpumask_bits(mask), nr_cpumask_bits);
	if (err) {
		free_cpumask_var(mask);
		DHD_ERROR(("%s : bitmap_parse fail.\n", __FUNCTION__));
		return err;
	}

	map = kzalloc(max_t(unsigned int,
		RPS_MAP_SIZE(cpumask_weight(mask)), L1_CACHE_BYTES),
		GFP_KERNEL);
	if (!map) {
		free_cpumask_var(mask);
		DHD_ERROR(("%s : map malloc fail.\n", __FUNCTION__));
		return -ENOMEM;
	}

	i = 0;
	for_each_cpu(cpu, mask)
		map->cpus[i++] = cpu;

	if (i)
		map->len = i;
	else {
		kfree(map);
		DHD_ERROR(("%s : mapping cpu fail.\n", __FUNCTION__));
		map = NULL;
	}

	spin_lock(&rps_map_lock);
	old_map = rcu_dereference_protected(queue->rps_map,
		lockdep_is_held(&rps_map_lock));
	rcu_assign_pointer(queue->rps_map, map);
	spin_unlock(&rps_map_lock);

	if (map)
		static_key_slow_inc(&rps_needed);
	if (old_map) {
		kfree_rcu(old_map, rcu);
		static_key_slow_dec(&rps_needed);
	}
	free_cpumask_var(mask);

	DHD_INFO(("%s : Done. mapping cpu nummber : %d\n", __FUNCTION__, map->len));
	return map->len;
}

void custom_rps_map_clear(struct netdev_rx_queue *queue)
{
	struct rps_map *map;

	DHD_INFO(("%s : Entered.\n", __FUNCTION__));

	map = rcu_dereference_protected(queue->rps_map, 1);
	if (map) {
		RCU_INIT_POINTER(queue->rps_map, NULL);
		kfree_rcu(map, rcu);
		DHD_INFO(("%s : rps_cpus map clear.\n", __FUNCTION__));
	}
}
#endif 

#if defined(CUSTOMER_HW20) && defined(WLANAUDIO)
void
SDA_setSharedMemory4Send(unsigned int buffer_id,
                         unsigned char *buffer, unsigned int buffer_size,
                         unsigned int packet_size, unsigned int headroom_size)
{
	dhd_info_t *dhd = dhd_global;

	sda_packet_length = packet_size;

	ASSERT(dhd);
	if (dhd == NULL)
		return;
}

void
SDA_registerCallback4SendDone(SDA_SendDoneCallBack packet_cb)
{
	dhd_info_t *dhd = dhd_global;

	ASSERT(dhd);
	if (dhd == NULL)
		return;
}


unsigned long long
SDA_getTsf(unsigned char vif_id)
{
	dhd_info_t *dhd = dhd_global;
	uint64 tsf_val;
	char buf[WLC_IOCTL_SMLEN];
	int ifidx = 0;

	struct tsf {
		uint32 low;
		uint32 high;
	} tsf_buf;

	memset(buf, 0, sizeof(buf));

	if (vif_id == 0) 
		ifidx = dhd_ifname2idx(dhd, "wlan0");
	else if (vif_id == 1) 
		ifidx = dhd_ifname2idx(dhd, "p2p0");

	bcm_mkiovar("tsf_bss", 0, 0, buf, sizeof(buf));

	if (dhd_wl_ioctl_cmd(&dhd->pub, WLC_GET_VAR, buf, sizeof(buf), FALSE, ifidx) < 0) {
		DHD_ERROR(("%s wl ioctl error\n", __FUNCTION__));
		return 0;
	}

	memcpy(&tsf_buf, buf, sizeof(tsf_buf));
	tsf_val = (uint64)tsf_buf.high;
	DHD_TRACE(("%s tsf high 0x%08x, low 0x%08x\n",
	           __FUNCTION__, tsf_buf.high, tsf_buf.low));

	return ((tsf_val << 32) | tsf_buf.low);
}
EXPORT_SYMBOL(SDA_getTsf);

unsigned int
SDA_syncTsf(void)
{
	dhd_info_t *dhd = dhd_global;
	int tsf_sync = 1;
	char iovbuf[WLC_IOCTL_SMLEN];

	bcm_mkiovar("wa_tsf_sync", (char *)&tsf_sync, 4, iovbuf, sizeof(iovbuf));
	dhd_wl_ioctl_cmd(&dhd->pub, WLC_SET_VAR, iovbuf, sizeof(iovbuf), TRUE, 0);

	DHD_TRACE(("%s\n", __FUNCTION__));
	return 0;
}

extern struct net_device *wl0dot1_dev;

void
BCMFASTPATH SDA_function4Send(uint buffer_id, void *packet, uint packet_size)
{
	struct sk_buff *skb;
	sda_packet_t *shm_packet = packet;
	dhd_info_t *dhd = dhd_global;
	int cnt;

	static unsigned int cnt_t = 1;

	ASSERT(dhd);
	if (dhd == NULL)
		return;

	if (dhd->is_wlanaudio_blist) {
		for (cnt = 0; cnt < MAX_WLANAUDIO_BLACKLIST; cnt++) {
			if (dhd->wlanaudio_blist[cnt].is_blacklist == true) {
				if (!bcmp(dhd->wlanaudio_blist[cnt].blacklist_addr.octet,
				          shm_packet->headroom.ether_dhost, ETHER_ADDR_LEN))
					return;
			}
		}
	}

	if ((cnt_t % 10000) == 0)
		cnt_t = 0;

	cnt_t++;

	
#define TXOFF 26
	skb = __dev_alloc_skb(TXOFF + sda_packet_length - SDA_PKT_HEADER_SIZE, GFP_ATOMIC);

	skb_reserve(skb, TXOFF - SDA_HEADROOM_SIZE);
	skb_put(skb, sda_packet_length - SDA_PKT_HEADER_SIZE + SDA_HEADROOM_SIZE);
	skb->priority = PRIO_8021D_VO; 

	
	skb->dev = wl0dot1_dev;
	shm_packet->txTsf = 0x0;
	shm_packet->rxTsf = 0x0;
	memcpy(skb->data, &shm_packet->headroom,
	       sda_packet_length - OFFSETOF(sda_packet_t, headroom));
	shm_packet->desc.ready_to_copy = 0;

	dhd_start_xmit(skb, skb->dev);
}

void
SDA_registerCallback4Recv(unsigned char *pBufferTotal,
                          unsigned int BufferTotalSize)
{
	dhd_info_t *dhd = dhd_global;

	ASSERT(dhd);
	if (dhd == NULL)
		return;
}


void
SDA_setSharedMemory4Recv(unsigned char *pBufferTotal,
                         unsigned int BufferTotalSize,
                         unsigned int BufferUnitSize,
                         unsigned int Headroomsize)
{
	dhd_info_t *dhd = dhd_global;

	ASSERT(dhd);
	if (dhd == NULL)
		return;
}


void
SDA_function4RecvDone(unsigned char * pBuffer, unsigned int BufferSize)
{
	dhd_info_t *dhd = dhd_global;

	ASSERT(dhd);
	if (dhd == NULL)
		return;
}

EXPORT_SYMBOL(SDA_setSharedMemory4Send);
EXPORT_SYMBOL(SDA_registerCallback4SendDone);
EXPORT_SYMBOL(SDA_syncTsf);
EXPORT_SYMBOL(SDA_function4Send);
EXPORT_SYMBOL(SDA_registerCallback4Recv);
EXPORT_SYMBOL(SDA_setSharedMemory4Recv);
EXPORT_SYMBOL(SDA_function4RecvDone);

#endif 
