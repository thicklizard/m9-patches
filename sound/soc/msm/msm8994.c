/* Copyright (c) 2014-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/qpnp/clkdiv.h>
#include <linux/regulator/consumer.h>
#include <linux/io.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/switch.h>
#include <sound/core.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <sound/jack.h>
#include <sound/q6afe-v2.h>
#include <sound/q6core.h>
#include <sound/pcm_params.h>
#include <soc/qcom/liquid_dock.h>
#include "device_event.h"
#include "qdsp6v2/msm-pcm-routing-v2.h"
#include "../codecs/wcd9xxx-common.h"
#include "../codecs/wcd9330.h"

//htc audio ++
#include <sound/q6adm-v2.h>
#include <sound/htc_acoustic_alsa.h>
#include <linux/of_device.h>
#include <linux/qpnp/qpnp-adc.h>

#undef pr_info
#undef pr_err
#define pr_info(fmt, ...) pr_aud_info(fmt, ##__VA_ARGS__)
#define pr_err(fmt, ...) pr_aud_err(fmt, ##__VA_ARGS__)
//htc audio --
#define DRV_NAME "msm8994-asoc-snd"

#define MSM8994_BRINGUP	(0)	/* htc audio, set only for bring up */
#define MSM8994_LDO_WR	(0)	/* htc audio, to fix LDO29 cause i2c bus error */
				/* This macro also defined in rt5506 and tfa9887 drivers!! */
#define SAMPLING_RATE_8KHZ      8000
#define SAMPLING_RATE_16KHZ     16000
#define SAMPLING_RATE_32KHZ     32000
#define SAMPLING_RATE_48KHZ     48000
#define SAMPLING_RATE_96KHZ     96000
#define SAMPLING_RATE_192KHZ    192000

#define MSM8994_SPK_ON     1
#define MSM8994_SPK_OFF    0

#define MSM_SLIM_0_RX_MAX_CHANNELS    2
#define MSM_SLIM_0_TX_MAX_CHANNELS    4
//htc audio ++
#define LO_1_SPK_AMP    0x1
#define LO_3_SPK_AMP    0x2
#define LO_2_SPK_AMP    0x4
#define LO_4_SPK_AMP    0x8
//htc audio --
#define I2S_PCM_SEL_PCM       1
#define I2S_PCM_SEL_I2S       0
#define I2S_PCM_SEL_OFFSET    1

#define WCD9XXX_MBHC_DEF_BUTTONS    8
#define WCD9XXX_MBHC_DEF_RLOADS     5
#define TOMTOM_EXT_CLK_RATE         9600000
#define ADSP_STATE_READY_TIMEOUT_MS    3000

#define MSM_TERT_MI2S_MASTER
#define MSM_QUAT_MI2S_MASTER

enum pinctrl_pin_state {
	STATE_DISABLE = 0,
	STATE_ON = 1
};

enum mi2s_pcm_mux {
	PRI_MI2S_PCM = 0,
	SEC_MI2S_PCM,
	TERT_MI2S_PCM,
	QUAT_MI2S_PCM
};
//htc audio ++
enum wcd_mux {
	WCD_CLK2 = 0
};

enum pinctrl_type {
	MI2S_PIN_TYPE = 0,
	AUXPCM_PIN_TYPE,
	WCD_PIN_TYPE,
};
//htc audio --
struct msm_pinctrl_info {
	struct pinctrl *pinctrl;
	struct pinctrl_state *disable;
	struct pinctrl_state *active;
	enum pinctrl_pin_state curr_state;
	void __iomem *mux;
};

struct msm8994_asoc_mach_data {
#if MSM8994_BRINGUP
	int hset_bias_gpio;
	int led_gpio;
#endif
#if MSM8994_LDO_WR
	int hph_en_gpio;
#endif
	int mclk_gpio;
	u32 mclk_freq;
	int us_euro_gpio;
	struct msm_pinctrl_info sec_auxpcm_pinctrl_info;
	struct msm_pinctrl_info pri_mi2s_pinctrl_info;
	struct msm_pinctrl_info sec_mi2s_pinctrl_info;
	struct msm_pinctrl_info tert_mi2s_pinctrl_info;
	struct msm_pinctrl_info quat_mi2s_pinctrl_info;
//htc audio ++
	struct msm_pinctrl_info wcd_clk2_pinctrl_info;
	struct msm_pinctrl_info ftm_sec_auxpcm_pinctrl_info;
//htc audio --
};
//htc audio ++
struct htc_request_gpio {
	unsigned gpio_no;
	const char* gpio_name;
};
static struct rcv_config {
	int init;
	struct htc_request_gpio gpio[2];
} htc_rcv_config = {

	.init = 0,
	.gpio = {
			{ .gpio_name = "rcv-gpio-sel",},
			{ .gpio_name = "rcv-gpio-en",},
	},
};
static struct reserved_gpio_config {
	int init;
	struct htc_request_gpio gpio[1];
} htc_reserved_gpio_config = {

	.init = 0,
	.gpio = {
			{ .gpio_name = "aud-reserved",},
	},
};
#if 0
static struct hset_amp_config {
	int init;
	struct htc_request_gpio gpio[1];
} htc_hset_amp_config = {

	.init = 0,
	.gpio = {
			{ .gpio_name = "hset-amp-gpio",},
	},
};
#endif

static struct aud_btpcm_config {
	int init;
	enum AUD_FTM_BTPCM_MODE mode;
	struct msm_pinctrl_info *pinctrl_info;
	struct htc_request_gpio gpio[4];
} htc_aud_btpcm_config = {
	.init = 0,
	.mode = AUD_FTM_BTPCM_MODE_PCM,
	.pinctrl_info = NULL,
	.gpio = {
		{ .gpio_name = "ftm-btpcm-sync",},	/* frame sync */
		{ .gpio_name = "ftm-btpcm-clock",},	/* bit clock */
		{ .gpio_name = "ftm-btpcm-dout",},	/* BT to MSM */
		{ .gpio_name = "ftm-btpcm-din",},	/* MSM to BT */
	},
};

#if MSM8994_LDO_WR
static int hph_en_ready = 0;
#endif
//htc audio --

static int slim0_rx_sample_rate = SAMPLING_RATE_48KHZ;
static int slim0_tx_sample_rate = SAMPLING_RATE_48KHZ;
static int slim0_rx_bit_format = SNDRV_PCM_FORMAT_S16_LE;
static int slim0_tx_bit_format = SNDRV_PCM_FORMAT_S16_LE;
// htc audio ++
//static int hdmi_rx_bit_format = SNDRV_PCM_FORMAT_S16_LE;
static int hdmi_rx_bit_format = SNDRV_PCM_FORMAT_S24_LE;
// htc audio --
static int msm8994_auxpcm_rate = 8000;

static struct platform_device *spdev;
static int ext_us_amp_gpio = -1;
static int msm8994_spk_control = 1;
static int msm_slim_0_rx_ch = 1;
static int msm_slim_0_tx_ch = 1;
static int msm_vi_feed_tx_ch = 2;

static int msm_btsco_rate = SAMPLING_RATE_8KHZ;
static int msm_hdmi_rx_ch = 2;
static int msm_proxy_rx_ch = 2;
static int hdmi_rx_sample_rate = SAMPLING_RATE_48KHZ;
static int msm_pri_mi2s_tx_ch = 2;

static struct mutex cdc_mclk_mutex;
static struct clk *codec_clk;
static int clk_users;
static atomic_t prim_auxpcm_rsc_ref;
static atomic_t sec_auxpcm_rsc_ref;

struct audio_plug_dev {
	int plug_gpio;
	int plug_irq;
	int plug_det;
	struct work_struct irq_work;
	struct switch_dev audio_sdev;
};

static struct audio_plug_dev *msm8994_liquid_dock_dev;

/* Rear panel audio jack which connected to LO1&2 */
static struct audio_plug_dev *apq8094_db_ext_bp_out_dev;
/* Front panel audio jack which connected to LO3&4 */
static struct audio_plug_dev *apq8094_db_ext_fp_in_dev;
/* Front panel audio jack which connected to Line in (ADC6) */
static struct audio_plug_dev *apq8094_db_ext_fp_out_dev;


static const char *const pin_states[] = {"Disable", "active"};
static const char *const spk_function[] = {"Off", "On"};
static const char *const slim0_rx_ch_text[] = {"One", "Two"};
static const char *const vi_feed_ch_text[] = {"One", "Two"};
static const char *const slim0_tx_ch_text[] = {"One", "Two", "Three", "Four",
						"Five", "Six", "Seven",
						"Eight"};
static char const *hdmi_rx_ch_text[] = {"Two", "Three", "Four", "Five",
					"Six", "Seven", "Eight"};
static char const *rx_bit_format_text[] = {"S16_LE", "S24_LE", "S24_3LE"};
static char const *slim0_rx_sample_rate_text[] = {"KHZ_48", "KHZ_96",
					"KHZ_192"};
static const char *const proxy_rx_ch_text[] = {"One", "Two", "Three", "Four",
	"Five",	"Six", "Seven", "Eight"};

static char const *hdmi_rx_sample_rate_text[] = {"KHZ_48", "KHZ_96",
					"KHZ_192"};
static const char *const btsco_rate_text[] = {"8000", "16000"};
static const struct soc_enum msm_btsco_enum[] = {
	SOC_ENUM_SINGLE_EXT(2, btsco_rate_text),
};

static const char *const auxpcm_rate_text[] = {"8000", "16000"};
static const struct soc_enum msm8994_auxpcm_enum[] = {
		SOC_ENUM_SINGLE_EXT(2, auxpcm_rate_text),
};

static void *adsp_state_notifier;
//htc audio ++
#ifdef CONFIG_USE_CODEC_MBHC
static void *def_codec_mbhc_cal(void);
#endif
//htc audio --
static int msm_snd_enable_codec_ext_clk(struct snd_soc_codec *codec,
					int enable, bool dapm);
//htc audio ++
#ifdef CONFIG_USE_CODEC_MBHC
static struct wcd9xxx_mbhc_config mbhc_cfg = {
	.read_fw_bin = false,
	.calibration = NULL,
	.micbias = MBHC_MICBIAS2,
	.anc_micbias = MBHC_MICBIAS2,
	.mclk_cb_fn = msm_snd_enable_codec_ext_clk,
	.mclk_rate = TOMTOM_EXT_CLK_RATE,
	.gpio_level_insert = 0,
	.detect_extn_cable = true,
	.micbias_enable_flags = 1 << MBHC_MICBIAS_ENABLE_THRESHOLD_HEADSET,
	.insert_detect = true,
	.swap_gnd_mic = NULL,
	.cs_enable_flags = (1 << MBHC_CS_ENABLE_POLLING |
			    1 << MBHC_CS_ENABLE_INSERTION |
			    1 << MBHC_CS_ENABLE_REMOVAL |
			    1 << MBHC_CS_ENABLE_DET_ANC),
	.do_recalibration = true,
	.use_vddio_meas = true,
	.enable_anc_mic_detect = false,
	.hw_jack_type = SIX_POLE_JACK,
	.key_code[0] = KEY_MEDIA,
	.key_code[1] = KEY_VOICECOMMAND,
	.key_code[2] = KEY_VOLUMEUP,
	.key_code[3] = KEY_VOLUMEDOWN,
	.key_code[4] = 0,
	.key_code[5] = 0,
	.key_code[6] = 0,
	.key_code[7] = 0,
	.htc_headset_cfg.htc_headset_init = false,
	.htc_headset_cfg.IsXaMotherboard = false,
};
#endif
//htc audio --
static struct afe_clk_cfg mi2s_tx_clk = {
	AFE_API_VERSION_I2S_CONFIG,
	Q6AFE_LPASS_IBIT_CLK_1_P536_MHZ,
	Q6AFE_LPASS_OSR_CLK_DISABLE,
	Q6AFE_LPASS_CLK_SRC_INTERNAL,
	Q6AFE_LPASS_CLK_ROOT_DEFAULT,
	Q6AFE_LPASS_MODE_CLK1_VALID,
	0,
};

static struct afe_clk_cfg tert_mi2s_clk = {
	AFE_API_VERSION_I2S_CONFIG,
	Q6AFE_LPASS_IBIT_CLK_1_P536_MHZ,
	Q6AFE_LPASS_OSR_CLK_DISABLE,
	Q6AFE_LPASS_CLK_SRC_INTERNAL,
	Q6AFE_LPASS_CLK_ROOT_DEFAULT,
	Q6AFE_LPASS_MODE_CLK1_VALID,
	0,
};
static struct afe_clk_cfg quat_mi2s_clk = {
	AFE_API_VERSION_I2S_CONFIG,
	Q6AFE_LPASS_IBIT_CLK_1_P536_MHZ,
	Q6AFE_LPASS_OSR_CLK_DISABLE,
	Q6AFE_LPASS_CLK_SRC_INTERNAL,
	Q6AFE_LPASS_CLK_ROOT_DEFAULT,
	Q6AFE_LPASS_MODE_CLK1_VALID,
	0,
};

static atomic_t tert_mi2s_rsc_ref, quat_mi2s_rsc_ref;
static int tert_mi2s_sample_rate = SAMPLING_RATE_48KHZ;
static int quat_mi2s_sample_rate = SAMPLING_RATE_48KHZ;
static int tert_mi2s_bit_format = SNDRV_PCM_FORMAT_S16_LE;
//static int quat_mi2s_bit_format = SNDRV_PCM_FORMAT_S16_LE;
static int quat_mi2s_bit_format = SNDRV_PCM_FORMAT_S24_LE; //htc audio

//htc audio ++
#define msm8994_BRING_UP_OPTION (0)
#define HTC_HS_AMP  0x1
#define HTC_RCV_AMP 0x2
static int htc_amp_mask = 0;
static int hs_amp_on = 0;
static int rcv_amp_on = 0;
static struct mutex htc_amp_mutex;
static int msm8994_ext_spk_pamp;
static struct mutex htc_audiosphere_enable_mutex;
static struct mutex htc_audiosphere_strength_mutex;


static atomic_t q6_effect_mode = ATOMIC_INIT(-1);
static atomic_t dev_use_dmic = ATOMIC_INIT(0);

static int msm8994_liquid_ext_spk_power_amp_on(u32 spk);
static void msm8994_liquid_ext_spk_power_amp_off(u32 spk);

static int msm8994_get_hw_component(void)
{
	if (of_property_read_bool(spdev->dev.of_node, "acadia-tfa9891-support"))
		return 0;
	else
		return (HTC_AUDIO_RT5501 | HTC_AUDIO_TFA9887 | HTC_AUDIO_TFA9887L);
}

char* msm8994_get_mid(void)
{
#if msm8994_BRING_UP_OPTION
	char* mid = board_mid();
	int i;
	pr_info("mid=%s", mid);
	for (i = 0; i < ARRAY_SIZE(MID_LIST); i++)
	{
	    if(!strncmp(mid, MID_LIST[i][0],7)){
	        pr_info("match mid %s at %d", mid, i);
	        break;
	    }
	}
	if (i == ARRAY_SIZE(MID_LIST)){
		pr_info("cannot match SKU table, use Generic setting.");
		return "Generic";
	}
	else
		return MID_LIST[i][1];
#else
	return "Generic";
#endif
}

static int msm8994_enable_digital_mic(void)
{
	return atomic_read(&dev_use_dmic);
}

int msm8994_enable_24b_audio(void)
{
	return 1;
}

void msm8994_set_q6_effect_mode(int mode)
{
	pr_debug("%s: mode %d\n", __func__, mode);
	atomic_set(&q6_effect_mode, mode);
}

int msm8994_get_q6_effect_mode(void)
{
	int mode = atomic_read(&q6_effect_mode);
	pr_debug("%s: mode %d\n", __func__, mode);
	return mode;
}

static struct acoustic_ops acoustic = {
	.get_mid = msm8994_get_mid,
	.enable_digital_mic = msm8994_enable_digital_mic,
	.get_hw_component = msm8994_get_hw_component,
	.set_q6_effect = msm8994_set_q6_effect_mode,
	.get_q6_effect = msm8994_get_q6_effect_mode,
	.enable_24b_audio = msm8994_enable_24b_audio
};

//htc audio --

static inline int param_is_mask(int p)
{
	return ((p >= SNDRV_PCM_HW_PARAM_FIRST_MASK) &&
			(p <= SNDRV_PCM_HW_PARAM_LAST_MASK));
}

static inline struct snd_mask *param_to_mask(struct snd_pcm_hw_params *p, int n)
{
	return &(p->masks[n - SNDRV_PCM_HW_PARAM_FIRST_MASK]);
}

static void param_set_mask(struct snd_pcm_hw_params *p, int n, unsigned bit)
{
	if (bit >= SNDRV_MASK_MAX)
		return;
	if (param_is_mask(n)) {
		struct snd_mask *m = param_to_mask(p, n);
		m->bits[0] = 0;
		m->bits[1] = 0;
		m->bits[bit >> 5] |= (1 << (bit & 31));
	}
}

static irqreturn_t msm8994_audio_plug_device_irq_handler(int irq, void *dev)
{
	struct audio_plug_dev *dock_dev = dev;

	/* switch speakers should not run in interrupt context */
	schedule_work(&dock_dev->irq_work);
	return IRQ_HANDLED;
}

static int msm8994_audio_plug_device_init
				(struct audio_plug_dev *audio_plug_dev,
				 int plug_gpio,
				 char *node_name,
				 char *switch_dev_name,
				 void (*irq_work)(struct work_struct *work))
{
	int ret = 0;

	/* awaike by plug in device OR unplug one of them */
	u32 plug_irq_flags = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING;

	audio_plug_dev =
		kzalloc(sizeof(*audio_plug_dev), GFP_KERNEL);
	if (!audio_plug_dev) {
		pr_err("audio_plug_device alloc fail. dev_name= %s\n",
					node_name);
		ret = -ENOMEM;
		goto exit;
	}

	audio_plug_dev->plug_gpio = plug_gpio;

	ret = gpio_request(audio_plug_dev->plug_gpio,
					   node_name);
	if (ret) {
		pr_err("%s:failed request audio_plug_device device_name = %s err= 0x%x\n",
			__func__, node_name, ret);
		ret = -EINVAL;
		goto release_audio_plug_device;
	}

	ret = gpio_direction_input(audio_plug_dev->plug_gpio);

	audio_plug_dev->plug_det =
			gpio_get_value(audio_plug_dev->plug_gpio);
	audio_plug_dev->plug_irq =
		gpio_to_irq(audio_plug_dev->plug_gpio);

	ret = request_irq(audio_plug_dev->plug_irq,
				msm8994_audio_plug_device_irq_handler,
				plug_irq_flags,
				node_name,
				audio_plug_dev);
	if (ret < 0) {
		pr_err("%s: Request Irq Failed err = %d\n",
				__func__, ret);
		goto fail_gpio;
	}

	audio_plug_dev->audio_sdev.name = switch_dev_name;
	if (switch_dev_register(
		&audio_plug_dev->audio_sdev) < 0) {
		pr_err("%s: audio plug device register in switch diretory failed\n",
		__func__);
		goto fail_switch_dev;
	}

	switch_set_state(&audio_plug_dev->audio_sdev,
					!(audio_plug_dev->plug_det));

	/*notify to audio deamon*/
	sysfs_notify(&audio_plug_dev->audio_sdev.dev->kobj,
				NULL, "state");

	INIT_WORK(&audio_plug_dev->irq_work,
			irq_work);

	return 0;

fail_switch_dev:
	free_irq(audio_plug_dev->plug_irq,
				audio_plug_dev);
fail_gpio:
	gpio_free(audio_plug_dev->plug_gpio);
release_audio_plug_device:
	kfree(audio_plug_dev);
exit:
	audio_plug_dev = NULL;
	return ret;

}

static void msm8994_audio_plug_device_remove
				(struct audio_plug_dev *audio_plug_dev)
{
	if (audio_plug_dev != NULL) {
		switch_dev_unregister(&audio_plug_dev->audio_sdev);

		if (audio_plug_dev->plug_irq)
			free_irq(audio_plug_dev->plug_irq,
				 audio_plug_dev);

		if (audio_plug_dev->plug_gpio)
			gpio_free(audio_plug_dev->plug_gpio);

		kfree(audio_plug_dev);
		audio_plug_dev = NULL;
	}
}

static void msm8994_liquid_docking_irq_work(struct work_struct *work)
{
	struct audio_plug_dev *dock_dev =
		container_of(work, struct audio_plug_dev, irq_work);

	dock_dev->plug_det =
		gpio_get_value(dock_dev->plug_gpio);

	switch_set_state(&dock_dev->audio_sdev, dock_dev->plug_det);
	/*notify to audio deamon*/
	sysfs_notify(&dock_dev->audio_sdev.dev->kobj, NULL, "state");
}

static int msm8994_liquid_dock_notify_handler(struct notifier_block *this,
					unsigned long dock_event,
					void *unused)
{
	int ret = 0;
	/* plug in docking speaker+plug in device OR unplug one of them */
	u32 dock_plug_irq_flags = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING |
					IRQF_SHARED;
	if (dock_event) {
		ret = gpio_request(msm8994_liquid_dock_dev->plug_gpio,
					   "dock-plug-det-irq");
		if (ret) {
			pr_err("%s:failed request msm8994_liquid_plug_gpio err = %d\n",
				__func__, ret);
			ret = -EINVAL;
			goto fail_dock_gpio;
		}

		msm8994_liquid_dock_dev->plug_det =
			gpio_get_value(msm8994_liquid_dock_dev->plug_gpio);
		msm8994_liquid_dock_dev->plug_irq =
			gpio_to_irq(msm8994_liquid_dock_dev->plug_gpio);

		ret = request_irq(msm8994_liquid_dock_dev->plug_irq,
				  msm8994_audio_plug_device_irq_handler,
				  dock_plug_irq_flags,
				  "liquid_dock_plug_irq",
				  msm8994_liquid_dock_dev);
		if (ret < 0) {
			pr_err("%s: Request Irq Failed err = %d\n",
				__func__, ret);
			goto fail_gpio_irq;
		}

		switch_set_state(&msm8994_liquid_dock_dev->audio_sdev,
				msm8994_liquid_dock_dev->plug_det);
		/*notify to audio deamon*/
		sysfs_notify(&msm8994_liquid_dock_dev->audio_sdev.dev->kobj,
				NULL, "state");
	} else {
		if (msm8994_liquid_dock_dev->plug_irq)
			free_irq(msm8994_liquid_dock_dev->plug_irq,
				 msm8994_liquid_dock_dev);

		if (msm8994_liquid_dock_dev->plug_gpio)
			gpio_free(msm8994_liquid_dock_dev->plug_gpio);
	}
	return NOTIFY_OK;

fail_gpio_irq:
	gpio_free(msm8994_liquid_dock_dev->plug_gpio);

fail_dock_gpio:
	return NOTIFY_DONE;
}


static struct notifier_block msm8994_liquid_docking_notifier = {
	.notifier_call  = msm8994_liquid_dock_notify_handler,
};

static int msm8994_liquid_init_docking(void)
{
	int ret = 0;
	int plug_gpio = 0;

	plug_gpio = of_get_named_gpio(spdev->dev.of_node,
					   "qcom,dock-plug-det-irq", 0);

	if (plug_gpio >= 0) {
		msm8994_liquid_dock_dev =
		 kzalloc(sizeof(*msm8994_liquid_dock_dev), GFP_KERNEL);
		if (!msm8994_liquid_dock_dev) {
			pr_err("msm8994_liquid_dock_dev alloc fail.\n");
			ret = -ENOMEM;
			goto exit;
		}

		msm8994_liquid_dock_dev->plug_gpio = plug_gpio;
		msm8994_liquid_dock_dev->audio_sdev.name =
						QC_AUDIO_EXTERNAL_SPK_1_EVENT;

		if (switch_dev_register(
			 &msm8994_liquid_dock_dev->audio_sdev) < 0) {
			pr_err("%s: dock device register in switch diretory failed\n",
				__func__);
			goto exit;
		}

		INIT_WORK(
			&msm8994_liquid_dock_dev->irq_work,
			msm8994_liquid_docking_irq_work);

		register_liquid_dock_notify(&msm8994_liquid_docking_notifier);
	}
	return 0;
exit:
	msm8994_liquid_dock_dev = NULL;
	return ret;
}

static void apq8094_db_device_irq_work(struct work_struct *work)
{
	struct audio_plug_dev *plug_dev =
		container_of(work, struct audio_plug_dev, irq_work);

	plug_dev->plug_det =
		gpio_get_value(plug_dev->plug_gpio);

	switch_set_state(&plug_dev->audio_sdev, !(plug_dev->plug_det));

	/*notify to audio deamon*/
	sysfs_notify(&plug_dev->audio_sdev.dev->kobj, NULL, "state");
}

static int apq8094_db_device_init(void)
{
	int ret = 0;
	int plug_gpio = 0;


	plug_gpio = of_get_named_gpio(spdev->dev.of_node,
					   "qcom,ext-spk-rear-panel-irq", 0);
	if (plug_gpio >= 0) {
		ret = msm8994_audio_plug_device_init(apq8094_db_ext_bp_out_dev,
						plug_gpio,
						"qcom,ext-spk-rear-panel-irq",
						QC_AUDIO_EXTERNAL_SPK_2_EVENT,
						&apq8094_db_device_irq_work);
		if (ret != 0) {
			pr_err("%s: external audio device init failed = %s\n",
				__func__,
				apq8094_db_ext_bp_out_dev->audio_sdev.name);
			return ret;
		}
	}

	plug_gpio = of_get_named_gpio(spdev->dev.of_node,
					   "qcom,ext-spk-front-panel-irq", 0);
	if (plug_gpio >= 0) {
		ret = msm8994_audio_plug_device_init(apq8094_db_ext_fp_out_dev,
						plug_gpio,
						"qcom,ext-spk-front-panel-irq",
						QC_AUDIO_EXTERNAL_SPK_1_EVENT,
						&apq8094_db_device_irq_work);
		if (ret != 0) {
			pr_err("%s: external audio device init failed = %s\n",
				__func__,
				apq8094_db_ext_fp_out_dev->audio_sdev.name);
			return ret;
		}
	}

	plug_gpio = of_get_named_gpio(spdev->dev.of_node,
					   "qcom,ext-mic-front-panel-irq", 0);
	if (plug_gpio >= 0) {
		ret = msm8994_audio_plug_device_init(apq8094_db_ext_fp_in_dev,
						plug_gpio,
						"qcom,ext-mic-front-panel-irq",
						QC_AUDIO_EXTERNAL_MIC_EVENT,
						&apq8094_db_device_irq_work);
		if (ret != 0)
			pr_err("%s: external audio device init failed = %s\n",
				__func__,
				apq8094_db_ext_fp_in_dev->audio_sdev.name);
	}

	return ret;
}

static void msm8994_ext_control(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	mutex_lock(&dapm->codec->mutex);
	pr_debug("%s: msm8994_spk_control = %d", __func__, msm8994_spk_control);
	if (msm8994_spk_control == MSM8994_SPK_ON) {
		snd_soc_dapm_enable_pin(dapm, "Lineout_1 amp");
		snd_soc_dapm_enable_pin(dapm, "Lineout_2 amp");
	} else {
		snd_soc_dapm_disable_pin(dapm, "Lineout_1 amp");
		snd_soc_dapm_disable_pin(dapm, "Lineout_2 amp");
	}
	mutex_unlock(&dapm->codec->mutex);
	snd_soc_dapm_sync(dapm);
}

static int msm8994_get_spk(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: msm8994_spk_control = %d\n",
			 __func__, msm8994_spk_control);
	ucontrol->value.integer.value[0] = msm8994_spk_control;
	return 0;
}

static int msm8994_set_spk(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);

	pr_debug("%s()\n", __func__);
	if (msm8994_spk_control == ucontrol->value.integer.value[0])
		return 0;

	msm8994_spk_control = ucontrol->value.integer.value[0];
	msm8994_ext_control(codec);
	return 1;
}

static int msm8994_ext_us_amp_init(void)
{
	int ret = 0;

	ext_us_amp_gpio = of_get_named_gpio(spdev->dev.of_node,
				"qcom,ext-ult-spk-amp-gpio", 0);
	if (ext_us_amp_gpio >= 0) {
		ret = gpio_request(ext_us_amp_gpio, "ext_us_amp_gpio");
		if (ret) {
			pr_err("%s: ext_us_amp_gpio request failed, ret:%d\n",
				__func__, ret);
			return ret;
		}
		gpio_direction_output(ext_us_amp_gpio, 0);
	}
	return ret;
}

static void msm8994_ext_us_amp_enable(u32 on)
{
	if (on)
		gpio_direction_output(ext_us_amp_gpio, 1);
	else
		gpio_direction_output(ext_us_amp_gpio, 0);

	pr_debug("%s: US Emitter GPIO enable:%s\n", __func__,
			on ? "Enable" : "Disable");
}

//htc audio ++
static int msm_ext_spkramp_event(struct snd_soc_dapm_widget *w,
				 struct snd_kcontrol *k, int event)
{
	int ret = 0;

	pr_debug("%s()\n", __func__);
	if (SND_SOC_DAPM_EVENT_ON(event)) {
		if (!strcmp(w->name, "Lineout_1 amp"))
			msm8994_liquid_ext_spk_power_amp_on(LO_1_SPK_AMP);
		else if (!strcmp(w->name, "Lineout_3 amp"))
			msm8994_liquid_ext_spk_power_amp_on(LO_3_SPK_AMP);
		else if (!strcmp(w->name, "Lineout_2 amp"))
			msm8994_liquid_ext_spk_power_amp_on(LO_2_SPK_AMP);
		else if  (!strcmp(w->name, "Lineout_4 amp"))
			msm8994_liquid_ext_spk_power_amp_on(LO_4_SPK_AMP);
		else {
			pr_err("%s() Invalid Speaker Widget = %s\n",
					__func__, w->name);
			ret = -EINVAL;
		}
	} else {
		if (!strcmp(w->name, "Lineout_1 amp"))
			msm8994_liquid_ext_spk_power_amp_off(LO_1_SPK_AMP);
		else if (!strcmp(w->name, "Lineout_3 amp"))
			msm8994_liquid_ext_spk_power_amp_off(LO_3_SPK_AMP);
		else if (!strcmp(w->name, "Lineout_2 amp"))
			msm8994_liquid_ext_spk_power_amp_off(LO_2_SPK_AMP);
		else if  (!strcmp(w->name, "Lineout_4 amp"))
			msm8994_liquid_ext_spk_power_amp_off(LO_4_SPK_AMP);
		else {
			pr_err("%s() Invalid Speaker Widget = %s\n",
					__func__, w->name);
			ret = -EINVAL;
		}
	}
	return ret;
}
//htc audio --

static int msm_ext_ultrasound_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *k, int event)
{
	pr_debug("%s()\n", __func__);
	if (!strcmp(w->name, "ultrasound amp")) {
		if (!gpio_is_valid(ext_us_amp_gpio)) {
			pr_err("%s: ext_us_amp_gpio isn't configured\n",
				__func__);
			return -EINVAL;
		}
		if (SND_SOC_DAPM_EVENT_ON(event))
			msm8994_ext_us_amp_enable(1);
		else
			msm8994_ext_us_amp_enable(0);
	} else {
		pr_err("%s() Invalid Widget = %s\n",
				__func__, w->name);
		return -EINVAL;
	}
	return 0;
}
//htc audio ++
#if 0
static void htc_hset_amp_ctl(int enable)
{
	int i, value = (enable)?1:0;

	if(!htc_hset_amp_config.init)
		return;

	for(i = 0; i < ARRAY_SIZE(htc_hset_amp_config.gpio); i++) {

		pr_info("%s: gpio = %d, gpio name = %s value %d\n", __func__,
		htc_hset_amp_config.gpio[i].gpio_no, htc_hset_amp_config.gpio[i].gpio_name,value);

		gpio_set_value(htc_hset_amp_config.gpio[i].gpio_no, value);
	}

}
#endif
static void htc_rcv_amp_ctl(int enable)
{
	int i, value = (enable)?1:0;

	if(!htc_rcv_config.init)
		return;

	for(i = 0; i < ARRAY_SIZE(htc_rcv_config.gpio); i++) {

		if(gpio_is_valid(htc_rcv_config.gpio[i].gpio_no))
		{
			pr_debug("%s: gpio = %d, gpio name = %s value %d\n", __func__,
			htc_rcv_config.gpio[i].gpio_no, htc_rcv_config.gpio[i].gpio_name,value);

			gpio_set_value(htc_rcv_config.gpio[i].gpio_no, value);
		}
	}

}

static void htc_amp_control(int amp_mask, int lineout_mask)
{
	if((lineout_mask & LO_1_SPK_AMP) && (lineout_mask & LO_3_SPK_AMP)) {
		if((lineout_mask & LO_2_SPK_AMP) && (lineout_mask & LO_4_SPK_AMP)) {
			if((amp_mask & HTC_HS_AMP) && hs_amp_on == 0) {
				pr_info("headphone amp on\n");
				hs_amp_on = 1;
				htc_acoustic_hs_amp_ctrl(1, 0);
			} else if (!(amp_mask & HTC_HS_AMP) && hs_amp_on == 1) {

				pr_info("headphone amp off\n");
				hs_amp_on = 0;
				htc_acoustic_hs_amp_ctrl(0, 0);
			}
		} else if(hs_amp_on == 1) {
			pr_info("headphone amp off\n");
			hs_amp_on = 0;
			htc_acoustic_hs_amp_ctrl(0, 0);
		}


		if((amp_mask & HTC_RCV_AMP) && rcv_amp_on == 0) {
			pr_info("receiver amp on\n");
			htc_rcv_amp_ctl(1);
			rcv_amp_on = 1;
		} else if(!(amp_mask & HTC_RCV_AMP) && rcv_amp_on == 1) {
			pr_info("receiver amp off\n");
			htc_rcv_amp_ctl(0);
			rcv_amp_on = 0;
		}

	} else {
		if(hs_amp_on == 1) {
			pr_info("headphone amp off\n");
			hs_amp_on = 0;
			htc_acoustic_hs_amp_ctrl(0, 0);
		}

		if(rcv_amp_on == 1) {
			pr_info("receiver amp off\n");
			htc_rcv_amp_ctl(0);
			rcv_amp_on = 0;
		}

	}

}


static int msm8994_liquid_ext_spk_power_amp_on(u32 spk)
{
	int rc = 0;

	if (spk & (LO_1_SPK_AMP | LO_3_SPK_AMP | LO_2_SPK_AMP | LO_4_SPK_AMP)) {
		pr_debug("%s: External speakers are already on. spk = 0x%x\n",
			__func__, spk);
		mutex_lock(&htc_amp_mutex);
		msm8994_ext_spk_pamp |= spk;
		htc_amp_control(htc_amp_mask,msm8994_ext_spk_pamp);
		mutex_unlock(&htc_amp_mutex);
	} else  {
		pr_err("%s: Invalid external speaker ampl. spk = 0x%x\n",
			__func__, spk);
		rc = -EINVAL;
	}
	return rc;
}

static void msm8994_liquid_ext_spk_power_amp_off(u32 spk)
{
	if (spk & (LO_1_SPK_AMP |
		   LO_3_SPK_AMP |
		   LO_2_SPK_AMP |
		   LO_4_SPK_AMP)) {

		pr_debug("%s Left and right speakers case spk = 0x%08x",
			__func__, spk);
		if(!msm8994_ext_spk_pamp)
			return;

		mutex_lock(&htc_amp_mutex);
		msm8994_ext_spk_pamp &= ~spk;
		htc_amp_control(htc_amp_mask,msm8994_ext_spk_pamp);
		mutex_unlock(&htc_amp_mutex);
	} else  {
		pr_err("%s: ERROR : Invalid Ext Spk Ampl. spk = 0x%08x\n",
			__func__, spk);
	}
}
//htc audio --
static int msm_snd_enable_codec_ext_clk(struct snd_soc_codec *codec, int enable,
					bool dapm)
{
	int ret = 0;
	pr_debug("%s: enable = %d clk_users = %d\n",
		__func__, enable, clk_users);

	mutex_lock(&cdc_mclk_mutex);
	if (enable) {
		if (!codec_clk) {
			dev_err(codec->dev, "%s: did not get codec MCLK\n",
				__func__);
			ret = -EINVAL;
			goto exit;
		}
		clk_users++;
		if (clk_users != 1)
			goto exit;

		ret = clk_prepare_enable(codec_clk);
		if (ret) {
			pr_err("%s: clk_prepare failed, err:%d\n",
				__func__, ret);
			clk_users--;
			goto exit;
		}
		tomtom_mclk_enable(codec, 1, dapm);
	} else {
		if (clk_users > 0) {
			clk_users--;
			if (clk_users == 0) {
				tomtom_mclk_enable(codec, 0, dapm);
				clk_disable_unprepare(codec_clk);
			}
		} else {
			pr_err("%s: Error releasing codec MCLK\n", __func__);
			ret = -EINVAL;
			goto exit;
		}
	}
exit:
	mutex_unlock(&cdc_mclk_mutex);
	return ret;
}

static int msm8994_mclk_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	pr_debug("%s: event = %d\n", __func__, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		return msm_snd_enable_codec_ext_clk(w->codec, 1, true);
	case SND_SOC_DAPM_POST_PMD:
		return msm_snd_enable_codec_ext_clk(w->codec, 0, true);
	}
	return 0;
}
//htc audio ++
#ifdef CONFIG_USE_CODEC_MBHC
static int hs_qpnp_remote_adc(int *adc,unsigned int channel)
{
	struct qpnp_vadc_result result;
	enum qpnp_vadc_channels chan;
	static struct qpnp_vadc_chip *vadc_chip;
	int err = 0;

	vadc_chip = qpnp_get_vadc(&spdev->dev, "headset");

	result.physical = -EINVAL;
	chan = channel;
	pr_debug("%s: pdata_chann %d\n", __func__, chan);
	err = qpnp_vadc_read(vadc_chip, chan, &result);
	if (err < 0) {
		pr_err("%s: Read ADC fail, ret = %d\n", __func__, err);
		return err;
	}

	*adc = (int) result.physical;
	*adc = *adc / 1000; /* uA to mA */
	pr_info("%s: Remote ADC %d (%#X)\n", __func__, *adc, *adc);
	return 1;
}

static int msm_headset_lr_event(struct snd_soc_dapm_widget *w,
				 struct snd_kcontrol *k, int event)
{
	int ret = 0;
	pr_info("%s\n", __func__);
	if (SND_SOC_DAPM_EVENT_ON(event)) {
		if (gpio_get_value(mbhc_cfg.htc_headset_cfg.ext_micbias) != 1) {
			gpio_set_value(mbhc_cfg.htc_headset_cfg.ext_micbias,1);
			pr_info("%s: ext_micbias on\n",__func__);
		}
	} else {
		if (gpio_get_value(mbhc_cfg.htc_headset_cfg.ext_micbias) != 0) {
			gpio_set_value(mbhc_cfg.htc_headset_cfg.ext_micbias,0);
			pr_info("%s: ext_micbias off\n",__func__);
		}
	}
	return ret;
}
#endif
//htc audio --
static const struct snd_soc_dapm_widget msm8994_dapm_widgets[] = {

	SND_SOC_DAPM_SUPPLY("MCLK",  SND_SOC_NOPM, 0, 0,
	msm8994_mclk_event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
//htc audio ++
	SND_SOC_DAPM_SPK("Lineout_1 amp", msm_ext_spkramp_event),
	SND_SOC_DAPM_SPK("Lineout_3 amp", msm_ext_spkramp_event),

	SND_SOC_DAPM_SPK("Lineout_2 amp", msm_ext_spkramp_event),
	SND_SOC_DAPM_SPK("Lineout_4 amp", msm_ext_spkramp_event),
//htc audio --
	SND_SOC_DAPM_SPK("ultrasound amp", msm_ext_ultrasound_event),
	SND_SOC_DAPM_MIC("Handset Mic", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
#ifdef CONFIG_USE_CODEC_MBHC //htc audio
	SND_SOC_DAPM_MIC("ANCRight Headset Mic", msm_headset_lr_event),
	SND_SOC_DAPM_MIC("ANCLeft Headset Mic", msm_headset_lr_event),
#else
	SND_SOC_DAPM_MIC("ANCRight Headset Mic", NULL),
	SND_SOC_DAPM_MIC("ANCLeft Headset Mic", NULL),
#endif
	SND_SOC_DAPM_MIC("Analog Mic4", NULL),
	SND_SOC_DAPM_MIC("Analog Mic5", NULL),
	SND_SOC_DAPM_MIC("Analog Mic6", NULL),
	SND_SOC_DAPM_MIC("Analog Mic7", NULL),
	SND_SOC_DAPM_MIC("Analog Mic8", NULL),

	SND_SOC_DAPM_MIC("Digital Mic1", NULL),
	SND_SOC_DAPM_MIC("Digital Mic2", NULL),
	SND_SOC_DAPM_MIC("Digital Mic3", NULL),
	SND_SOC_DAPM_MIC("Digital Mic4", NULL),
	SND_SOC_DAPM_MIC("Digital Mic5", NULL),
	SND_SOC_DAPM_MIC("Digital Mic6", NULL),
};

static int slim0_rx_sample_rate_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int sample_rate_val = 0;

	switch (slim0_rx_sample_rate) {
	case SAMPLING_RATE_192KHZ:
		sample_rate_val = 2;
		break;

	case SAMPLING_RATE_96KHZ:
		sample_rate_val = 1;
		break;

	case SAMPLING_RATE_48KHZ:
	default:
		sample_rate_val = 0;
		break;
	}

	ucontrol->value.integer.value[0] = sample_rate_val;
	pr_debug("%s: slim0_rx_sample_rate = %d\n", __func__,
				slim0_rx_sample_rate);

	return 0;
}

static int slim0_rx_sample_rate_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: ucontrol value = %ld\n", __func__,
			ucontrol->value.integer.value[0]);

	switch (ucontrol->value.integer.value[0]) {
	case 2:
		slim0_rx_sample_rate = SAMPLING_RATE_192KHZ;
		break;
	case 1:
		slim0_rx_sample_rate = SAMPLING_RATE_96KHZ;
		break;
	case 0:
	default:
		slim0_rx_sample_rate = SAMPLING_RATE_48KHZ;
	}

	pr_debug("%s: slim0_rx_sample_rate = %d\n", __func__,
			slim0_rx_sample_rate);

	return 0;
}

static int slim0_rx_bit_format_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{

	switch (slim0_rx_bit_format) {
	case SNDRV_PCM_FORMAT_S24_3LE:
		ucontrol->value.integer.value[0] = 2;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		ucontrol->value.integer.value[0] = 1;
		break;

	case SNDRV_PCM_FORMAT_S16_LE:
	default:
		ucontrol->value.integer.value[0] = 0;
		break;
	}

	pr_debug("%s: slim0_rx_bit_format = %d, ucontrol value = %ld\n",
			 __func__, slim0_rx_bit_format,
			ucontrol->value.integer.value[0]);

	return 0;
}

static int slim0_rx_bit_format_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	switch (ucontrol->value.integer.value[0]) {
	case 2:
		slim0_rx_bit_format = SNDRV_PCM_FORMAT_S24_3LE;
		break;
	case 1:
		slim0_rx_bit_format = SNDRV_PCM_FORMAT_S24_LE;
		break;
	case 0:
	default:
		slim0_rx_bit_format = SNDRV_PCM_FORMAT_S16_LE;
		break;
	}
	return 0;
}

static int msm_slim_0_rx_ch_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: msm_slim_0_rx_ch  = %d\n", __func__,
		 msm_slim_0_rx_ch);
	ucontrol->value.integer.value[0] = msm_slim_0_rx_ch - 1;
	return 0;
}

static int msm_slim_0_rx_ch_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	msm_slim_0_rx_ch = ucontrol->value.integer.value[0] + 1;

	pr_debug("%s: msm_slim_0_rx_ch = %d\n", __func__,
		 msm_slim_0_rx_ch);
	return 1;
}

static int slim0_tx_sample_rate_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int sample_rate_val = 0;

	switch (slim0_tx_sample_rate) {
	case SAMPLING_RATE_192KHZ:
		sample_rate_val = 2;
		break;
	case SAMPLING_RATE_96KHZ:
		sample_rate_val = 1;
		break;
	case SAMPLING_RATE_48KHZ:
	default:
		sample_rate_val = 0;
		break;
	}

	ucontrol->value.integer.value[0] = sample_rate_val;
	pr_debug("%s: slim0_tx_sample_rate = %d\n", __func__,
				slim0_tx_sample_rate);

	return 0;
}

static int slim0_tx_sample_rate_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int rc = 0;

	pr_debug("%s: ucontrol value = %ld\n", __func__,
			ucontrol->value.integer.value[0]);

	switch (ucontrol->value.integer.value[0]) {
	case 2:
		slim0_tx_sample_rate = SAMPLING_RATE_192KHZ;
		break;
	case 1:
		slim0_tx_sample_rate = SAMPLING_RATE_96KHZ;
		break;
	case 0:
		slim0_tx_sample_rate = SAMPLING_RATE_48KHZ;
		break;
	default:
		rc = -EINVAL;
		pr_err("%s: invalid sample rate being passed\n", __func__);
		break;
	}

	pr_debug("%s: slim0_tx_sample_rate = %d\n", __func__,
			slim0_tx_sample_rate);

	return rc;
}

static int slim0_tx_bit_format_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{

	switch (slim0_tx_bit_format) {
	case SNDRV_PCM_FORMAT_S24_LE:
		ucontrol->value.integer.value[0] = 1;
		break;

	case SNDRV_PCM_FORMAT_S16_LE:
	default:
		ucontrol->value.integer.value[0] = 0;
		break;
	}

	pr_debug("%s: slim0_tx_bit_format = %d, ucontrol value = %ld\n",
			 __func__, slim0_tx_bit_format,
			ucontrol->value.integer.value[0]);

	return 0;
}

static int slim0_tx_bit_format_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	int rc = 0;

	switch (ucontrol->value.integer.value[0]) {
	case 1:
		slim0_tx_bit_format = SNDRV_PCM_FORMAT_S24_LE;
		break;
	case 0:
		slim0_tx_bit_format = SNDRV_PCM_FORMAT_S16_LE;
		break;
	default:
		pr_err("%s: invalid value %ld\n", __func__,
		       ucontrol->value.integer.value[0]);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int msm_slim_0_tx_ch_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: msm_slim_0_tx_ch  = %d\n", __func__,
		 msm_slim_0_tx_ch);
	ucontrol->value.integer.value[0] = msm_slim_0_tx_ch - 1;
	return 0;
}

static int msm_slim_0_tx_ch_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	msm_slim_0_tx_ch = ucontrol->value.integer.value[0] + 1;

	pr_debug("%s: msm_slim_0_tx_ch = %d\n", __func__, msm_slim_0_tx_ch);
	return 1;
}

static int msm_vi_feed_tx_ch_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = (msm_vi_feed_tx_ch/2 - 1);
	pr_debug("%s: msm_vi_feed_tx_ch = %ld\n", __func__,
		 ucontrol->value.integer.value[0]);
	return 0;
}

static int msm_vi_feed_tx_ch_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	msm_vi_feed_tx_ch =
		roundup_pow_of_two(ucontrol->value.integer.value[0] + 2);

	pr_debug("%s: msm_vi_feed_tx_ch = %d\n", __func__, msm_vi_feed_tx_ch);
	return 1;
}

static int msm_btsco_rate_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: msm_btsco_rate  = %d", __func__, msm_btsco_rate);
	ucontrol->value.integer.value[0] = msm_btsco_rate;
	return 0;
}

static int msm_btsco_rate_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	switch (ucontrol->value.integer.value[0]) {
	case SAMPLING_RATE_8KHZ:
		msm_btsco_rate = SAMPLING_RATE_8KHZ;
		break;
	case SAMPLING_RATE_16KHZ:
		msm_btsco_rate = SAMPLING_RATE_16KHZ;
		break;
	default:
		msm_btsco_rate = SAMPLING_RATE_8KHZ;
		break;
	}
	pr_debug("%s: msm_btsco_rate = %d\n", __func__, msm_btsco_rate);
	return 0;
}

static int hdmi_rx_bit_format_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{

	switch (hdmi_rx_bit_format) {
	case SNDRV_PCM_FORMAT_S24_LE:
		ucontrol->value.integer.value[0] = 1;
		break;

	case SNDRV_PCM_FORMAT_S16_LE:
	default:
		ucontrol->value.integer.value[0] = 0;
		break;
	}

	pr_debug("%s: hdmi_rx_bit_format = %d, ucontrol value = %ld\n",
			 __func__, hdmi_rx_bit_format,
			ucontrol->value.integer.value[0]);

	return 0;
}

static int hdmi_rx_bit_format_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	switch (ucontrol->value.integer.value[0]) {
	case 1:
		hdmi_rx_bit_format = SNDRV_PCM_FORMAT_S24_LE;
		break;
	case 0:
	default:
		hdmi_rx_bit_format = SNDRV_PCM_FORMAT_S16_LE;
		break;
	}
	pr_debug("%s: hdmi_rx_bit_format = %d, ucontrol value = %ld\n",
			 __func__, hdmi_rx_bit_format,
			ucontrol->value.integer.value[0]);
	return 0;
}

static int msm_hdmi_rx_ch_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: msm_hdmi_rx_ch  = %d\n", __func__,
			msm_hdmi_rx_ch);
	ucontrol->value.integer.value[0] = msm_hdmi_rx_ch - 2;

	return 0;
}

static int msm_hdmi_rx_ch_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	msm_hdmi_rx_ch = ucontrol->value.integer.value[0] + 2;
	if (msm_hdmi_rx_ch > 8) {
		pr_err("%s: channels %d exceeded 8.Limiting to max chs-8\n",
			__func__, msm_hdmi_rx_ch);
		msm_hdmi_rx_ch = 8;
	}
	pr_debug("%s: msm_hdmi_rx_ch = %d\n", __func__, msm_hdmi_rx_ch);

	return 1;
}

static int hdmi_rx_sample_rate_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int sample_rate_val = 0;

	switch (hdmi_rx_sample_rate) {
	case SAMPLING_RATE_192KHZ:
		sample_rate_val = 2;
		break;

	case SAMPLING_RATE_96KHZ:
		sample_rate_val = 1;
		break;

	case SAMPLING_RATE_48KHZ:
	default:
		sample_rate_val = 0;
		break;
	}

	ucontrol->value.integer.value[0] = sample_rate_val;
	pr_debug("%s: hdmi_rx_sample_rate = %d\n", __func__,
				hdmi_rx_sample_rate);

	return 0;
}

static int hdmi_rx_sample_rate_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: ucontrol value = %ld\n", __func__,
			ucontrol->value.integer.value[0]);

	switch (ucontrol->value.integer.value[0]) {
	case 2:
		hdmi_rx_sample_rate = SAMPLING_RATE_192KHZ;
		break;
	case 1:
		hdmi_rx_sample_rate = SAMPLING_RATE_96KHZ;
		break;
	case 0:
	default:
		hdmi_rx_sample_rate = SAMPLING_RATE_48KHZ;
	}

	pr_debug("%s: hdmi_rx_sample_rate = %d\n", __func__,
			hdmi_rx_sample_rate);

	return 0;
}

static int msm8994_auxpcm_rate_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = msm8994_auxpcm_rate;
	return 0;
}

static int msm8994_auxpcm_rate_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	switch (ucontrol->value.integer.value[0]) {
	case 0:
		msm8994_auxpcm_rate = SAMPLING_RATE_8KHZ;
		break;
	case 1:
		msm8994_auxpcm_rate = SAMPLING_RATE_16KHZ;
		break;
	default:
		msm8994_auxpcm_rate = SAMPLING_RATE_8KHZ;
		break;
	}
	return 0;
}

static int quat_mi2s_sample_rate_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int sample_rate_val = 0;

	switch (quat_mi2s_sample_rate) {
	case SAMPLING_RATE_192KHZ:
		sample_rate_val = 2;
		break;

	case SAMPLING_RATE_96KHZ:
		sample_rate_val = 1;
		break;

	case SAMPLING_RATE_48KHZ:
	default:
		sample_rate_val = 0;
		break;
	}

	ucontrol->value.integer.value[0] = sample_rate_val;

	return 0;
}

static int quat_mi2s_sample_rate_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{

	switch (ucontrol->value.integer.value[0]) {
	case 2:
		quat_mi2s_sample_rate = SAMPLING_RATE_192KHZ;
		break;
	case 1:
		quat_mi2s_sample_rate = SAMPLING_RATE_96KHZ;
		break;
	case 0:
	default:
		quat_mi2s_sample_rate = SAMPLING_RATE_48KHZ;
	}

	pr_debug("%s: sample_rate = %d\n", __func__, quat_mi2s_sample_rate);

	return 0;
}

static int quat_mi2s_bit_format_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{

	switch (quat_mi2s_bit_format) {
	case SNDRV_PCM_FORMAT_S24_LE:
		ucontrol->value.integer.value[0] = 1;
		break;

	case SNDRV_PCM_FORMAT_S16_LE:
	default:
		ucontrol->value.integer.value[0] = 0;
		break;
	}



	return 0;
}

static int quat_mi2s_bit_format_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	switch (ucontrol->value.integer.value[0]) {
	case 1:
		quat_mi2s_bit_format = SNDRV_PCM_FORMAT_S24_LE;
		break;
	case 0:
	default:
		quat_mi2s_bit_format = SNDRV_PCM_FORMAT_S16_LE;
		break;
	}
	pr_debug("%s: bit_format = %d \n", __func__, quat_mi2s_bit_format);
	return 0;
}

static int tert_mi2s_sample_rate_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int sample_rate_val = 0;

	switch (tert_mi2s_sample_rate) {
	case SAMPLING_RATE_192KHZ:
		sample_rate_val = 2;
		break;

	case SAMPLING_RATE_96KHZ:
		sample_rate_val = 1;
		break;

	case SAMPLING_RATE_48KHZ:
	default:
		sample_rate_val = 0;
		break;
	}

	ucontrol->value.integer.value[0] = sample_rate_val;

	return 0;
}

static int tert_mi2s_sample_rate_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{

	switch (ucontrol->value.integer.value[0]) {
	case 2:
		tert_mi2s_sample_rate = SAMPLING_RATE_192KHZ;
		break;
	case 1:
		tert_mi2s_sample_rate = SAMPLING_RATE_96KHZ;
		break;
	case 0:
	default:
		tert_mi2s_sample_rate = SAMPLING_RATE_48KHZ;
	}

	pr_debug("%s: sample_rate = %d\n", __func__, tert_mi2s_sample_rate);

	return 0;
}

static int tert_mi2s_bit_format_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{

	switch (tert_mi2s_bit_format) {
	case SNDRV_PCM_FORMAT_S24_LE:
		ucontrol->value.integer.value[0] = 1;
		break;

	case SNDRV_PCM_FORMAT_S16_LE:
	default:
		ucontrol->value.integer.value[0] = 0;
		break;
	}



	return 0;
}

static int tert_mi2s_bit_format_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	switch (ucontrol->value.integer.value[0]) {
	case 1:
		tert_mi2s_bit_format = SNDRV_PCM_FORMAT_S24_LE;
		break;
	case 0:
	default:
		tert_mi2s_bit_format = SNDRV_PCM_FORMAT_S16_LE;
		break;
	}
	pr_debug("%s: bit_format = %d \n", __func__, tert_mi2s_bit_format);
	return 0;
}



static int msm_proxy_rx_ch_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: msm_proxy_rx_ch = %d\n", __func__,
						msm_proxy_rx_ch);
	ucontrol->value.integer.value[0] = msm_proxy_rx_ch - 1;
	return 0;
}

static int msm_proxy_rx_ch_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	msm_proxy_rx_ch = ucontrol->value.integer.value[0] + 1;
	pr_debug("%s: msm_proxy_rx_ch = %d\n", __func__,
						msm_proxy_rx_ch);
	return 1;
}

static int msm_auxpcm_be_params_fixup(struct snd_soc_pcm_runtime *rtd,
					struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate =
	    hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);

	struct snd_interval *channels =
	    hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);

	rate->min = rate->max = msm8994_auxpcm_rate;
	channels->min = channels->max = 1;

	return 0;
}

static int msm_proxy_rx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);

	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	pr_debug("%s: msm_proxy_rx_ch =%d\n", __func__, msm_proxy_rx_ch);

	if (channels->max < 2)
		channels->min = channels->max = 2;
	channels->min = channels->max = msm_proxy_rx_ch;
	rate->min = rate->max = 48000;
	return 0;
}

static int msm_proxy_tx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);

	rate->min = rate->max = 48000;
	return 0;
}

static int msm8994_hdmi_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);

	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	pr_debug("%s channels->min %u channels->max %u ()\n", __func__,
			channels->min, channels->max);

	param_set_mask(params, SNDRV_PCM_HW_PARAM_FORMAT,
				hdmi_rx_bit_format);
	if (channels->max < 2)
		channels->min = channels->max = 2;
	rate->min = rate->max = hdmi_rx_sample_rate;
	channels->min = channels->max = msm_hdmi_rx_ch;

	return 0;
}

static int msm_set_pinctrl(struct msm_pinctrl_info *pinctrl_info)
{
	int ret = 0;

	if (pinctrl_info == NULL) {
		pr_err("%s: pinctrl_info is NULL\n", __func__);
		ret = -EINVAL;
		goto err;
	}
	pr_debug("%s: curr_state = %s\n", __func__,
		 pin_states[pinctrl_info->curr_state]);

	switch (pinctrl_info->curr_state) {
	case STATE_DISABLE:
		ret = pinctrl_select_state(pinctrl_info->pinctrl,
					   pinctrl_info->active);
		if (ret) {
			pr_err("%s: pinctrl_select_state failed with %d\n",
				__func__, ret);
			ret = -EIO;
			goto err;
		}
		pinctrl_info->curr_state = STATE_ON;
		break;
	case STATE_ON:
		pr_err("%s: TLMM pins already set\n", __func__);
		break;
	default:
		pr_err("%s: TLMM pin state is invalid\n", __func__);
		return -EINVAL;
	}

err:
	return ret;
}

static int msm_reset_pinctrl(struct msm_pinctrl_info *pinctrl_info)
{
	int ret = 0;

	if (pinctrl_info == NULL) {
		pr_err("%s: pinctrl_info is NULL\n", __func__);
		ret = -EINVAL;
		goto err;
	}
	pr_debug("%s: curr_state = %s\n", __func__,
		 pin_states[pinctrl_info->curr_state]);

	switch (pinctrl_info->curr_state) {
	case STATE_ON:
		ret = pinctrl_select_state(pinctrl_info->pinctrl,
					   pinctrl_info->disable);
		if (ret) {
			pr_err("%s: pinctrl_select_state failed with %d\n",
				__func__, ret);
			ret = -EIO;
			goto err;
		}
		pinctrl_info->curr_state = STATE_DISABLE;
		break;
	case STATE_DISABLE:
		pr_err("%s: TLMM pins already disabled\n", __func__);
		break;
	default:
		pr_err("%s: TLMM pin state is invalid\n", __func__);
		return -EINVAL;
	}
err:
	return ret;
}

//htc audio ++
static int msm_wcd_get_pinctrl(struct platform_device *pdev, struct platform_device *pinpdev,
							enum wcd_mux mux)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	struct msm_pinctrl_info *pinctrl_info = NULL;
	struct pinctrl *pinctrl;
	int ret;

	switch (mux) {
	case WCD_CLK2:
		pinctrl_info = &pdata->wcd_clk2_pinctrl_info;
		break;
	default:
		pr_err("%s: Not a valid MUX ID: %d\n", __func__, mux);
		break;
	}
	if (pinctrl_info == NULL) {
		pr_err("%s: pinctrl_info is NULL\n", __func__);
		return -EINVAL;
	}

	pinctrl = devm_pinctrl_get(&pinpdev->dev);
	if (IS_ERR_OR_NULL(pinctrl)) {
		pr_err("%s: Unable to get pinctrl handle\n", __func__);
		return -EINVAL;
	}
	pinctrl_info->pinctrl = pinctrl;

	/* get all the states handles from Device Tree */
	pinctrl_info->disable = pinctrl_lookup_state(pinctrl,
						"wcd_clk2-sleep");
	if (IS_ERR(pinctrl_info->disable)) {
		pr_err("%s: could not get disable pinstate\n", __func__);
		goto err;
	}

	pinctrl_info->active = pinctrl_lookup_state(pinctrl,
						"wcd_clk2-active");
	if (IS_ERR(pinctrl_info->active)) {
		pr_err("%s: could not get active pinstate\n",
			__func__);
		goto err;
	}

	/* Reset the AUXPCM TLMM pins to a default state */
	ret = pinctrl_select_state(pinctrl_info->pinctrl,
					pinctrl_info->active);
	if (ret != 0) {
		pr_err("%s: Disable TLMM pins failed with %d\n",
			__func__, ret);
		ret = -EIO;
		goto err;
	}
	pinctrl_info->curr_state = STATE_ON;

	return 0;

err:
	devm_pinctrl_put(pinctrl);
	pinctrl_info->pinctrl = NULL;
	return -EINVAL;
}

static void ftm_auxpcm_release_pinctrl(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	struct msm_pinctrl_info *pinctrl_info = NULL;

	pinctrl_info = &pdata->ftm_sec_auxpcm_pinctrl_info;
	if (pinctrl_info) {
		devm_pinctrl_put(pinctrl_info->pinctrl);
		pinctrl_info->pinctrl = NULL;
	}
}

static int ftm_auxpcm_get_pinctrl(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	struct msm_pinctrl_info *pinctrl_info = NULL;
	struct pinctrl *pinctrl;
	int ret;

	pinctrl_info = &pdata->ftm_sec_auxpcm_pinctrl_info;
	if (pinctrl_info == NULL) {
		pr_err("%s: pinctrl_info is NULL\n", __func__);
		return -EINVAL;
	}

	pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR_OR_NULL(pinctrl)) {
		pr_err("%s: Unable to get pinctrl handle\n", __func__);
		return -EINVAL;
	}
	pinctrl_info->pinctrl = pinctrl;

	/* get all the states handles from Device Tree */
	pinctrl_info->disable = pinctrl_lookup_state(pinctrl,
						"ftm-auxpcm-sleep");
	if (IS_ERR(pinctrl_info->disable)) {
		pr_err("%s: could not get disable pinstate ftm-auxpcm-sleep\n", __func__);
		goto err;
	}

	pinctrl_info->active = pinctrl_lookup_state(pinctrl,
						"ftm-auxpcm-active");
	if (IS_ERR(pinctrl_info->active)) {
		pr_err("%s: could not get active pinstate ftm-auxpcm-active\n", __func__);
		goto err;
	}

	/* Reset the AUXPCM TLMM pins to a active state */
	ret = pinctrl_select_state(pinctrl_info->pinctrl,
					pinctrl_info->active);
	if (ret != 0) {
		pr_err("%s: Config TLMM pins failed with %d\n",
			__func__, ret);
		ret = -EIO;
		goto err;
	}
	pinctrl_info->curr_state = STATE_ON;

	return 0;

err:
	devm_pinctrl_put(pinctrl);
	pinctrl_info->pinctrl = NULL;
	return -EINVAL;
}
//htc audio --

static void msm_auxpcm_release_pinctrl(struct platform_device *pdev,
				enum mi2s_pcm_mux mux)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	struct msm_pinctrl_info *pinctrl_info = NULL;

	switch (mux) {
	case SEC_MI2S_PCM:
		pinctrl_info = &pdata->sec_auxpcm_pinctrl_info;
		break;
	default:
		pr_err("%s: Not a valid MUX ID: %d\n", __func__, mux);
		break;
	}
	if (pinctrl_info) {
		iounmap(pinctrl_info->mux);
		devm_pinctrl_put(pinctrl_info->pinctrl);
		pinctrl_info->pinctrl = NULL;
	}
}

static int msm_auxpcm_get_pinctrl(struct platform_device *pdev,
							enum mi2s_pcm_mux mux)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	struct msm_pinctrl_info *pinctrl_info = NULL;
	struct pinctrl *pinctrl;
	struct resource	*muxsel;
	int ret;

	switch (mux) {
	case SEC_MI2S_PCM:
		pinctrl_info = &pdata->sec_auxpcm_pinctrl_info;
		break;
	default:
		pr_err("%s: Not a valid MUX ID: %d\n", __func__, mux);
		break;
	}
	if (pinctrl_info == NULL) {
		pr_err("%s: pinctrl_info is NULL\n", __func__);
		return -EINVAL;
	}

	pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR_OR_NULL(pinctrl)) {
		pr_err("%s: Unable to get pinctrl handle\n", __func__);
		return -EINVAL;
	}
	pinctrl_info->pinctrl = pinctrl;

	/* get all the states handles from Device Tree */
	pinctrl_info->disable = pinctrl_lookup_state(pinctrl,
						"auxpcm-sleep");
	if (IS_ERR(pinctrl_info->disable)) {
		pr_err("%s: could not get disable pinstate\n", __func__);
		goto err;
	}

	pinctrl_info->active = pinctrl_lookup_state(pinctrl,
						"auxpcm-active");
	if (IS_ERR(pinctrl_info->active)) {
		pr_err("%s: could not get active pinstate\n",
			__func__);
		goto err;
	}

	/* Reset the AUXPCM TLMM pins to a default state */
	ret = pinctrl_select_state(pinctrl_info->pinctrl,
					pinctrl_info->disable);
	if (ret != 0) {
		pr_err("%s: Disable TLMM pins failed with %d\n",
			__func__, ret);
		ret = -EIO;
		goto err;
	}
	pinctrl_info->curr_state = STATE_DISABLE;

	muxsel = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						"lpaif_sec_mode_muxsel");
	if (!muxsel) {
		dev_err(&pdev->dev, "MUX addr invalid for AUXPCM\n");
		ret = -ENODEV;
		goto err;
	}
	pinctrl_info->mux = ioremap(muxsel->start, resource_size(muxsel));
	if (pinctrl_info->mux == NULL) {
		pr_err("%s: AUXPCM muxsel virt addr is null\n", __func__);
		ret = -EINVAL;
		goto err;
	}
	return 0;

err:
	devm_pinctrl_put(pinctrl);
	pinctrl_info->pinctrl = NULL;
	return -EINVAL;
}

static int msm_sec_auxpcm_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	struct msm_pinctrl_info *pinctrl_info = &pdata->sec_auxpcm_pinctrl_info;
	int ret = 0;
	u32 pcm_sel_reg = 0;

	pr_info("%s(): substream = %s, sec_auxpcm_rsc_ref counter = %d\n",
		__func__, substream->name, atomic_read(&sec_auxpcm_rsc_ref));

	if (pinctrl_info == NULL) {
		pr_err("%s: pinctrl_info is NULL\n", __func__);
		ret = -EINVAL;
		goto err;
	}
	if (atomic_inc_return(&sec_auxpcm_rsc_ref) == 1) {
		if (pinctrl_info->mux != NULL) {
			pcm_sel_reg = ioread32(pinctrl_info->mux);
			iowrite32(I2S_PCM_SEL_PCM << I2S_PCM_SEL_OFFSET,
				  pinctrl_info->mux);
		} else {
			pr_err("%s Sec AUXPCM MUX addr is NULL\n", __func__);
			ret = -EINVAL;
			goto err;
		}
		ret = msm_set_pinctrl(pinctrl_info);
		if (ret) {
			pr_err("%s: AUXPCM TLMM pinctrl set failed with %d\n",
				__func__, ret);
			return ret;
		}
	}
err:
	return ret;
}

static void msm_sec_auxpcm_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	struct msm_pinctrl_info *pinctrl_info = &pdata->sec_auxpcm_pinctrl_info;
	int ret = 0;

	pr_debug("%s(): substream = %s, sec_auxpcm_rsc_ref counter = %d\n",
		__func__, substream->name, atomic_read(&sec_auxpcm_rsc_ref));

	if (atomic_dec_return(&sec_auxpcm_rsc_ref) == 0) {
		ret = msm_reset_pinctrl(pinctrl_info);
		if (ret)
			pr_err("%s Reset pinctrl failed with %d\n",
				__func__, ret);
	}
}

static struct snd_soc_ops msm_sec_auxpcm_be_ops = {
	.startup = msm_sec_auxpcm_startup,
	.shutdown = msm_sec_auxpcm_shutdown,
};

static void msm_mi2s_release_pinctrl(struct platform_device *pdev,
				enum mi2s_pcm_mux mux)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	struct msm_pinctrl_info *pinctrl_info = &pdata->pri_mi2s_pinctrl_info;

	switch (mux) {
	case PRI_MI2S_PCM:
		pinctrl_info = &pdata->pri_mi2s_pinctrl_info;
		break;
	case SEC_MI2S_PCM:
		pinctrl_info = &pdata->sec_mi2s_pinctrl_info;
		break;
	case TERT_MI2S_PCM:
		pinctrl_info = &pdata->tert_mi2s_pinctrl_info;
		break;
	case QUAT_MI2S_PCM:
		pinctrl_info = &pdata->quat_mi2s_pinctrl_info;
		break;

	default:
		pr_err("%s: Not a valid MUX ID: %d\n", __func__, mux);
		break;
	}
	if (pinctrl_info) {
		iounmap(pinctrl_info->mux);
		devm_pinctrl_put(pinctrl_info->pinctrl);
		pinctrl_info->pinctrl = NULL;
	}
}

static int msm_mi2s_get_pinctrl(struct platform_device *pdev, struct platform_device *pinpdev,
							enum mi2s_pcm_mux mux)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	struct msm_pinctrl_info *pinctrl_info = NULL;
	struct pinctrl *pinctrl;
#if 0 //htc audio: mark this for audio resume time issue
	struct resource	*muxsel;
#endif
	char *const mi2s_sleep_state[] =  {"pri_mi2s_sleep", "sec_mi2s_sleep", "tert_mi2s_sleep", "quat_mi2s_sleep" };
	char *const mi2s_active_state[] = {"pri_mi2s_active", "sec_mi2s_active", "tert_mi2s_active", "quat_mi2s_active" };
#if 0 //htc audio: mark this for audio resume time issue
	char *const mi2s_lpaif_muxsel[] = {"lpaif_pri_mode_muxsel", "lpaif_sec_mode_muxsel", "lpaif_tert_mode_muxsel", "lpaif_quat_mode_muxsel" };
#endif
	int ret;

	switch (mux) {
	case PRI_MI2S_PCM:
		pinctrl_info = &pdata->pri_mi2s_pinctrl_info;
		break;
	case SEC_MI2S_PCM:
		pinctrl_info = &pdata->sec_mi2s_pinctrl_info;
		break;
	case TERT_MI2S_PCM:
		pinctrl_info = &pdata->tert_mi2s_pinctrl_info;
		break;
	case QUAT_MI2S_PCM:
		pinctrl_info = &pdata->quat_mi2s_pinctrl_info;
		break;

	default:
		pr_err("%s: Not a valid MUX ID: %d\n", __func__, mux);
		break;
	}
	if (pinctrl_info == NULL) {
		pr_err("%s: pinctrl_info is NULL\n", __func__);
		return -EINVAL;
	}

	pinctrl = devm_pinctrl_get(&pinpdev->dev);
	if (IS_ERR_OR_NULL(pinctrl)) {
		pr_err("%s: Unable to get pinctrl handle\n", __func__);
		return -EINVAL;
	}
	pinctrl_info->pinctrl = pinctrl;

	/* get all the states handles from Device Tree */
	pinctrl_info->disable = pinctrl_lookup_state(pinctrl, mi2s_sleep_state[mux]);
	if (IS_ERR(pinctrl_info->disable)) {
		pr_err("%s: could not get disable pinstate\n", __func__);
		goto err;
	}

	pinctrl_info->active = pinctrl_lookup_state(pinctrl, mi2s_active_state[mux]);
	if (IS_ERR(pinctrl_info->active)) {
		pr_err("%s: could not get mi2s_active pinstate\n",
			__func__);
		goto err;
	}

	/* Reset the MI2S TLMM pins to a default state */
	ret = pinctrl_select_state(pinctrl_info->pinctrl,
					pinctrl_info->disable);
	if (ret != 0) {
		pr_err("%s: Disable MI2S TLMM pins failed with %d\n",
			__func__, ret);
		ret = -EIO;
		goto err;
	}
	pinctrl_info->curr_state = STATE_DISABLE;

#if 0 //htc audio: mark this for audio resume time issue
	muxsel = platform_get_resource_byname(pdev, IORESOURCE_MEM, mi2s_lpaif_muxsel[mux]);
	if (!muxsel) {
		dev_err(&pdev->dev, "MUX addr invalid for MI2S\n");
		ret = -ENODEV;
		goto err;
	}
	pinctrl_info->mux = ioremap(muxsel->start, resource_size(muxsel));
	if (pinctrl_info->mux == NULL) {
		pr_err("%s: MI2S muxsel virt addr is null\n", __func__);
		ret = -EINVAL;
		goto err;
	}
#endif

	return 0;

err:
	devm_pinctrl_put(pinctrl);
	pinctrl_info->pinctrl = NULL;
	return -EINVAL;
}

static int msm_tx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	pr_debug("%s: channel:%d\n", __func__, msm_pri_mi2s_tx_ch);
	rate->min = rate->max = SAMPLING_RATE_48KHZ;
	channels->min = channels->max = msm_pri_mi2s_tx_ch;
	return 0;
}

static int msm8994_mi2s_snd_startup(struct snd_pcm_substream *substream)
{
	int ret = 0;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	struct msm_pinctrl_info *pinctrl_info = &pdata->pri_mi2s_pinctrl_info;

	pr_debug("%s: substream = %s  stream = %d\n", __func__,
		substream->name, substream->stream);

	if (pinctrl_info == NULL) {
		pr_err("%s: pinctrl_info is NULL\n", __func__);
		ret = -EINVAL;
		goto err;
	}
	if (pinctrl_info->mux != NULL)
		iowrite32(I2S_PCM_SEL_I2S << I2S_PCM_SEL_OFFSET,
				pinctrl_info->mux);
	else
		pr_err("%s: MI2S muxsel addr is NULL\n", __func__);

	ret = msm_set_pinctrl(pinctrl_info);
	if (ret) {
		pr_err("%s: MI2S TLMM pinctrl set failed with %d\n",
			__func__, ret);
		return ret;
	}

	mi2s_tx_clk.clk_val1 = Q6AFE_LPASS_IBIT_CLK_1_P536_MHZ;
	mi2s_tx_clk.clk_set_mode = Q6AFE_LPASS_MODE_CLK1_VALID;
	ret = afe_set_lpass_clock(AFE_PORT_ID_PRIMARY_MI2S_TX,
				&mi2s_tx_clk);
	if (ret < 0) {
		pr_err("%s: afe lpass clock failed, err:%d\n", __func__, ret);
		goto err;
	}
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		pr_err("%s: set fmt cpu dai failed, err:%d\n", __func__, ret);
err:
	return ret;
}

static void msm8994_mi2s_snd_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	struct msm_pinctrl_info *pinctrl_info = &pdata->pri_mi2s_pinctrl_info;
	int ret = 0;

	pr_debug("%s: substream = %s  stream = %d\n", __func__,
		substream->name, substream->stream);

	mi2s_tx_clk.clk_val1 = Q6AFE_LPASS_IBIT_CLK_DISABLE;
	mi2s_tx_clk.clk_set_mode = Q6AFE_LPASS_MODE_CLK1_VALID;
	ret = afe_set_lpass_clock(AFE_PORT_ID_PRIMARY_MI2S_TX,
				&mi2s_tx_clk);
	if (ret < 0)
		pr_err("%s: afe lpass clock failed, err:%d\n", __func__, ret);

	ret = msm_reset_pinctrl(pinctrl_info);
	if (ret)
		pr_err("%s: Reset pinctrl failed with %d\n",
			__func__, ret);
}

static int msm8994_tert_mi2s_snd_startup(struct snd_pcm_substream *substream)
{
	int ret = 0;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	struct msm_pinctrl_info *pinctrl_info = &pdata->tert_mi2s_pinctrl_info;

	pr_info("%s: dai name %s %pK  substream = %s  stream = %d bit width =%d sample rate =%d  \n", __func__, cpu_dai->name, cpu_dai->dev,substream->name,
			substream->stream, tert_mi2s_bit_format, tert_mi2s_sample_rate);

	if (atomic_inc_return(&tert_mi2s_rsc_ref) == 1) {
		if (pinctrl_info == NULL) {
			pr_err("%s: pinctrl_info is NULL\n", __func__);
			ret = -EINVAL;
			goto err;
		}
		if (pinctrl_info->mux != NULL)
			iowrite32(I2S_PCM_SEL_I2S << I2S_PCM_SEL_OFFSET, pinctrl_info->mux);
		else pr_err("%s: MI2S muxsel addr is NULL\n", __func__);

		ret = msm_set_pinctrl(pinctrl_info);
		if (ret) {
			pr_err("%s: MI2S TLMM pinctrl set failed with %d\n", __func__, ret);
		return ret;
		}
		if(tert_mi2s_bit_format==SNDRV_PCM_FORMAT_S24_LE)
		{
			switch(tert_mi2s_sample_rate) {
				case SAMPLING_RATE_192KHZ :
					tert_mi2s_clk.clk_val1 = Q6AFE_LPASS_IBIT_CLK_12_P288_MHZ;
					break;
				case SAMPLING_RATE_96KHZ :
					tert_mi2s_clk.clk_val1 = Q6AFE_LPASS_IBIT_CLK_6_P144_MHZ;
					break;
				case SAMPLING_RATE_48KHZ :
				default:
					tert_mi2s_clk.clk_val1 = Q6AFE_LPASS_IBIT_CLK_3_P072_MHZ;
					break;
			}
		} else {
			switch(tert_mi2s_sample_rate) {
				case SAMPLING_RATE_192KHZ :
			        tert_mi2s_clk.clk_val1 = Q6AFE_LPASS_IBIT_CLK_6_P144_MHZ;
					break;
				case SAMPLING_RATE_96KHZ :
					tert_mi2s_clk.clk_val1 = Q6AFE_LPASS_IBIT_CLK_3_P072_MHZ;
					break;
				case SAMPLING_RATE_48KHZ :
				default:
					tert_mi2s_clk.clk_val1 = Q6AFE_LPASS_IBIT_CLK_1_P536_MHZ;
					break;
			}
		}


#ifdef MSM_TERT_MI2S_MASTER
#ifdef MSM_TERT_MI2S_MCLK
		tert_mi2s_clk.clk_val2 = Q6AFE_LPASS_OSR_CLK_12_P288_MHZ;
		tert_mi2s_clk.clk_set_mode = Q6AFE_LPASS_MODE_BOTH_VALID;
#else
		tert_mi2s_clk.clk_set_mode = Q6AFE_LPASS_MODE_CLK1_VALID;
#endif

		ret = afe_set_lpass_clock(AFE_PORT_ID_TERTIARY_MI2S_RX,	&tert_mi2s_clk);
		if (ret < 0) {
			pr_err("%s: afe lpass clock failed, err:%d\n", __func__, ret);
			goto err;
		}

		ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_CBS_CFS);
		if (ret < 0) pr_err("%s: set fmt cpu dai failed, err:%d\n", __func__, ret);
#else
		tert_mi2s_clk.clk_set_mode = Q6AFE_LPASS_MODE_CLK1_VALID;
		tert_mi2s_clk.clk_src = Q6AFE_LPASS_CLK_SRC_EXTERNAL;
		ret = afe_set_lpass_clock(AFE_PORT_ID_TERTIARY_MI2S_RX,	&tert_mi2s_clk);
		if (ret < 0) {
			pr_err("%s: afe lpass clock failed, err:%d\n", __func__, ret);
			goto err;
		}

		ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_CBM_CFM);
		if (ret < 0) pr_err("%s: set fmt cpu dai failed, err:%d\n", __func__, ret);


#endif
	}


err:
	return ret;
}



static void msm8994_tert_mi2s_snd_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	struct msm_pinctrl_info *pinctrl_info = &pdata->tert_mi2s_pinctrl_info;
	int ret = 0;

	pr_info("%s: dai name %s %pK  substream = %s  stream = %d  \n", __func__, cpu_dai->name, cpu_dai->dev,substream->name, substream->stream);
	if (atomic_dec_return(&tert_mi2s_rsc_ref) == 0) {

		tert_mi2s_clk.clk_val1 = Q6AFE_LPASS_IBIT_CLK_DISABLE;
		tert_mi2s_clk.clk_val2 = Q6AFE_LPASS_OSR_CLK_DISABLE;
		tert_mi2s_clk.clk_src =Q6AFE_LPASS_CLK_SRC_INTERNAL;
		tert_mi2s_clk.clk_set_mode = Q6AFE_LPASS_MODE_CLK1_VALID;
		ret = afe_set_lpass_clock(AFE_PORT_ID_TERTIARY_MI2S_RX,	&tert_mi2s_clk);
		if (ret < 0) pr_err("%s: afe lpass clock failed, err:%d\n", __func__, ret);

		ret = msm_reset_pinctrl(pinctrl_info);
		if (ret) pr_err("%s: Reset pinctrl failed with %d\n", __func__, ret);
		pr_debug("%s Tertiary MI2S Clock is Disabled", __func__);
	}
}


static int msm8994_quat_mi2s_snd_startup(struct snd_pcm_substream *substream)
{
	int ret = 0;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	struct msm_pinctrl_info *pinctrl_info = &pdata->quat_mi2s_pinctrl_info;

	pr_info("%s: dai name %s %pK  substream = %s  stream = %d bit width =%d sample rate =%d  \n", __func__, cpu_dai->name, cpu_dai->dev,substream->name,
			substream->stream, quat_mi2s_bit_format, quat_mi2s_sample_rate);

	if (atomic_inc_return(&quat_mi2s_rsc_ref) == 1) {

		if (pinctrl_info == NULL) {
			pr_err("%s: pinctrl_info is NULL\n", __func__);
			ret = -EINVAL;
			goto err;
		}
		if (pinctrl_info->mux != NULL)
			iowrite32(I2S_PCM_SEL_I2S << I2S_PCM_SEL_OFFSET, pinctrl_info->mux);
		else pr_err("%s: MI2S muxsel addr is NULL\n", __func__);

		ret = msm_set_pinctrl(pinctrl_info);
		if (ret) {
			pr_err("%s: MI2S TLMM pinctrl set failed with %d\n", __func__, ret);
		return ret;
		}
		if(quat_mi2s_bit_format==SNDRV_PCM_FORMAT_S24_LE)
		{
			switch(quat_mi2s_sample_rate) {
				case SAMPLING_RATE_192KHZ :
					quat_mi2s_clk.clk_val1 = Q6AFE_LPASS_IBIT_CLK_12_P288_MHZ;
					break;
				case SAMPLING_RATE_96KHZ :
					quat_mi2s_clk.clk_val1 = Q6AFE_LPASS_IBIT_CLK_6_P144_MHZ;
					break;
				case SAMPLING_RATE_48KHZ :
				default:
					quat_mi2s_clk.clk_val1 = Q6AFE_LPASS_IBIT_CLK_3_P072_MHZ;
					break;
			}
		} else {
			switch(quat_mi2s_sample_rate) {
				case SAMPLING_RATE_192KHZ :
					quat_mi2s_clk.clk_val1 = Q6AFE_LPASS_IBIT_CLK_6_P144_MHZ;
					break;
				case SAMPLING_RATE_96KHZ :
					quat_mi2s_clk.clk_val1 = Q6AFE_LPASS_IBIT_CLK_3_P072_MHZ;
					break;
				case SAMPLING_RATE_48KHZ :
				default:
					quat_mi2s_clk.clk_val1 = Q6AFE_LPASS_IBIT_CLK_1_P536_MHZ;
					break;
			}
		}
#ifdef MSM_QUAT_MI2S_MASTER
#ifdef MSM_QUAT_MI2S_MCLK
		quat_mi2s_clk.clk_val2 = Q6AFE_LPASS_OSR_CLK_12_P288_MHZ;
		quat_mi2s_clk.clk_set_mode = Q6AFE_LPASS_MODE_BOTH_VALID;
#else
		quat_mi2s_clk.clk_set_mode = Q6AFE_LPASS_MODE_CLK1_VALID;
#endif
		quat_mi2s_clk.clk_src =Q6AFE_LPASS_CLK_SRC_INTERNAL;
		ret = afe_set_lpass_clock(AFE_PORT_ID_QUATERNARY_MI2S_RX, &quat_mi2s_clk);
		if (ret < 0) {
			pr_err("%s: afe lpass clock failed, err:%d\n", __func__, ret);
			goto err;
		}

		ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_CBS_CFS);
		if (ret < 0) pr_err("%s: set fmt cpu dai failed, err:%d\n", __func__, ret);
#else
		quat_mi2s_clk.clk_set_mode = Q6AFE_LPASS_MODE_CLK1_VALID;
                quat_mi2s_clk.clk_src =Q6AFE_LPASS_CLK_SRC_EXTERNAL;
		ret = afe_set_lpass_clock(AFE_PORT_ID_QUATERNARY_MI2S_RX, &quat_mi2s_clk);
		if (ret < 0) {
			pr_err("%s: afe lpass clock failed, err:%d\n", __func__, ret);
			goto err;
		}

		ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_CBS_CFS);
		if (ret < 0) pr_err("%s: set fmt cpu dai failed, err:%d\n", __func__, ret);
#endif
	}


err:
	return ret;
}



static void msm8994_quat_mi2s_snd_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	struct msm_pinctrl_info *pinctrl_info = &pdata->quat_mi2s_pinctrl_info;
	int ret = 0;

	pr_info("%s: dai name %s %pK  substream = %s  stream = %d  \n", __func__, cpu_dai->name, cpu_dai->dev,substream->name, substream->stream);
	if (atomic_dec_return(&quat_mi2s_rsc_ref) == 0) {

		quat_mi2s_clk.clk_val1 = Q6AFE_LPASS_IBIT_CLK_DISABLE;
		quat_mi2s_clk.clk_val2 = Q6AFE_LPASS_OSR_CLK_DISABLE;
		quat_mi2s_clk.clk_set_mode = Q6AFE_LPASS_MODE_CLK1_VALID;
		quat_mi2s_clk.clk_src =Q6AFE_LPASS_CLK_SRC_INTERNAL;
		ret = afe_set_lpass_clock(AFE_PORT_ID_QUATERNARY_MI2S_RX,	&quat_mi2s_clk);
		if (ret < 0) pr_err("%s: afe lpass clock failed, err:%d\n", __func__, ret);

		ret = msm_reset_pinctrl(pinctrl_info);
		if (ret) pr_err("%s: Reset pinctrl failed with %d\n", __func__, ret);
		pr_debug("%s Quaternary MI2S Clock is Disabled", __func__);
	}
}


static int msm_be_tert_mi2s_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
	SNDRV_PCM_HW_PARAM_RATE);

	struct snd_interval *channels = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_CHANNELS);
	param_set_mask(params, SNDRV_PCM_HW_PARAM_FORMAT,
			   tert_mi2s_bit_format);
	rate->min = rate->max = tert_mi2s_sample_rate;
	channels->min = channels->max =2;
	pr_debug("%s Tertiary MI2S Sample Rate =%d, bit Format = %d \n", __func__, tert_mi2s_sample_rate, tert_mi2s_bit_format);      
	return 0;
}

static int msm_be_quat_mi2s_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
	SNDRV_PCM_HW_PARAM_RATE);

	struct snd_interval *channels = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_CHANNELS);
	param_set_mask(params, SNDRV_PCM_HW_PARAM_FORMAT,
			   quat_mi2s_bit_format);

	rate->min = rate->max = quat_mi2s_sample_rate;
	channels->min = channels->max =2;
	pr_debug("%s Quat MI2S Sample Rate =%d, bit Format = %d \n", __func__, quat_mi2s_sample_rate, quat_mi2s_bit_format);      
	return 0;
}

static struct snd_soc_ops msm8994_tert_mi2s_be_ops = {
	.startup = msm8994_tert_mi2s_snd_startup,
	.shutdown = msm8994_tert_mi2s_snd_shutdown,
};
static struct snd_soc_ops msm8994_quat_mi2s_be_ops = {
	.startup = msm8994_quat_mi2s_snd_startup,
	.shutdown = msm8994_quat_mi2s_snd_shutdown,
};

static struct snd_soc_ops msm8994_mi2s_be_ops = {
	.startup = msm8994_mi2s_snd_startup,
	.shutdown = msm8994_mi2s_snd_shutdown,
};

static int msm_slim_0_rx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
	SNDRV_PCM_HW_PARAM_RATE);

	struct snd_interval *channels =
	    hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);

	pr_debug("%s()\n", __func__);
	param_set_mask(params, SNDRV_PCM_HW_PARAM_FORMAT,
				   slim0_rx_bit_format);
//htc audio ++
        if(htc_acoustic_query_feature(HTC_AUD_24BIT)) {
                param_set_mask(params, SNDRV_PCM_HW_PARAM_FORMAT,
                        SNDRV_PCM_FORMAT_S24_LE);
        }
//htc audio --
	rate->min = rate->max = slim0_rx_sample_rate;
	channels->min = channels->max = msm_slim_0_rx_ch;

	 pr_debug("%s: format = %d, rate = %d, channels = %d\n",
			  __func__, params_format(params), params_rate(params),
			  msm_slim_0_rx_ch);

	return 0;
}

static int msm_slim_0_tx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
	SNDRV_PCM_HW_PARAM_RATE);

	struct snd_interval *channels = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_CHANNELS);

	pr_debug("%s()\n", __func__);
	param_set_mask(params, SNDRV_PCM_HW_PARAM_FORMAT,
				   slim0_tx_bit_format);
	rate->min = rate->max = slim0_tx_sample_rate;
	channels->min = channels->max = msm_slim_0_tx_ch;

	return 0;
}

static int msm_slim_4_tx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
	SNDRV_PCM_HW_PARAM_RATE);

	struct snd_interval *channels = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_CHANNELS);

	rate->min = rate->max = 48000;
	channels->min = channels->max = msm_vi_feed_tx_ch;
	pr_debug("%s: %d\n", __func__, msm_vi_feed_tx_ch);
	return 0;
}

static int msm_slim_5_tx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					    struct snd_pcm_hw_params *params)
{
	void *config;
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_interval *rate =
	    hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels =
	    hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	int rc = 0;

	pr_debug("%s: enter\n", __func__);
	rate->min = rate->max = 16000;
	channels->min = channels->max = 1;

	config = tomtom_get_afe_config(codec, AFE_SLIMBUS_SLAVE_PORT_CONFIG);
	rc = afe_set_config(AFE_SLIMBUS_SLAVE_PORT_CONFIG, config,
			    SLIMBUS_5_TX);
	if (rc) {
		pr_err("%s: Failed to set slimbus slave port config %d\n",
		       __func__, rc);
		return rc;
	}

	return rc;
}

static int msm_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				  struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);

	pr_debug("%s:\n", __func__);
	rate->min = rate->max = 48000;
	return 0;
}

static int msm_be_fm_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);

	struct snd_interval *channels =
	    hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);

	pr_debug("%s:\n", __func__);
	rate->min = rate->max = 48000;
	channels->min = channels->max = 2;

	return 0;
}

static const struct soc_enum msm_snd_enum[] = {
	SOC_ENUM_SINGLE_EXT(2, spk_function),
	SOC_ENUM_SINGLE_EXT(2, slim0_rx_ch_text),
	SOC_ENUM_SINGLE_EXT(8, slim0_tx_ch_text),
	SOC_ENUM_SINGLE_EXT(7, hdmi_rx_ch_text),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(rx_bit_format_text), rx_bit_format_text),
	SOC_ENUM_SINGLE_EXT(3, slim0_rx_sample_rate_text),
	SOC_ENUM_SINGLE_EXT(8, proxy_rx_ch_text),
	SOC_ENUM_SINGLE_EXT(3, hdmi_rx_sample_rate_text),
	SOC_ENUM_SINGLE_EXT(2, vi_feed_ch_text),
};

static const struct snd_kcontrol_new msm_snd_controls[] = {
	SOC_ENUM_EXT("Speaker Function", msm_snd_enum[0], msm8994_get_spk,
			msm8994_set_spk),
	SOC_ENUM_EXT("SLIM_0_RX Channels", msm_snd_enum[1],
			msm_slim_0_rx_ch_get, msm_slim_0_rx_ch_put),
	SOC_ENUM_EXT("SLIM_0_TX Channels", msm_snd_enum[2],
			msm_slim_0_tx_ch_get, msm_slim_0_tx_ch_put),
	SOC_ENUM_EXT("VI_FEED_TX Channels", msm_snd_enum[8],
			msm_vi_feed_tx_ch_get, msm_vi_feed_tx_ch_put),
	SOC_ENUM_EXT("AUX PCM SampleRate", msm8994_auxpcm_enum[0],
			msm8994_auxpcm_rate_get, msm8994_auxpcm_rate_put),
	SOC_ENUM_EXT("HDMI_RX Channels", msm_snd_enum[3],
			msm_hdmi_rx_ch_get, msm_hdmi_rx_ch_put),
	SOC_ENUM_EXT("SLIM_0_RX Format", msm_snd_enum[4],
			slim0_rx_bit_format_get, slim0_rx_bit_format_put),
	SOC_ENUM_EXT("SLIM_0_RX SampleRate", msm_snd_enum[5],
			slim0_rx_sample_rate_get, slim0_rx_sample_rate_put),
	SOC_ENUM_EXT("HDMI_RX Bit Format", msm_snd_enum[4],
			hdmi_rx_bit_format_get, hdmi_rx_bit_format_put),
	SOC_ENUM_EXT("PROXY_RX Channels", msm_snd_enum[6],
			msm_proxy_rx_ch_get, msm_proxy_rx_ch_put),
	SOC_ENUM_EXT("Internal BTSCO SampleRate", msm_btsco_enum[0],
		     msm_btsco_rate_get, msm_btsco_rate_put),
	SOC_ENUM_EXT("HDMI_RX SampleRate", msm_snd_enum[7],
			hdmi_rx_sample_rate_get, hdmi_rx_sample_rate_put),
	SOC_ENUM_EXT("TERT_MI2S BitWidth", msm_snd_enum[4],
			tert_mi2s_bit_format_get, tert_mi2s_bit_format_put),
	SOC_ENUM_EXT("TERT_MI2S SampleRate", msm_snd_enum[5],
			tert_mi2s_sample_rate_get, tert_mi2s_sample_rate_put),
	SOC_ENUM_EXT("QUAT_MI2S BitWidth", msm_snd_enum[4],
			quat_mi2s_bit_format_get, quat_mi2s_bit_format_put),
	SOC_ENUM_EXT("QUAT_MI2S SampleRate", msm_snd_enum[5],
			quat_mi2s_sample_rate_get, quat_mi2s_sample_rate_put),


	SOC_ENUM_EXT("SLIM_0_TX Format", msm_snd_enum[4],
			slim0_tx_bit_format_get, slim0_tx_bit_format_put),
	SOC_ENUM_EXT("SLIM_0_TX SampleRate", msm_snd_enum[5],
			slim0_tx_sample_rate_get, slim0_tx_sample_rate_put),

};
#ifdef CONFIG_USE_CODEC_MBHC //htc_audio
//htc_audio: we don't use below
static bool msm8994_swap_gnd_mic(struct snd_soc_codec *codec)
{
	struct snd_soc_card *card = codec->card;
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	int value = gpio_get_value_cansleep(pdata->us_euro_gpio);
	pr_info("%s: swap select switch %d to %d\n", __func__, value, !value);
	gpio_set_value_cansleep(pdata->us_euro_gpio, !value);
	return true;
}
#endif
static int msm_afe_set_config(struct snd_soc_codec *codec)
{
	int rc;
	void *config_data;

	pr_debug("%s: enter\n", __func__);
	config_data = tomtom_get_afe_config(codec, AFE_CDC_REGISTERS_CONFIG);
	rc = afe_set_config(AFE_CDC_REGISTERS_CONFIG, config_data, 0);
	if (rc) {
		pr_err("%s: Failed to set codec registers config %d\n",
		       __func__, rc);
		return rc;
	}

	config_data = tomtom_get_afe_config(codec, AFE_SLIMBUS_SLAVE_CONFIG);
	rc = afe_set_config(AFE_SLIMBUS_SLAVE_CONFIG, config_data, 0);
	if (rc) {
		pr_err("%s: Failed to set slimbus slave config %d\n", __func__,
		       rc);
		return rc;
	}

	return 0;
}

static void msm_afe_clear_config(void)
{
	afe_clear_config(AFE_CDC_REGISTERS_CONFIG);
	afe_clear_config(AFE_SLIMBUS_SLAVE_CONFIG);
}

static int  msm8994_adsp_state_callback(struct notifier_block *nb,
		unsigned long value, void *priv)
{
	if (value == SUBSYS_BEFORE_SHUTDOWN) {
		pr_debug("%s: ADSP is about to shutdown. Clearing AFE config\n",
			 __func__);
		msm_afe_clear_config();
	} else if (value == SUBSYS_AFTER_POWERUP) {
		pr_debug("%s: ADSP is up\n", __func__);
	}

	return NOTIFY_OK;
}

static struct notifier_block adsp_state_notifier_block = {
	.notifier_call = msm8994_adsp_state_callback,
	.priority = -INT_MAX,
};

static int msm8994_wcd93xx_codec_up(struct snd_soc_codec *codec)
{
	int err;
	unsigned long timeout;
	int adsp_ready = 0;

	timeout = jiffies +
		msecs_to_jiffies(ADSP_STATE_READY_TIMEOUT_MS);

	do {
		if (!q6core_is_adsp_ready()) {
			pr_err("%s: ADSP Audio isn't ready\n", __func__);
		} else {
			pr_debug("%s: ADSP Audio is ready\n", __func__);
			adsp_ready = 1;
			break;
		}
	} while (time_after(timeout, jiffies));

	if (!adsp_ready) {
		pr_err("%s: timed out waiting for ADSP Audio\n", __func__);
		return -ETIMEDOUT;
	}

	err = msm_afe_set_config(codec);
	if (err)
		pr_err("%s: Failed to set AFE config. err %d\n",
				__func__, err);
	return err;
}

static int msm8994_codec_event_cb(struct snd_soc_codec *codec,
		enum wcd9xxx_codec_event codec_event)
{
	switch (codec_event) {
	case WCD9XXX_CODEC_EVENT_CODEC_UP:
		return msm8994_wcd93xx_codec_up(codec);
	default:
		pr_err("%s: UnSupported codec event %d\n",
				__func__, codec_event);
		return -EINVAL;
	}
}

static int msm_snd_get_ext_clk_cnt(void)
{
	return clk_users;
}

//htc audio ++
static int msm_rcv_amp_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	mutex_lock(&htc_amp_mutex);
	ucontrol->value.integer.value[0] = (htc_amp_mask & HTC_RCV_AMP)?1:0;
	mutex_unlock(&htc_amp_mutex);
	return 0;
}

static int msm_hs_amp_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	mutex_lock(&htc_amp_mutex);
	ucontrol->value.integer.value[0] = (htc_amp_mask & HTC_HS_AMP)?1:0;
	mutex_unlock(&htc_amp_mutex);
	return 0;
}

static int msm_audiosphere_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

static int msm_speaker_reverse_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

static int msm_rcv_amp_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	mutex_lock(&htc_amp_mutex);

	if (ucontrol->value.integer.value[0])
		htc_amp_mask |= HTC_RCV_AMP;
	else
		htc_amp_mask &= ~HTC_RCV_AMP;

	htc_amp_control(htc_amp_mask,msm8994_ext_spk_pamp);
	mutex_unlock(&htc_amp_mutex);
	return 1;
}

static int msm_hs_amp_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	mutex_lock(&htc_amp_mutex);

	if (ucontrol->value.integer.value[0])
		htc_amp_mask |= HTC_HS_AMP;
	else
		htc_amp_mask &= ~HTC_HS_AMP;

	htc_amp_control(htc_amp_mask,msm8994_ext_spk_pamp);

	mutex_unlock(&htc_amp_mutex);
	return 1;
}

static int htc_set_surround_effect(enum HTC_ADM_EFFECT_ID effect_id, uint32_t module_id, uint32_t param_id, uint32_t value)
{
	int rc;
	char *params_value;
	int *update_params_value;
	uint32_t params_length =  12 +sizeof(uint32_t);

	pr_info("%s: module id = 0x%x, param id = 0x%x, value = %d\n",
		__func__, module_id, param_id, value);

	params_value = kzalloc(params_length, GFP_KERNEL);
	if (!params_value) {
		pr_err("%s, params memory alloc failed", __func__);
		return -ENOMEM;
	}
	update_params_value = (int *)params_value;
	*update_params_value++ = module_id;
	*update_params_value++ = param_id;
	*update_params_value++ = sizeof(uint32_t);
	*update_params_value = value;

	rc = htc_adm_effect_control(effect_id,
				AFE_PORT_ID_QUATERNARY_MI2S_RX,
				AFE_COPP_ID_AUDIO_SPHERE,
				param_id,
				params_length,
				params_value);
	if(rc)
		pr_err("%s: call q6adm_enable_effect to port 0x%x, rc %d\n",
			__func__,AFE_PORT_ID_QUATERNARY_MI2S_RX,rc);

	kfree(params_value);

	return rc;
}

static int msm_audiosphere_enable_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	unsigned int enable_flag = 0;
	int rc;

	mutex_lock(&htc_audiosphere_enable_mutex);
	pr_debug("%s\n", __func__);

	if ((ucontrol->value.integer.value[0] == 0) || (ucontrol->value.integer.value[0] == 1))
		enable_flag = ucontrol->value.integer.value[0];
	else {
		mutex_unlock(&htc_audiosphere_enable_mutex);
		return 0;
	}

	rc = htc_set_surround_effect(HTC_ADM_EFFECT_AUDIOSPHERE, AFE_MODULE_AUDIO_SPHERE, AFE_PARAM_ID_AUDIO_SPHERE_EN, enable_flag);

	mutex_unlock(&htc_audiosphere_enable_mutex);
	return 1;
}

static int msm_audiosphere_strength_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	unsigned int strength = 0;
	int rc;

	mutex_lock(&htc_audiosphere_strength_mutex);
	pr_debug("%s\n", __func__);

	if ((ucontrol->value.integer.value[0] >= 0) && ((ucontrol->value.integer.value[0] <= 1000)))
		strength = ucontrol->value.integer.value[0];
	else {
		mutex_unlock(&htc_audiosphere_strength_mutex);
		return 0;
	}

	rc = htc_set_surround_effect(HTC_ADM_EFFECT_AUDIOSPHERE_ST, AFE_MODULE_AUDIO_SPHERE, AFE_PARAM_ID_AUDIO_SPHERE_ST, strength);

	mutex_unlock(&htc_audiosphere_strength_mutex);
	return 1;
}

static int msm_audiospherelimit_enable_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	unsigned int enable_flag = 0;
	int rc;

	mutex_lock(&htc_audiosphere_enable_mutex);
	pr_debug("%s\n", __func__);

	if ((ucontrol->value.integer.value[0] == 0) || (ucontrol->value.integer.value[0] == 1))
		enable_flag = ucontrol->value.integer.value[0];
	else {
		mutex_unlock(&htc_audiosphere_enable_mutex);
		return 0;
	}

	rc = htc_set_surround_effect(HTC_ADM_EFFECT_AUDIOSPHERE, AFE_MODULE_AUDIO_SPHERE, AFE_PARAM_ID_AUDIO_SPHERE_EN, enable_flag);
	if(rc) {
		mutex_unlock(&htc_audiosphere_enable_mutex);
		return 0;
	}else
		rc = htc_set_surround_effect(HTC_ADM_EFFECT_LIMITER, AFE_MODULE_AUDIO_LIMITER, AFE_PARAM_ID_AUDIO_LIMITER_EN, enable_flag);

	mutex_unlock(&htc_audiosphere_enable_mutex);
	return 1;
}

static int msm_speaker_reverse_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	unsigned int enable_flag = 0;
	int rc;

	mutex_lock(&htc_audiosphere_enable_mutex);
	pr_debug("%s\n", __func__);

	if ((ucontrol->value.integer.value[0] == 0) || (ucontrol->value.integer.value[0] == 1))
		enable_flag = ucontrol->value.integer.value[0];
	else {
		mutex_unlock(&htc_audiosphere_enable_mutex);
		return 0;
	}

	rc = htc_set_surround_effect(HTC_ADM_EFFECT_SPEAKER_REVERSE, AFE_MODULE_SPEAKER_REVERSE, AFE_PARAM_ID_SPEAKER_REVERSE_EN, enable_flag);

	mutex_unlock(&htc_audiosphere_enable_mutex);
	return 1;
}

static int htc_mi2s_amp_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *k, int event)
{
	pr_debug("%s() %s\n", __func__,w->name);

	if (SND_SOC_DAPM_EVENT_ON(event)) {
		pr_info("%s:mi2s amp on\n",__func__);
		htc_acoustic_spk_amp_ctrl(SPK_AMP_RIGHT,1, 0);
		htc_acoustic_spk_amp_ctrl(SPK_AMP_LEFT,1, 0);
	} else {
		pr_info("%s:mi2s amp off\n",__func__);
		htc_acoustic_spk_amp_ctrl(SPK_AMP_RIGHT,0, 0);
		htc_acoustic_spk_amp_ctrl(SPK_AMP_LEFT,0, 0);
	}

	return 0;

}

static const struct snd_kcontrol_new htc_amp_siwth_control[] = {
	SOC_SINGLE_EXT("RCV AMP EN Switch", SND_SOC_NOPM,
	0, 1, 0, msm_rcv_amp_get,msm_rcv_amp_put),

	SOC_SINGLE_EXT("HS AMP EN Switch", SND_SOC_NOPM,
	0, 1, 0, msm_hs_amp_get,msm_hs_amp_put),
};

static const struct snd_kcontrol_new htc_audiosphere_params_control[] = {
	SOC_SINGLE_EXT("Audiosphere Enable", SND_SOC_NOPM,
	0, 1, 0, msm_audiosphere_get,msm_audiosphere_enable_put),

	SOC_SINGLE_EXT("Audiosphere Strength", SND_SOC_NOPM,
	0, 1000, 0, msm_audiosphere_get,msm_audiosphere_strength_put),

	SOC_SINGLE_EXT("Audiospherelimit Enable", SND_SOC_NOPM,
	0, 1, 0, msm_audiosphere_get,msm_audiospherelimit_enable_put),
};

static const struct snd_kcontrol_new htc_speaker_reverse_params_control[] = {
	SOC_SINGLE_EXT("Speaker Reverse Enable", SND_SOC_NOPM,
	0, 1, 0, msm_speaker_reverse_get,msm_speaker_reverse_put),
};

static const struct snd_soc_dapm_widget htc_quat_mi2s_widget[] = {

	SND_SOC_DAPM_AIF_IN_E("HTC VIRTUAL MI2S", "htc-virtual-mi2s-if", 0, SND_SOC_NOPM,
				0, 0, htc_mi2s_amp_event,
				SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SPK("HTC_MI2S_OUT", NULL),
};

static const struct snd_soc_dapm_route htc_quat_mi2s_virtual_route[] = {
	/* SLIMBUS Connections */
	{"HTC_MI2S_OUT", NULL, "HTC VIRTUAL MI2S"},
};

static int msm_htc_quat_mi2s_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	pr_info("%s: ++\n",__func__);
	snd_soc_dapm_new_controls(dapm, htc_quat_mi2s_widget,
				  ARRAY_SIZE(htc_quat_mi2s_widget));

	snd_soc_dapm_add_routes(dapm, htc_quat_mi2s_virtual_route,
		ARRAY_SIZE(htc_quat_mi2s_virtual_route));
	pr_info("%s: --\n",__func__);

	return 0;
}
//htc audio --
static int msm_audrx_init(struct snd_soc_pcm_runtime *rtd)
{
	int err;
	void *config_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;

	/* Codec SLIMBUS configuration
	 * RX1, RX2, RX3, RX4, RX5, RX6, RX7, RX8, RX9, RX10, RX11, RX12, RX13
	 * TX1, TX2, TX3, TX4, TX5, TX6, TX7, TX8, TX9, TX10, TX11, TX12, TX13
	 * TX14, TX15, TX16
	 */
	unsigned int rx_ch[TOMTOM_RX_MAX] = {144, 145, 146, 147, 148, 149, 150,
					    151, 152, 153, 154, 155, 156};
	unsigned int tx_ch[TOMTOM_TX_MAX]  = {128, 129, 130, 131, 132, 133,
					     134, 135, 136, 137, 138, 139,
					     140, 141, 142, 143};

	pr_info("%s: dev_name%s\n", __func__, dev_name(cpu_dai->dev));

	rtd->pmdown_time = 0;

	err = snd_soc_add_codec_controls(codec, msm_snd_controls,
					 ARRAY_SIZE(msm_snd_controls));
	if (err < 0) {
		pr_err("%s: add_codec_controls failed, err%d\n",
			__func__, err);
		return err;
	}

	err = msm8994_liquid_init_docking();
	if (err) {
		pr_err("%s: 8994 init Docking stat IRQ failed (%d)\n",
			   __func__, err);
		return err;
	}

//htc audio ++
	err = snd_soc_add_codec_controls(codec, htc_amp_siwth_control,
					 ARRAY_SIZE(htc_amp_siwth_control));
	if (err < 0)
		return err;

	err = snd_soc_add_codec_controls(codec, htc_audiosphere_params_control,
					 ARRAY_SIZE(htc_audiosphere_params_control));
	if (err < 0)
		return err;

	err = snd_soc_add_codec_controls(codec, htc_speaker_reverse_params_control,
					 ARRAY_SIZE(htc_speaker_reverse_params_control));
	if (err < 0)
		return err;
//htc audio --

	err = msm8994_ext_us_amp_init();
	if (err) {
		pr_err("%s: MTP 8994 US Emitter GPIO init failed (%d)\n",
			__func__, err);
		// htc audio: When feature not supported and GPIO removed in DT,
		//            Do not report error here.
		//return err;
	}

	snd_soc_dapm_new_controls(dapm, msm8994_dapm_widgets,
				ARRAY_SIZE(msm8994_dapm_widgets));

	snd_soc_dapm_enable_pin(dapm, "Lineout_1 amp");
	snd_soc_dapm_enable_pin(dapm, "Lineout_3 amp");
	snd_soc_dapm_enable_pin(dapm, "Lineout_2 amp");
	snd_soc_dapm_enable_pin(dapm, "Lineout_4 amp");

	snd_soc_dapm_ignore_suspend(dapm, "Lineout_1 amp");
	snd_soc_dapm_ignore_suspend(dapm, "Lineout_3 amp");
	snd_soc_dapm_ignore_suspend(dapm, "Lineout_2 amp");
	snd_soc_dapm_ignore_suspend(dapm, "Lineout_4 amp");
	snd_soc_dapm_ignore_suspend(dapm, "ultrasound amp");
	snd_soc_dapm_ignore_suspend(dapm, "Handset Mic");
	snd_soc_dapm_ignore_suspend(dapm, "Headset Mic");
	snd_soc_dapm_ignore_suspend(dapm, "ANCRight Headset Mic");
	snd_soc_dapm_ignore_suspend(dapm, "ANCLeft Headset Mic");
	snd_soc_dapm_ignore_suspend(dapm, "Digital Mic1");
	snd_soc_dapm_ignore_suspend(dapm, "Digital Mic2");
	snd_soc_dapm_ignore_suspend(dapm, "Digital Mic3");
	snd_soc_dapm_ignore_suspend(dapm, "Digital Mic4");
	snd_soc_dapm_ignore_suspend(dapm, "Digital Mic5");
	snd_soc_dapm_ignore_suspend(dapm, "Digital Mic6");

	snd_soc_dapm_ignore_suspend(dapm, "MADINPUT");
	snd_soc_dapm_ignore_suspend(dapm, "MAD_CPE_INPUT");
	snd_soc_dapm_ignore_suspend(dapm, "AIF4 MAD");
	snd_soc_dapm_ignore_suspend(dapm, "EAR");
	snd_soc_dapm_ignore_suspend(dapm, "HEADPHONE");
	snd_soc_dapm_ignore_suspend(dapm, "LINEOUT1");
	snd_soc_dapm_ignore_suspend(dapm, "LINEOUT2");
	snd_soc_dapm_ignore_suspend(dapm, "LINEOUT3");
	snd_soc_dapm_ignore_suspend(dapm, "LINEOUT4");
	snd_soc_dapm_ignore_suspend(dapm, "SPK_OUT");
	snd_soc_dapm_ignore_suspend(dapm, "ANC HEADPHONE");
	snd_soc_dapm_ignore_suspend(dapm, "ANC EAR");
	snd_soc_dapm_ignore_suspend(dapm, "AMIC1");
	snd_soc_dapm_ignore_suspend(dapm, "AMIC2");
	snd_soc_dapm_ignore_suspend(dapm, "AMIC3");
	snd_soc_dapm_ignore_suspend(dapm, "AMIC4");
	snd_soc_dapm_ignore_suspend(dapm, "AMIC5");
	snd_soc_dapm_ignore_suspend(dapm, "AMIC6");
	snd_soc_dapm_ignore_suspend(dapm, "DMIC1");
	snd_soc_dapm_ignore_suspend(dapm, "DMIC2");
	snd_soc_dapm_ignore_suspend(dapm, "DMIC3");
	snd_soc_dapm_ignore_suspend(dapm, "DMIC4");
	snd_soc_dapm_ignore_suspend(dapm, "DMIC5");
	snd_soc_dapm_ignore_suspend(dapm, "DMIC6");
	snd_soc_dapm_ignore_suspend(dapm, "AIF4 VI");
	snd_soc_dapm_ignore_suspend(dapm, "VIINPUT");

	snd_soc_dapm_sync(dapm);

	codec_clk = clk_get(&spdev->dev, "osr_clk");
	if (IS_ERR(codec_clk)) {
		pr_err("%s: error clk_get %lu\n",
			__func__, PTR_ERR(codec_clk));
		return -EINVAL;
	}

	snd_soc_dai_set_channel_map(codec_dai, ARRAY_SIZE(tx_ch),
				    tx_ch, ARRAY_SIZE(rx_ch), rx_ch);

	err = msm_afe_set_config(codec);
	if (err) {
		pr_err("%s: Failed to set AFE config %d\n", __func__, err);
		goto out;
	}

	config_data = tomtom_get_afe_config(codec, AFE_AANC_VERSION);
	err = afe_set_config(AFE_AANC_VERSION, config_data, 0);
	if (err) {
		pr_err("%s: Failed to set aanc version %d\n",
			__func__, err);
		goto out;
	}
	config_data = tomtom_get_afe_config(codec,
				AFE_CDC_CLIP_REGISTERS_CONFIG);
	if (config_data) {
		err = afe_set_config(AFE_CDC_CLIP_REGISTERS_CONFIG,
					config_data, 0);
		if (err) {
			pr_err("%s: Failed to set clip registers %d\n",
				__func__, err);
			goto out;
		}
	}
	config_data = tomtom_get_afe_config(codec, AFE_CLIP_BANK_SEL);
	if (config_data) {
		err = afe_set_config(AFE_CLIP_BANK_SEL, config_data, 0);
		if (err) {
			pr_err("%s: Failed to set AFE bank selection %d\n",
				__func__, err);
			goto out;
		}
	}
//htc audio ++
#ifdef CONFIG_USE_CODEC_MBHC
	/* start mbhc */
	mbhc_cfg.calibration = def_codec_mbhc_cal();
	if (mbhc_cfg.calibration) {
		err = tomtom_hs_detect(codec, &mbhc_cfg);
		if (err) {
			pr_err("%s: tomtom_hs_detect failed, err:%d\n",
				__func__, err);
			goto out;
		}
	} else {
		pr_err("%s: mbhc_cfg calibration is NULL\n", __func__);
		err = -ENOMEM;
		goto out;
	}
#endif
	adsp_state_notifier =
	    subsys_notif_register_notifier("adsp",
					   &adsp_state_notifier_block);
	if (!adsp_state_notifier) {
		pr_err("%s: Failed to register adsp state notifier\n",
		       __func__);
		err = -EFAULT;
#ifdef CONFIG_USE_CODEC_MBHC //htc_audio
		tomtom_hs_detect_exit(codec);
#endif
		goto out;
	}

	tomtom_event_register(msm8994_codec_event_cb, rtd->codec);
	tomtom_register_ext_clk_cb(msm_snd_enable_codec_ext_clk,
				   msm_snd_get_ext_clk_cnt,
				   rtd->codec);

	err = msm_snd_enable_codec_ext_clk(rtd->codec, 1, false);
	if (IS_ERR_VALUE(err)) {
		pr_err("%s: Failed to enable mclk, err = 0x%x\n",
			__func__, err);
		goto out;
	}
	tomtom_enable_qfuse_sensing(rtd->codec);
	err = msm_snd_enable_codec_ext_clk(rtd->codec, 0, false);
	if (IS_ERR_VALUE(err)) {
		pr_err("%s: Failed to disable mclk, err = 0x%x\n",
			__func__, err);
		goto out;
	}

	return 0;
out:
	clk_put(codec_clk);
	return err;
}

//htc audio ++
#ifdef CONFIG_USE_CODEC_MBHC
static void *def_codec_mbhc_cal(void)
{
	void *codec_cal;
	struct wcd9xxx_mbhc_btn_detect_cfg *btn_cfg;
	u16 *btn_low, *btn_high;
	u8 *n_ready, *n_cic, *gain;

	codec_cal = kzalloc(WCD9XXX_MBHC_CAL_SIZE(WCD9XXX_MBHC_DEF_BUTTONS,
						WCD9XXX_MBHC_DEF_RLOADS),
			    GFP_KERNEL);
	if (!codec_cal) {
		pr_err("%s: out of memory\n", __func__);
		return NULL;
	}

#define S(X, Y) ((WCD9XXX_MBHC_CAL_GENERAL_PTR(codec_cal)->X) = (Y))
	S(t_ldoh, 100);
	S(t_bg_fast_settle, 100);
	S(t_shutdown_plug_rem, 255);
	S(mbhc_nsa, 4);
	S(mbhc_navg, 4);
#undef S
#define S(X, Y) ((WCD9XXX_MBHC_CAL_PLUG_DET_PTR(codec_cal)->X) = (Y))
	S(mic_current, TOMTOM_PID_MIC_5_UA);
	S(hph_current, TOMTOM_PID_MIC_5_UA);
	S(t_mic_pid, 100);
	S(t_ins_complete, 250);
	S(t_ins_retry, 200);
#undef S
#define S(X, Y) ((WCD9XXX_MBHC_CAL_PLUG_TYPE_PTR(codec_cal)->X) = (Y))
	S(v_no_mic, 30);
	S(v_hs_max, 2400);
#undef S
#define S(X, Y) ((WCD9XXX_MBHC_CAL_BTN_DET_PTR(codec_cal)->X) = (Y))
	S(c[0], 62);
	S(c[1], 124);
	S(nc, 1);
	S(n_meas, 3);
	S(mbhc_nsc, 11);
	S(n_btn_meas, 1);
	S(n_btn_con, 2);
	S(num_btn, WCD9XXX_MBHC_DEF_BUTTONS);
	S(v_btn_press_delta_sta, 100);
	S(v_btn_press_delta_cic, 50);
#undef S
	btn_cfg = WCD9XXX_MBHC_CAL_BTN_DET_PTR(codec_cal);
	btn_low = wcd9xxx_mbhc_cal_btn_det_mp(btn_cfg, MBHC_BTN_DET_V_BTN_LOW);
	btn_high = wcd9xxx_mbhc_cal_btn_det_mp(btn_cfg,
					       MBHC_BTN_DET_V_BTN_HIGH);
	btn_low[0] = -50;
	btn_high[0] = 90;
	btn_low[1] = 130;
	btn_high[1] = 220;
	btn_low[2] = 235;
	btn_high[2] = 335;
	btn_low[3] = 375;
	btn_high[3] = 655;
	btn_low[4] = 656;
	btn_high[4] = 660;
	btn_low[5] = 661;
	btn_high[5] = 670;
	btn_low[6] = 671;
	btn_high[6] = 680;
	btn_low[7] = 681;
	btn_high[7] = 690;
	n_ready = wcd9xxx_mbhc_cal_btn_det_mp(btn_cfg, MBHC_BTN_DET_N_READY);
	n_ready[0] = 80;
	n_ready[1] = 68;
	n_cic = wcd9xxx_mbhc_cal_btn_det_mp(btn_cfg, MBHC_BTN_DET_N_CIC);
	n_cic[0] = 60;
	n_cic[1] = 47;
	gain = wcd9xxx_mbhc_cal_btn_det_mp(btn_cfg, MBHC_BTN_DET_GAIN);
	gain[0] = 11;
	gain[1] = 9;

	return codec_cal;
}
#endif
//htc audio --
static int msm_snd_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai_link *dai_link = rtd->dai_link;

	int ret = 0;
	u32 rx_ch[SLIM_MAX_RX_PORTS], tx_ch[SLIM_MAX_TX_PORTS];
	u32 rx_ch_cnt = 0, tx_ch_cnt = 0;
	u32 user_set_tx_ch = 0;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		pr_debug("%s: rx_0_ch=%d\n", __func__, msm_slim_0_rx_ch);
		ret = snd_soc_dai_get_channel_map(codec_dai,
					&tx_ch_cnt, tx_ch, &rx_ch_cnt , rx_ch);
		if (ret < 0) {
			pr_err("%s: failed to get codec chan map, err:%d\n",
				__func__, ret);
			goto end;
		}
		ret = snd_soc_dai_set_channel_map(cpu_dai, 0, 0,
						  msm_slim_0_rx_ch, rx_ch);
		if (ret < 0) {
			pr_err("%s: failed to set cpu chan map, err:%d\n",
				__func__, ret);
			goto end;
		}
	} else {

		pr_debug("%s: %s_tx_dai_id_%d_ch=%d\n", __func__,
			 codec_dai->name, codec_dai->id, user_set_tx_ch);
		ret = snd_soc_dai_get_channel_map(codec_dai,
					 &tx_ch_cnt, tx_ch, &rx_ch_cnt , rx_ch);
		if (ret < 0) {
			pr_err("%s: failed to get codec chan map\n, err:%d\n",
				__func__, ret);
			goto end;
		}
		/* For <codec>_tx1 case */
		if (dai_link->be_id == MSM_BACKEND_DAI_SLIMBUS_0_TX)
			user_set_tx_ch = msm_slim_0_tx_ch;
		/* For <codec>_tx2 case */
		else if (dai_link->be_id == MSM_BACKEND_DAI_SLIMBUS_1_TX)
			user_set_tx_ch = params_channels(params);
		else if (dai_link->be_id == MSM_BACKEND_DAI_SLIMBUS_3_TX)
			/* DAI 5 is used for external EC reference from codec.
			 * Since Rx is fed as reference for EC, the config of
			 * this DAI is based on that of the Rx path.
			 */
			user_set_tx_ch = msm_slim_0_rx_ch;
		else if (dai_link->be_id == MSM_BACKEND_DAI_SLIMBUS_4_TX)
			user_set_tx_ch = msm_vi_feed_tx_ch;
		else
			user_set_tx_ch = tx_ch_cnt;

		pr_debug(
		"%s: msm_slim_0_tx_ch(%d) user_set_tx_ch(%d) tx_ch_cnt(%d) be_id %d\n",
			__func__, msm_slim_0_tx_ch, user_set_tx_ch,
			tx_ch_cnt, dai_link->be_id);

		ret = snd_soc_dai_set_channel_map(cpu_dai,
						  user_set_tx_ch, tx_ch, 0 , 0);
		if (ret < 0) {
			pr_err("%s: failed to set cpu chan map, err:%d\n",
				__func__, ret);
			goto end;
		}
	}
end:
	return ret;
}

static struct snd_soc_ops msm8994_be_ops = {
	.hw_params = msm_snd_hw_params,
};

static int msm8994_slimbus_2_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int ret = 0;
	unsigned int rx_ch[SLIM_MAX_RX_PORTS], tx_ch[SLIM_MAX_TX_PORTS];
	unsigned int rx_ch_cnt = 0, tx_ch_cnt = 0;
	unsigned int num_tx_ch = 0;
	unsigned int num_rx_ch = 0;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		num_rx_ch =  params_channels(params);
		pr_debug("%s: %s rx_dai_id = %d  num_ch = %d\n", __func__,
			codec_dai->name, codec_dai->id, num_rx_ch);
		ret = snd_soc_dai_get_channel_map(codec_dai,
				&tx_ch_cnt, tx_ch, &rx_ch_cnt , rx_ch);
		if (ret < 0) {
			pr_err("%s: failed to get codec chan map, err:%d\n",
				__func__, ret);
			goto end;
		}
		ret = snd_soc_dai_set_channel_map(cpu_dai, 0, 0,
				num_rx_ch, rx_ch);
		if (ret < 0) {
			pr_err("%s: failed to set cpu chan map, err:%d\n",
				__func__, ret);
			goto end;
		}
	} else {
		num_tx_ch =  params_channels(params);
		pr_debug("%s: %s  tx_dai_id = %d  num_ch = %d\n", __func__,
			codec_dai->name, codec_dai->id, num_tx_ch);
		ret = snd_soc_dai_get_channel_map(codec_dai,
				&tx_ch_cnt, tx_ch, &rx_ch_cnt , rx_ch);
		if (ret < 0) {
			pr_err("%s: failed to get codec chan map, err:%d\n",
				__func__, ret);
			goto end;
		}
		ret = snd_soc_dai_set_channel_map(cpu_dai,
				num_tx_ch, tx_ch, 0 , 0);
		if (ret < 0) {
			pr_err("%s: failed to set cpu chan map, err:%d\n",
				__func__, ret);
			goto end;
		}
	}
end:
	return ret;
}

static struct snd_soc_ops msm8994_slimbus_2_be_ops = {
	.hw_params = msm8994_slimbus_2_hw_params,
};

/* Digital audio interface glue - connects codec <---> CPU */
static struct snd_soc_dai_link msm8994_common_dai_links[] = {
	/* FrontEnd DAI Links */
	{
		.name = "MSM8994 Media1",
		.stream_name = "MultiMedia1",
		.cpu_dai_name	= "MultiMedia1",
		.platform_name  = "msm-pcm-dsp.0",
		.dynamic = 1,
		.async_ops = ASYNC_DPCM_SND_SOC_PREPARE,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA1
	},
	{
		.name = "MSM8994 Media2",
		.stream_name = "MultiMedia2",
		.cpu_dai_name   = "MultiMedia2",
		.platform_name  = "msm-pcm-dsp.0",
		.dynamic = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA2,
	},
	{
		.name = "Circuit-Switch Voice",
		.stream_name = "CS-Voice",
		.cpu_dai_name   = "CS-VOICE",
		.platform_name  = "msm-pcm-voice",
		.dynamic = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.be_id = MSM_FRONTEND_DAI_CS_VOICE,
	},
	{
		.name = "MSM VoIP",
		.stream_name = "VoIP",
		.cpu_dai_name = "VoIP",
		.platform_name = "msm-voip-dsp",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.be_id = MSM_FRONTEND_DAI_VOIP,
	},
	{
		.name = "MSM8994 ULL",
		.stream_name = "MultiMedia3",
		.cpu_dai_name = "MultiMedia3",
		.platform_name = "msm-pcm-dsp.2",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA3,
	},
	/* Hostless PCM purpose */
	{
		.name = "SLIMBUS_0 Hostless",
		.stream_name = "SLIMBUS_0 Hostless",
		.cpu_dai_name = "SLIMBUS0_HOSTLESS",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1, /* dai link has playback support */
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
	{
		.name = "Primary MI2S TX_Hostless",
		.stream_name = "Primary MI2S_TX Hostless Capture",
		.cpu_dai_name = "PRI_MI2S_TX_HOSTLESS",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
	{
		.name = "MSM AFE-PCM RX",
		.stream_name = "AFE-PROXY RX",
		.cpu_dai_name = "msm-dai-q6-dev.241",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-rx",
		.platform_name = "msm-pcm-afe",
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
	},
	{
		.name = "MSM AFE-PCM TX",
		.stream_name = "AFE-PROXY TX",
		.cpu_dai_name = "msm-dai-q6-dev.240",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-tx",
		.platform_name  = "msm-pcm-afe",
		.ignore_suspend = 1,
	},
	{
		.name = "MSM8994 Compress1",
		.stream_name = "Compress1",
		.cpu_dai_name = "MultiMedia4",
		.platform_name = "msm-compress-dsp",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			 SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		 /* this dainlink has playback support */
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA4,
	},
	{
		.name = "AUXPCM Hostless",
		.stream_name = "AUXPCM Hostless",
		.cpu_dai_name = "AUXPCM_HOSTLESS",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
	{
		.name = "SLIMBUS_1 Hostless",
		.stream_name = "SLIMBUS_1 Hostless",
		.cpu_dai_name = "SLIMBUS1_HOSTLESS",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1, /* dai link has playback support */
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
	{
		.name = "SLIMBUS_3 Hostless",
		.stream_name = "SLIMBUS_3 Hostless",
		.cpu_dai_name = "SLIMBUS3_HOSTLESS",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1, /* dai link has playback support */
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
	{
		.name = "SLIMBUS_4 Hostless",
		.stream_name = "SLIMBUS_4 Hostless",
		.cpu_dai_name = "SLIMBUS4_HOSTLESS",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1, /* dai link has playback support */
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
	{
		.name = "VoLTE",
		.stream_name = "VoLTE",
		.cpu_dai_name = "VoLTE",
		.platform_name = "msm-pcm-voice",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_VOLTE,
	},
	{
		.name = "MSM8994 LowLatency",
		.stream_name = "MultiMedia5",
		.cpu_dai_name = "MultiMedia5",
		.platform_name = "msm-pcm-dsp.1",
		.dynamic = 1,
		.async_ops = ASYNC_DPCM_SND_SOC_PREPARE,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
				SND_SOC_DPCM_TRIGGER_POST},
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA5,
	},
	{
		.name = "Listen 1 Audio Service",
		.stream_name = "Listen 1 Audio Service",
		.cpu_dai_name = "LSM1",
		.platform_name = "msm-lsm-client",
		.dynamic = 1,
		.trigger = { SND_SOC_DPCM_TRIGGER_POST,
			     SND_SOC_DPCM_TRIGGER_POST },
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_LSM1,
	},
	/* Multiple Tunnel instances */
	{
		.name = "MSM8994 Compress2",
		.stream_name = "Compress2",
		.cpu_dai_name = "MultiMedia7",
		.platform_name = "msm-compress-dsp",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			 SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		 /* this dainlink has playback support */
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA7,
	},
	{
		.name = "MSM8994 Compress3",
		.stream_name = "Compress3",
		.cpu_dai_name = "MultiMedia10",
		.platform_name = "msm-compress-dsp",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			 SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		 /* this dainlink has playback support */
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA10,
	},
	{
		.name = "MSM8994 Compr8",
		.stream_name = "COMPR8",
		.cpu_dai_name = "MultiMedia8",
		.platform_name = "msm-compr-dsp",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			 SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		 /* this dainlink has playback support */
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA8,
	},
	{
		.name = "QCHAT",
		.stream_name = "QCHAT",
		.cpu_dai_name = "QCHAT",
		.platform_name = "msm-pcm-voice",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_QCHAT,
	},
	/* HDMI Hostless */
	{
		.name = "HDMI_RX_HOSTLESS",
		.stream_name = "HDMI_RX_HOSTLESS",
		.cpu_dai_name = "HDMI_HOSTLESS",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
	{
		.name = "Voice2",
		.stream_name = "Voice2",
		.cpu_dai_name = "Voice2",
		.platform_name = "msm-pcm-voice",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_VOICE2,
	},
	{
		.name = "INT_HFP_BT Hostless",
		.stream_name = "INT_HFP_BT Hostless",
		.cpu_dai_name = "INT_HFP_BT_HOSTLESS",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
	{
		.name = "MSM8994 HFP TX",
		.stream_name = "MultiMedia6",
		.cpu_dai_name = "MultiMedia6",
		.platform_name = "msm-pcm-loopback",
		.dynamic = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.ignore_suspend = 1,
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA6,
	},
	{
		.name = LPASS_BE_SLIMBUS_4_TX,
		.stream_name = "Slimbus4 Capture",
		.cpu_dai_name = "msm-dai-q6-dev.16393",
		.platform_name = "msm-pcm-hostless",
		.codec_name = "tomtom_codec",
		.codec_dai_name = "tomtom_vifeedback",
		.be_id = MSM_BACKEND_DAI_SLIMBUS_4_TX,
		.be_hw_params_fixup = msm_slim_4_tx_be_hw_params_fixup,
		.ops = &msm8994_be_ops,
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
	},
	/* Ultrasound RX DAI Link */
	{
		.name = "SLIMBUS_2 Hostless Playback",
		.stream_name = "SLIMBUS_2 Hostless Playback",
		.cpu_dai_name = "msm-dai-q6-dev.16388",
		.platform_name = "msm-pcm-hostless",
		.codec_name = "tomtom_codec",
		.codec_dai_name = "tomtom_rx2",
		.ignore_suspend = 1,
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ops = &msm8994_slimbus_2_be_ops,
	},
	/* Ultrasound TX DAI Link */
	{
		.name = "SLIMBUS_2 Hostless Capture",
		.stream_name = "SLIMBUS_2 Hostless Capture",
		.cpu_dai_name = "msm-dai-q6-dev.16389",
		.platform_name = "msm-pcm-hostless",
		.codec_name = "tomtom_codec",
		.codec_dai_name = "tomtom_tx2",
		.ignore_suspend = 1,
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ops = &msm8994_slimbus_2_be_ops,
	},
	/* LSM FE */
	{
		.name = "Listen 2 Audio Service",
		.stream_name = "Listen 2 Audio Service",
		.cpu_dai_name = "LSM2",
		.platform_name = "msm-lsm-client",
		.dynamic = 1,
		.trigger = { SND_SOC_DPCM_TRIGGER_POST,
				 SND_SOC_DPCM_TRIGGER_POST },
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_LSM2,
	},
	{
		.name = "Listen 3 Audio Service",
		.stream_name = "Listen 3 Audio Service",
		.cpu_dai_name = "LSM3",
		.platform_name = "msm-lsm-client",
		.dynamic = 1,
		.trigger = { SND_SOC_DPCM_TRIGGER_POST,
				 SND_SOC_DPCM_TRIGGER_POST },
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_LSM3,
	},
	{
		.name = "Listen 4 Audio Service",
		.stream_name = "Listen 4 Audio Service",
		.cpu_dai_name = "LSM4",
		.platform_name = "msm-lsm-client",
		.dynamic = 1,
		.trigger = { SND_SOC_DPCM_TRIGGER_POST,
				 SND_SOC_DPCM_TRIGGER_POST },
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_LSM4,
	},
	{
		.name = "Listen 5 Audio Service",
		.stream_name = "Listen 5 Audio Service",
		.cpu_dai_name = "LSM5",
		.platform_name = "msm-lsm-client",
		.dynamic = 1,
		.trigger = { SND_SOC_DPCM_TRIGGER_POST,
				 SND_SOC_DPCM_TRIGGER_POST },
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_LSM5,
	},
	{
		.name = "Listen 6 Audio Service",
		.stream_name = "Listen 6 Audio Service",
		.cpu_dai_name = "LSM6",
		.platform_name = "msm-lsm-client",
		.dynamic = 1,
		.trigger = { SND_SOC_DPCM_TRIGGER_POST,
				 SND_SOC_DPCM_TRIGGER_POST },
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_LSM6,
	},
	{
		.name = "Listen 7 Audio Service",
		.stream_name = "Listen 7 Audio Service",
		.cpu_dai_name = "LSM7",
		.platform_name = "msm-lsm-client",
		.dynamic = 1,
		.trigger = { SND_SOC_DPCM_TRIGGER_POST,
				 SND_SOC_DPCM_TRIGGER_POST },
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_LSM7,
	},
	{
		.name = "Listen 8 Audio Service",
		.stream_name = "Listen 8 Audio Service",
		.cpu_dai_name = "LSM8",
		.platform_name = "msm-lsm-client",
		.dynamic = 1,
		.trigger = { SND_SOC_DPCM_TRIGGER_POST,
				 SND_SOC_DPCM_TRIGGER_POST },
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_LSM8,
	},
	{
		.name = "MSM8994 Media9",
		.stream_name = "MultiMedia9",
		.cpu_dai_name = "MultiMedia9",
		.platform_name = "msm-pcm-dsp.0",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
				SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA9,
	},
	{
		.name = "VoWLAN",
		.stream_name = "VoWLAN",
		.cpu_dai_name = "VoWLAN",
		.platform_name = "msm-pcm-voice",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_VOWLAN,
	},
	{
		.name = "MSM8994 Compress4",
		.stream_name = "Compress4",
		.cpu_dai_name = "MultiMedia11",
		.platform_name = "msm-compress-dsp",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			 SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		 /* this dainlink has playback support */
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA11,
	},
	{
		.name = "MSM8994 Compress5",
		.stream_name = "Compress5",
		.cpu_dai_name = "MultiMedia12",
		.platform_name = "msm-compress-dsp",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			 SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		 /* this dainlink has playback support */
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA12,
	},
	{
		.name = "MSM8994 Compress6",
		.stream_name = "Compress6",
		.cpu_dai_name = "MultiMedia13",
		.platform_name = "msm-compress-dsp",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			 SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		 /* this dainlink has playback support */
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA13,
	},
	{
		.name = "MSM8994 Compress7",
		.stream_name = "Compress7",
		.cpu_dai_name = "MultiMedia14",
		.platform_name = "msm-compress-dsp",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			 SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		 /* this dainlink has playback support */
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA14,
	},
	{
		.name = "MSM8994 Compress8",
		.stream_name = "Compress8",
		.cpu_dai_name = "MultiMedia15",
		.platform_name = "msm-compress-dsp",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			 SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		 /* this dainlink has playback support */
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA15,
	},
	{
		.name = "MSM8994 Compress9",
		.stream_name = "Compress9",
		.cpu_dai_name = "MultiMedia16",
		.platform_name = "msm-compress-dsp",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			 SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		 /* this dainlink has playback support */
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA16,
	},
	/* CPE LSM direct dai-link */
	{
		.name = "CPE Listen service",
		.stream_name = "CPE Listen Audio Service",
		.cpu_dai_name = "CPE_LSM_NOHOST",
		.platform_name = "msm-cpe-lsm",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST },
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "tomtom_mad1",
		.codec_name = "tomtom_codec",
	},
	{
		.name = "MultiMedia3 Record",
		.stream_name = "MultiMedia3 Capture",
		.cpu_dai_name = "MultiMedia3",
		.platform_name = "msm-pcm-dsp.0",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA3,
	},
	/* End of FE DAI LINK */
	/* Backend FM DAI Links */
	{
		.name = LPASS_BE_INT_FM_RX,
		.stream_name = "Internal FM Playback",
		.cpu_dai_name = "msm-dai-q6-dev.12292",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-rx",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_INT_FM_RX,
		.be_hw_params_fixup = msm_be_fm_hw_params_fixup,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_INT_FM_TX,
		.stream_name = "Internal FM Capture",
		.cpu_dai_name = "msm-dai-q6-dev.12293",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-tx",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_INT_FM_TX,
		.be_hw_params_fixup = msm_be_hw_params_fixup,
		.ignore_suspend = 1,
	},
	/* Backend AFE DAI Links */
	{
		.name = LPASS_BE_AFE_PCM_RX,
		.stream_name = "AFE Playback",
		.cpu_dai_name = "msm-dai-q6-dev.224",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-rx",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_AFE_PCM_RX,
		.be_hw_params_fixup = msm_proxy_rx_be_hw_params_fixup,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_AFE_PCM_TX,
		.stream_name = "AFE Capture",
		.cpu_dai_name = "msm-dai-q6-dev.225",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-tx",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_AFE_PCM_TX,
		.be_hw_params_fixup = msm_proxy_tx_be_hw_params_fixup,
		.ignore_suspend = 1,
	},
//htc audio ++
#if 0
	/* Primary AUX PCM Backend DAI Links */
	{
		.name = LPASS_BE_AUXPCM_RX,
		.stream_name = "AUX PCM Playback",
		.cpu_dai_name = "msm-dai-q6-auxpcm.1",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-rx",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_AUXPCM_RX,
		.be_hw_params_fixup = msm_auxpcm_be_params_fixup,
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		/* this dainlink has playback support */
	},
	{
		.name = LPASS_BE_AUXPCM_TX,
		.stream_name = "AUX PCM Capture",
		.cpu_dai_name = "msm-dai-q6-auxpcm.1",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-tx",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_AUXPCM_TX,
		.be_hw_params_fixup = msm_auxpcm_be_params_fixup,
		.ignore_suspend = 1,
	},
#endif
//htc audio --
	/* Secondary AUX PCM Backend DAI Links */
	{
		.name = LPASS_BE_SEC_AUXPCM_RX,
		.stream_name = "Sec AUX PCM Playback",
		.cpu_dai_name = "msm-dai-q6-auxpcm.2",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-rx",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_SEC_AUXPCM_RX,
		.be_hw_params_fixup = msm_auxpcm_be_params_fixup,
		.ops = &msm_sec_auxpcm_be_ops,
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		/* this dainlink has playback support */
	},
	{
		.name = LPASS_BE_SEC_AUXPCM_TX,
		.stream_name = "Sec AUX PCM Capture",
		.cpu_dai_name = "msm-dai-q6-auxpcm.2",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-tx",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_SEC_AUXPCM_TX,
		.be_hw_params_fixup = msm_auxpcm_be_params_fixup,
		.ops = &msm_sec_auxpcm_be_ops,
		.ignore_suspend = 1,
	},

	/* Backend DAI Links */
	{
		.name = LPASS_BE_SLIMBUS_0_RX,
		.stream_name = "Slimbus Playback",
		.cpu_dai_name = "msm-dai-q6-dev.16384",
		.platform_name = "msm-pcm-routing",
		.codec_name = "tomtom_codec",
		.codec_dai_name = "tomtom_rx1",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_SLIMBUS_0_RX,
		.init = &msm_audrx_init,
		.be_hw_params_fixup = msm_slim_0_rx_be_hw_params_fixup,
		.ignore_pmdown_time = 1, /* dai link has playback support */
		.ignore_suspend = 1,
		.ops = &msm8994_be_ops,
	},
	{
		.name = LPASS_BE_SLIMBUS_0_TX,
		.stream_name = "Slimbus Capture",
		.cpu_dai_name = "msm-dai-q6-dev.16385",
		.platform_name = "msm-pcm-routing",
		.codec_name = "tomtom_codec",
		.codec_dai_name = "tomtom_tx1",
		.no_pcm = 1,
		.async_ops = ASYNC_DPCM_SND_SOC_PREPARE,
		.be_id = MSM_BACKEND_DAI_SLIMBUS_0_TX,
		.be_hw_params_fixup = msm_slim_0_tx_be_hw_params_fixup,
		.ignore_suspend = 1,
		.ops = &msm8994_be_ops,
	},
	{
		.name = LPASS_BE_SLIMBUS_1_RX,
		.stream_name = "Slimbus1 Playback",
		.cpu_dai_name = "msm-dai-q6-dev.16386",
		.platform_name = "msm-pcm-routing",
		.codec_name = "tomtom_codec",
		.codec_dai_name = "tomtom_rx1",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_SLIMBUS_1_RX,
		.be_hw_params_fixup = msm_slim_0_rx_be_hw_params_fixup,
		.ops = &msm8994_be_ops,
		/* dai link has playback support */
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_SLIMBUS_1_TX,
		.stream_name = "Slimbus1 Capture",
		.cpu_dai_name = "msm-dai-q6-dev.16387",
		.platform_name = "msm-pcm-routing",
		.codec_name = "tomtom_codec",
		.codec_dai_name = "tomtom_tx1",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_SLIMBUS_1_TX,
		.be_hw_params_fixup = msm_slim_0_tx_be_hw_params_fixup,
		.ops = &msm8994_be_ops,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_SLIMBUS_3_RX,
		.stream_name = "Slimbus3 Playback",
		.cpu_dai_name = "msm-dai-q6-dev.16390",
		.platform_name = "msm-pcm-routing",
		.codec_name = "tomtom_codec",
		.codec_dai_name = "tomtom_rx1",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_SLIMBUS_3_RX,
		.be_hw_params_fixup = msm_slim_0_rx_be_hw_params_fixup,
		.ops = &msm8994_be_ops,
		/* dai link has playback support */
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_SLIMBUS_3_TX,
		.stream_name = "Slimbus3 Capture",
		.cpu_dai_name = "msm-dai-q6-dev.16391",
		.platform_name = "msm-pcm-routing",
		.codec_name = "tomtom_codec",
		.codec_dai_name = "tomtom_tx1",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_SLIMBUS_3_TX,
		.be_hw_params_fixup = msm_slim_0_tx_be_hw_params_fixup,
		.ops = &msm8994_be_ops,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_SLIMBUS_4_RX,
		.stream_name = "Slimbus4 Playback",
		.cpu_dai_name = "msm-dai-q6-dev.16392",
		.platform_name = "msm-pcm-routing",
		.codec_name = "tomtom_codec",
		.codec_dai_name	= "tomtom_rx1",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_SLIMBUS_4_RX,
		.be_hw_params_fixup = msm_slim_0_rx_be_hw_params_fixup,
		.ops = &msm8994_be_ops,
		/* dai link has playback support */
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
	},
	/* Incall Record Uplink BACK END DAI Link */
	{
		.name = LPASS_BE_INCALL_RECORD_TX,
		.stream_name = "Voice Uplink Capture",
		.cpu_dai_name = "msm-dai-q6-dev.32772",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-tx",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_INCALL_RECORD_TX,
		.be_hw_params_fixup = msm_be_hw_params_fixup,
		.ignore_suspend = 1,
	},
	/* Incall Record Downlink BACK END DAI Link */
	{
		.name = LPASS_BE_INCALL_RECORD_RX,
		.stream_name = "Voice Downlink Capture",
		.cpu_dai_name = "msm-dai-q6-dev.32771",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-tx",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_INCALL_RECORD_RX,
		.be_hw_params_fixup = msm_be_hw_params_fixup,
		.ignore_suspend = 1,
	},
	/* MAD BE */
	{
		.name = LPASS_BE_SLIMBUS_5_TX,
		.stream_name = "Slimbus5 Capture",
		.cpu_dai_name = "msm-dai-q6-dev.16395",
		.platform_name = "msm-pcm-routing",
		.codec_name = "tomtom_codec",
		.codec_dai_name = "tomtom_mad1",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_SLIMBUS_5_TX,
		.be_hw_params_fixup = msm_slim_5_tx_be_hw_params_fixup,
		.ignore_suspend = 1,
		.ops = &msm8994_be_ops,
	},
	/* Incall Music BACK END DAI Link */
	{
		.name = LPASS_BE_VOICE_PLAYBACK_TX,
		.stream_name = "Voice Farend Playback",
		.cpu_dai_name = "msm-dai-q6-dev.32773",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-rx",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_VOICE_PLAYBACK_TX,
		.be_hw_params_fixup = msm_be_hw_params_fixup,
		.ignore_suspend = 1,
	},
	/* Incall Music 2 BACK END DAI Link */
	{
		.name = LPASS_BE_VOICE2_PLAYBACK_TX,
		.stream_name = "Voice2 Farend Playback",
		.cpu_dai_name = "msm-dai-q6-dev.32770",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-rx",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_VOICE2_PLAYBACK_TX,
		.be_hw_params_fixup = msm_be_hw_params_fixup,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_PRI_MI2S_TX,
		.stream_name = "Primary MI2S Capture",
		.cpu_dai_name = "msm-dai-q6-mi2s.0",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-tx",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_PRI_MI2S_TX,
		.be_hw_params_fixup = msm_tx_be_hw_params_fixup,
		.ops = &msm8994_mi2s_be_ops,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_TERT_MI2S_RX,
		.stream_name = "Tertiary MI2S Playback",
		.cpu_dai_name = "msm-dai-q6-mi2s.2",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-rx",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_TERTIARY_MI2S_RX,
		.be_hw_params_fixup = msm_be_tert_mi2s_hw_params_fixup,
		.ops = &msm8994_tert_mi2s_be_ops,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_TERT_MI2S_TX,
		.stream_name = "Tertiary MI2S Capture",
		.cpu_dai_name = "msm-dai-q6-mi2s.2",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-tx",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_TERTIARY_MI2S_TX,
		.be_hw_params_fixup = msm_be_tert_mi2s_hw_params_fixup,
		.ops = &msm8994_tert_mi2s_be_ops,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_QUAT_MI2S_RX,
		.stream_name = "Quaternary MI2S Playback",
		.cpu_dai_name = "msm-dai-q6-mi2s.3",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm_htc_mi2s_codec", //htc audio
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
		.be_hw_params_fixup = msm_be_quat_mi2s_hw_params_fixup,
		.init = &msm_htc_quat_mi2s_init,  //htc audio
		.ops = &msm8994_quat_mi2s_be_ops,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_QUAT_MI2S_TX,
		.stream_name = "Quaternary MI2S Capture",
		.cpu_dai_name = "msm-dai-q6-mi2s.3",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-tx",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_QUATERNARY_MI2S_TX,
		.be_hw_params_fixup = msm_be_quat_mi2s_hw_params_fixup,
		.ops = &msm8994_quat_mi2s_be_ops,
		.ignore_suspend = 1,
	},
	{
		.name = "Quaternary MI2S Hostless",
		.stream_name = "Quaternary MI2S Hostless",
		.cpu_dai_name = "QUAT_MI2S_HOSTLESS",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
	{
		.name = "Tertiary MI2S Hostless",
		.stream_name = "TERT_MI2S Hostless",
		.cpu_dai_name = "TERT_MI2S_HOSTLESS",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},

//htc audio ++: add for open_dummy_stream() at AHAL
	{
		.name = "Compress Stub",
		.stream_name = "Compress Stub",
		.cpu_dai_name   = "MM_STUB",
		.platform_name  = "msm-pcm-hostless",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1, /* this dailink has playback support */
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
//htc audio --


};

static struct snd_soc_dai_link msm8994_hdmi_dai_link[] = {
/* HDMI BACK END DAI Link */
	{
		.name = LPASS_BE_HDMI,
		.stream_name = "HDMI Playback",
		.cpu_dai_name = "msm-dai-q6-hdmi.8",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-hdmi-audio-codec-rx",
		.codec_dai_name = "msm_hdmi_audio_codec_rx_dai",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_HDMI_RX,
		.be_hw_params_fixup = msm8994_hdmi_be_hw_params_fixup,
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
	},
};

static struct snd_soc_dai_link msm8994_dai_links[
					 ARRAY_SIZE(msm8994_common_dai_links) +
					 ARRAY_SIZE(msm8994_hdmi_dai_link)];

struct snd_soc_card snd_soc_card_msm8994 = {
	.name		= "msm8994-tomtom-snd-card",
};

//htc audio ++
//Enable hset bias for bring up
#if MSM8994_BRINGUP
static int msm8994_enable_hset_bias(struct snd_soc_card *card)
{
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	int ret;

	//Enable LED_3V3
	if (pdata->led_gpio) {
		ret = gpio_request(pdata->led_gpio, "TOMTOM_CODEC_LED3V3");
		if (ret) {
			pr_err("%s: Failed to request led-pio %d ret %d\n",
				__func__, pdata->led_gpio,ret);
//			return ret;
		} else
			pr_info("%s: Request led_gpio %d ret %d\n",
				__func__, pdata->led_gpio,ret);
	}

	gpio_direction_output(pdata->led_gpio, 0);
	pr_info("%s: enable led_gpio %d\n",__func__,pdata->led_gpio);
	gpio_set_value(pdata->led_gpio, 1);

	if (pdata->hset_bias_gpio) {
		ret = gpio_request(pdata->hset_bias_gpio, "TOMTOM_CODEC_HSET_BIAS");
		if (ret) {
			pr_err("%s: Failed to request hset bias gpio %d ret %d\n",
				__func__, pdata->hset_bias_gpio,ret);
//			return ret;
		} else
			pr_info("%s: Request hset bias gpio %d ret %d\n",
				__func__, pdata->hset_bias_gpio,ret);
	}

	gpio_direction_output(pdata->hset_bias_gpio, 0);
	pr_info("%s: enable headset mic bias(hset_bias), gpio %d\n",__func__,pdata->hset_bias_gpio);
	gpio_set_value(pdata->hset_bias_gpio, 1);

	return 0;
}
#endif

#if MSM8994_LDO_WR
static int msm8994_enable_hph(struct snd_soc_card *card)
{
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	int ret;

	//Enable hph en gpio, otherwise AKM will hold I2C bus then cause I2C bus error
	if (pdata->hph_en_gpio) {
		ret = gpio_request(pdata->hph_en_gpio, "TOMTOM_CODEC_HPH_EN");
		if (ret) {
			pr_err("%s: Failed to request hph_en_gpio %d ret %d\n",
				__func__, pdata->hph_en_gpio,ret);
//			return ret;
		} else
			pr_info("%s: Request hph_en_gpio %d ret %d\n",
				__func__, pdata->hph_en_gpio,ret);
	}

	gpio_direction_output(pdata->hph_en_gpio, 0);
	pr_info("%s: enable hph_en_gpio %d\n",__func__,pdata->hph_en_gpio);
	gpio_set_value(pdata->hph_en_gpio, 1);

	return 0;
}

int msm8994_hph_en_ready(void)
{
	return hph_en_ready;
}
EXPORT_SYMBOL(msm8994_hph_en_ready);
#endif
// htc audio --

static int msm8994_populate_dai_link_component_of_node(
					struct snd_soc_card *card)
{
	int i, index, ret = 0;
	struct device *cdev = card->dev;
	struct snd_soc_dai_link *dai_link = card->dai_link;
	struct device_node *np;

	if (!cdev) {
		pr_err("%s: Sound card device memory NULL\n", __func__);
		return -ENODEV;
	}

	for (i = 0; i < card->num_links; i++) {
		if (dai_link[i].platform_of_node && dai_link[i].cpu_of_node)
			continue;

		/* populate platform_of_node for snd card dai links */
		if (dai_link[i].platform_name &&
		    !dai_link[i].platform_of_node) {
			index = of_property_match_string(cdev->of_node,
						"asoc-platform-names",
						dai_link[i].platform_name);
			if (index < 0) {
				pr_debug("%s: No match found for platform name: %s\n",
					__func__, dai_link[i].platform_name);
				ret = index;
				goto err;
			}
			np = of_parse_phandle(cdev->of_node, "asoc-platform",
					      index);
			if (!np) {
				pr_err("%s: retrieving phandle for platform %s, index %d failed\n",
					__func__, dai_link[i].platform_name,
					index);
				ret = -ENODEV;
				goto err;
			}
			dai_link[i].platform_of_node = np;
			dai_link[i].platform_name = NULL;
		}

		/* populate cpu_of_node for snd card dai links */
		if (dai_link[i].cpu_dai_name && !dai_link[i].cpu_of_node) {
			index = of_property_match_string(cdev->of_node,
						 "asoc-cpu-names",
						 dai_link[i].cpu_dai_name);
			if (index >= 0) {
				np = of_parse_phandle(cdev->of_node, "asoc-cpu",
						index);
				if (!np) {
					pr_err("%s: retrieving phandle for cpu dai %s failed\n",
						__func__,
						dai_link[i].cpu_dai_name);
					ret = -ENODEV;
					goto err;
				}
				dai_link[i].cpu_of_node = np;
				dai_link[i].cpu_dai_name = NULL;
			}
		}

		/* populate codec_of_node for snd card dai links */
		if (dai_link[i].codec_name && !dai_link[i].codec_of_node) {
			index = of_property_match_string(cdev->of_node,
						 "asoc-codec-names",
						 dai_link[i].codec_name);
			if (index < 0)
				continue;
			np = of_parse_phandle(cdev->of_node, "asoc-codec",
					      index);
			if (!np) {
				pr_err("%s: retrieving phandle for codec %s failed\n",
					__func__, dai_link[i].codec_name);
				ret = -ENODEV;
				goto err;
			}
			dai_link[i].codec_of_node = np;
			dai_link[i].codec_name = NULL;
		}
	}

err:
	return ret;
}

static int msm8994_prepare_codec_mclk(struct snd_soc_card *card)
{
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	int ret;
	if (pdata->mclk_gpio) {
		ret = gpio_request(pdata->mclk_gpio, "TOMTOM_CODEC_PMIC_MCLK");
		if (ret) {
			dev_err(card->dev,
				"%s: request mclk gpio failed %d, err:%d\n",
				__func__, pdata->mclk_gpio, ret);
			return ret;
		}
	}

	return 0;
}
//htc audio ++
#ifndef CONFIG_USE_CODEC_MBHC
static int msm8994_prepare_us_euro(struct snd_soc_card *card)
{
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	int ret;
	if (pdata->us_euro_gpio >= 0) {
		dev_dbg(card->dev, "%s: us_euro gpio request %d", __func__,
			pdata->us_euro_gpio);
		ret = gpio_request(pdata->us_euro_gpio, "TOMTOM_CODEC_US_EURO");
		if (ret) {
			dev_err(card->dev,
				"%s: Failed to request codec US/EURO gpio %d error %d\n",
				__func__, pdata->us_euro_gpio, ret);
			return ret;
		}
	}

	return 0;
}
#endif

#if 0
static int msm8994_dtparse_hset_amp(struct platform_device *pdev,
				struct hset_amp_config *pconfig)
{
	int i, ret;

	if(!pconfig || !pdev)
		return 0;

	for(i = 0; i < ARRAY_SIZE(pconfig->gpio); i++) {

		if(!pconfig->gpio[i].gpio_name) {
			pr_err("%s: %d hset gpio name is null\n",__func__,i);
			return 0;
		}

		pconfig->gpio[i].gpio_no = of_get_named_gpio(pdev->dev.of_node, \
						pconfig->gpio[i].gpio_name, 0);

		if(!gpio_is_valid(pconfig->gpio[i].gpio_no)) {

			pr_err("%s: hset get gpio %s fail\n",__func__,pconfig->gpio[i].gpio_name);
			return 0;
		} else
			pr_info("%s: hset gpio %s no %d\n",__func__,pconfig->gpio[i].gpio_name,pconfig->gpio[i].gpio_no);

		ret = gpio_request(pconfig->gpio[i].gpio_no, pconfig->gpio[i].gpio_name);
		if (ret) {
			pr_err(	"%s: Failed to request gpio %d error %d\n",
				__func__, pconfig->gpio[i].gpio_no, ret);
#if 0
			for(--i; i >= 0; i--)
				gpio_free(pconfig->gpio[i].gpio_no);

			return ret;
#endif
		}

		gpio_direction_output(pconfig->gpio[i].gpio_no, 0);
	}

	pconfig->init = 1;
	return 0;
}
#endif

static void msm8994_dtparse_and_config_reserved_gpio(struct platform_device *pdev)
{
	int i, ret;
	struct reserved_gpio_config *pconfig = &htc_reserved_gpio_config;

	if(!pdev)
		return;

	for(i = 0; i < ARRAY_SIZE(pconfig->gpio); i++) {

		if(!pconfig->gpio[i].gpio_name) {
			pr_err("%s: %d reserved gpio name is null\n",__func__,i);
			return;
		}

		pconfig->gpio[i].gpio_no = of_get_named_gpio(pdev->dev.of_node, \
						pconfig->gpio[i].gpio_name, 0);

		if(!gpio_is_valid(pconfig->gpio[i].gpio_no)) {
			pr_err("%s: get reserved gpio %s fail\n",__func__,pconfig->gpio[i].gpio_name);
			return;
		} else
			pr_info("%s: got reserved gpio %s no %d\n",__func__,pconfig->gpio[i].gpio_name,pconfig->gpio[i].gpio_no);

		ret = gpio_request(pconfig->gpio[i].gpio_no, pconfig->gpio[i].gpio_name);
		if (ret) {
			pr_err(	"%s: Failed to request gpio %d error %d\n",
				__func__, pconfig->gpio[i].gpio_no, ret);
		}

		gpio_direction_input(pconfig->gpio[i].gpio_no);
	}

	pconfig->init = 1;
	return;
}

static int msm8994_init_ftm_btpcm(struct platform_device *pdev,
				struct aud_btpcm_config *pconfig)
{
	int i;

	if(!pconfig || !pdev)
		return 0;

	for(i = 0; i < ARRAY_SIZE(pconfig->gpio); i++) {
		if(!pconfig->gpio[i].gpio_name) {
			pr_err("%s: %d btpcm gpio name is null\n",__func__,i);
			return 0;
		}

		pconfig->gpio[i].gpio_no = of_get_named_gpio(pdev->dev.of_node,
						pconfig->gpio[i].gpio_name, 0);

		if(!gpio_is_valid(pconfig->gpio[i].gpio_no)) {
			pr_err("%s: btpcm get gpio %s failed\n", __func__, pconfig->gpio[i].gpio_name);
			return -1;
		} else
			pr_info("%s: btpcm gpio %s no %d\n",__func__, pconfig->gpio[i].gpio_name, pconfig->gpio[i].gpio_no);
	}
	pconfig->init = 1;
	return 0;
}

static int aud_ftm_btpcm_gpio_config(void *user_data, enum AUD_FTM_BTPCM_MODE mode)
{
	int ret = 0;
	struct platform_device *pdev = (struct platform_device *)user_data;
	struct aud_btpcm_config *pconfig = &htc_aud_btpcm_config;

	if (!pdev)
		return -EINVAL;

	if (pconfig->init == 0) {
		pr_err("%s: btpcm gpio config not init", __func__);
		return -EPERM;
	}

	if (mode == AUD_FTM_BTPCM_MODE_PCM) {
		if (pconfig->mode == AUD_FTM_BTPCM_MODE_GPIO) {
			ftm_auxpcm_release_pinctrl(pdev);
			ret = msm_auxpcm_get_pinctrl(pdev, SEC_MI2S_PCM);
			if (ret < 0) {
				pr_err("%s: msm_auxpcm_get_pinctrl failed with %d\n", __func__, ret);
			} else {
				pconfig->mode = AUD_FTM_BTPCM_MODE_PCM;
				pr_info("%s: AUD_FTM_BTPCM_MODE_PCM done (%d)", __func__, ret);
			}
		}
	} else if (mode == AUD_FTM_BTPCM_MODE_GPIO) {
		if (pconfig->mode == AUD_FTM_BTPCM_MODE_PCM) {
			msm_auxpcm_release_pinctrl(pdev, SEC_MI2S_PCM);
			ret = ftm_auxpcm_get_pinctrl(pdev);
			if (ret < 0) {
				pr_err("%s: ftm_auxpcm_get_pinctrl failed with %d\n", __func__, ret);
			} else {
				pconfig->mode = AUD_FTM_BTPCM_MODE_GPIO;
				pr_info("%s: AUD_FTM_BTPCM_MODE_GPIO done (%d)", __func__, ret);
			}
		}
	} else {
		pr_err("%s: Unknown BTPCM mode %d", __func__, mode);
		ret = -EINVAL;
	}
	return ret;
}

static int aud_ftm_btpcm_gpio_read(void *user_data, int *state)
{
	int i;
	struct aud_btpcm_config *pconfig = &htc_aud_btpcm_config;

	if (state == NULL)
		return -EINVAL;

	if (htc_aud_btpcm_config.init == 0 || pconfig->mode != AUD_FTM_BTPCM_MODE_GPIO)
		return -EPERM;

	for(i = 0; i < ARRAY_SIZE(pconfig->gpio); i++)
		state[i] = gpio_get_value(pconfig->gpio[i].gpio_no);

	return 0;
}

static struct aud_ftm_btpcm_func_t aud_ftm_btpcm_func = {
	.gpio_config = aud_ftm_btpcm_gpio_config,
	.gpio_read = aud_ftm_btpcm_gpio_read,
};

static int msm8994_dtparse_rcv(struct platform_device *pdev,
				struct rcv_config *pconfig)
{
	int i, ret;

	if(!pconfig || !pdev)
		return 0;

	for(i = 0; i < ARRAY_SIZE(pconfig->gpio); i++) {

		if(!pconfig->gpio[i].gpio_name) {
			pr_err("%s: %d rcv gpio name is null\n",__func__,i);
			return 0;
		}

		pconfig->gpio[i].gpio_no = of_get_named_gpio(pdev->dev.of_node, \
						pconfig->gpio[i].gpio_name, 0);

		if(!gpio_is_valid(pconfig->gpio[i].gpio_no)) {

			pr_err("%s: rcv get gpio %s fail\n",__func__,pconfig->gpio[i].gpio_name);
			continue;//Acadia only uses one gpio(rcv-gpio-en)
		} else
			pr_info("%s: rcv gpio %s no %d\n",__func__,pconfig->gpio[i].gpio_name,pconfig->gpio[i].gpio_no);

		ret = gpio_request(pconfig->gpio[i].gpio_no, pconfig->gpio[i].gpio_name);
		if (ret) {
			pr_err("%s: Failed to request gpio %d error %d\n",
				__func__, pconfig->gpio[i].gpio_no, ret);
#if 0
			for(--i; i >= 0; i--)
				gpio_free(pconfig->gpio[i].gpio_no);

			return ret;
#endif
		}

		gpio_direction_output(pconfig->gpio[i].gpio_no, 0);
	}

	pconfig->init = 1;
	return 0;
}

static int tfa_pin_init(struct pinctrl *pinctrl)
{
       struct pinctrl_state *set_state;
       int retval;

       set_state = pinctrl_lookup_state(pinctrl, "tfa_gpio_init");
       if (IS_ERR(set_state)) {
               pr_err("cannot get tfa_gpio_init pinctrl state\n");
               return PTR_ERR(set_state);
       }

       retval = pinctrl_select_state(pinctrl, set_state);
       if (retval) {
               pr_err("cannot set tfa_gpio_init pinctrl gpio state\n");
               return retval;
       }
       pr_info("tfa init done\n");
       return 0;
}

static int tfa_dt_parser(struct platform_device *pdev){
	int ret = 0;
	struct pinctrl *tfa_gpio_pinctrl = NULL;
	pr_debug("%s: start parser\n", __func__);

	tfa_gpio_pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(tfa_gpio_pinctrl)) {
			pr_err("%s: Target does not have pinctrl\n", __func__);
	}

	if (tfa_gpio_pinctrl) {
		ret = tfa_pin_init(tfa_gpio_pinctrl);
		if (ret) {
			pr_err("%s: tfa gpios init fail, ret=%d\n", __func__, ret);
		}
	}

	pr_debug("%s: parse end\n", __func__);

	return 0;
}

#ifdef CONFIG_USE_CODEC_MBHC
static int headset_pin_init(struct pinctrl *pinctrl)
{
       struct pinctrl_state *set_state;
       int retval;

       set_state = pinctrl_lookup_state(pinctrl, "headset_gpio_init");
       if (IS_ERR(set_state)) {
               pr_err("cannot get headset_gpio_init pinctrl state\n");
               return PTR_ERR(set_state);
       }

       retval = pinctrl_select_state(pinctrl, set_state);
       if (retval) {
               pr_err("cannot set headset_gpio_init pinctrl gpio state\n");
               return retval;
       }
       pr_info("headset init done\n");
       return 0;
}

static int headset_dt_parser(struct platform_device *pdev, struct wcd9xxx_mbhc_config *wcd_mbhc_cfg){
	int i = 0;
	int id_gpio_count = 0;
	int switch_gpio_count = 0;
	uint32_t min_max_array[2]; //[0] : min [1] :max
	const char *id_gpio_name = "htc,aud_gpio_ids";
	const char *switch_gpio_name = "htc,aud_gpio_switches";
	const char *hsmic_bias_string = "htc,hsmic_2v85_en";
	const char *adapter_35mm_string = "htc,adapter_35mm_threshold";
	const char *adapter_25mm_string = "htc,adapter_25mm_threshold";
	const char *adc_channel_string = "htc,headset_adc_channel";
	const char *name;

	int ret = 0;
	struct pinctrl *headset_gpio_pinctrl = NULL;
	pr_debug("%s: start parser\n", __func__);

	id_gpio_count = of_property_count_strings(pdev->dev.of_node, id_gpio_name);
	switch_gpio_count = of_property_count_strings(pdev->dev.of_node, switch_gpio_name);
	if (IS_ERR_VALUE(id_gpio_count) && IS_ERR_VALUE(switch_gpio_count)) {
		pr_err("%s: Failed to get %s = %d, %s = %d\n", __func__,
			id_gpio_name, id_gpio_count,
			switch_gpio_name, switch_gpio_count);
		return -EINVAL;
	}

	if (id_gpio_count != TYPEC_ID_MAX) {
		id_gpio_count = TYPEC_ID_MAX;
		pr_err("%s: id_gpio_count incorrect, please check dts config", __func__);
		return -EINVAL;
	}

	if (switch_gpio_count != HEADSET_SWITCH_MAX) {
		switch_gpio_count = TYPEC_ID_MAX;
		pr_err("%s: id_gpio_count incorrect, please check dts config", __func__);
		return -EINVAL;
	}

	for (i = 0; i < id_gpio_count; i++){
		ret = of_property_read_string_index(pdev->dev.of_node, id_gpio_name, i, &name);
		if (ret) {
			pr_err("%s: of read string %s index %d error %d\n",
				__func__, id_gpio_name, i, ret);
			return ret;
		}

		wcd_mbhc_cfg->htc_headset_cfg.id_gpio[i] = of_get_named_gpio(pdev->dev.of_node,
			name, 0);
		if (gpio_is_valid(wcd_mbhc_cfg->htc_headset_cfg.id_gpio[i])) {
			pr_info("%s: gpio %s parse success gpio no %d\n", __func__,
				name, wcd_mbhc_cfg->htc_headset_cfg.id_gpio[i]);
			ret = gpio_request_one(wcd_mbhc_cfg->htc_headset_cfg.id_gpio[i],
				GPIOF_DIR_IN, name);
			if (ret) {
				pr_err("%s: gpio %s request failed with err %d\n", __func__,
					name, ret);
				return ret;
			}
		} else {
			pr_err("%s: gpio %s parse fail\n", __func__, name);
			return -EINVAL;
		}
	}

	for (i = 0; i < switch_gpio_count; i++){
		ret = of_property_read_string_index(pdev->dev.of_node, switch_gpio_name, i, &name);
		if (ret) {
			pr_err("%s: of read string %s index %d error %d\n",
				__func__, switch_gpio_name, i, ret);
			return ret;
		}
		wcd_mbhc_cfg->htc_headset_cfg.switch_gpio[i] = of_get_named_gpio(pdev->dev.of_node,
			name, 0);
		if (gpio_is_valid(wcd_mbhc_cfg->htc_headset_cfg.switch_gpio[i])) {
			pr_info("%s: gpio %s parse success gpio no %d\n", __func__,
				name, wcd_mbhc_cfg->htc_headset_cfg.switch_gpio[i]);
			ret = gpio_request_one(wcd_mbhc_cfg->htc_headset_cfg.switch_gpio[i],
				GPIOF_OUT_INIT_LOW, name);
			if (ret) {
				pr_err("%s: gpio %s request failed with err %d\n", __func__,
					name, ret);
				return ret;
			}
		} else {
			pr_err("%s: gpio %s parse fail\n", __func__, name);
			return -EINVAL;
		}
	}

	wcd_mbhc_cfg->htc_headset_cfg.ext_micbias = of_get_named_gpio(pdev->dev.of_node,
		hsmic_bias_string, 0);
	if (gpio_is_valid(wcd_mbhc_cfg->htc_headset_cfg.ext_micbias)) {
		pr_info("%s: gpio %s parse success gpio no %d\n", __func__,
			hsmic_bias_string,
			wcd_mbhc_cfg->htc_headset_cfg.ext_micbias);
		ret = gpio_request_one(wcd_mbhc_cfg->htc_headset_cfg.ext_micbias,
			GPIOF_OUT_INIT_LOW, hsmic_bias_string);
		if (ret) {
			pr_err("%s: gpio %s request failed with err %d\n", __func__,
				hsmic_bias_string, ret);
			return ret;
		}
	} else {
		pr_err("%s: gpio %s parse fail\n", __func__,
			hsmic_bias_string);
		return -EINVAL;
	}

	headset_gpio_pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(headset_gpio_pinctrl)) {
			pr_err("%s: Target does not have pinctrl\n", __func__);
	}

	if (headset_gpio_pinctrl) {
		ret = headset_pin_init(headset_gpio_pinctrl);
		if (ret) {
			pr_err("%s: headset gpios init fail, ret=%d\n", __func__, ret);
		}
	}

	ret = of_property_read_u32_array(pdev->dev.of_node,
			adapter_35mm_string,
			min_max_array, 2);

	if (ret < 0) {
			pr_err("%s: adapter_35mm_string parser err\n", __func__);
		} else {
			wcd_mbhc_cfg->htc_headset_cfg.adc_35mm_min = min_max_array[0];
			wcd_mbhc_cfg->htc_headset_cfg.adc_35mm_max = min_max_array[1];
			pr_info("%s: adapter_35mm_string  min: = %d max: = %d", __func__, wcd_mbhc_cfg->htc_headset_cfg.adc_35mm_min , wcd_mbhc_cfg->htc_headset_cfg.adc_35mm_max);
		}


	ret = of_property_read_u32_array(pdev->dev.of_node,
			adapter_25mm_string,
			min_max_array, 2);

	if (ret < 0) {
			pr_err("%s: adapter_25mm_string parser err\n", __func__);
		} else {
			wcd_mbhc_cfg->htc_headset_cfg.adc_25mm_min = min_max_array[0];
			wcd_mbhc_cfg->htc_headset_cfg.adc_25mm_max = min_max_array[1];
			pr_info("%s: adapter_25mm_string  min: = %d max: = %d", __func__, wcd_mbhc_cfg->htc_headset_cfg.adc_25mm_min , wcd_mbhc_cfg->htc_headset_cfg.adc_25mm_max);
		}

	ret = of_property_read_u32(pdev->dev.of_node, adc_channel_string, &wcd_mbhc_cfg->htc_headset_cfg.adc_channel);
	if (ret < 0) {
		pr_err("%s: adc channel parser err\n", __func__);
	} else
		pr_info("%s: adc_channel = %d", __func__, wcd_mbhc_cfg->htc_headset_cfg.adc_channel);

	wcd_mbhc_cfg->htc_headset_cfg.IsXaMotherboard = of_property_read_bool(pdev->dev.of_node, "acadia-xa");
	wcd_mbhc_cfg->htc_headset_cfg.get_adc_value = hs_qpnp_remote_adc;
	wcd_mbhc_cfg->htc_headset_cfg.htc_headset_init = true;

	pr_debug("%s: parse end\n", __func__);

	return 0;
}
#endif
//htc audio --

static int msm8994_asoc_machine_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &snd_soc_card_msm8994;
	struct msm8994_asoc_mach_data *pdata;
#ifdef CONFIG_USE_CODEC_MBHC //htc_audio
	const char *mbhc_audio_jack_type = NULL;
#endif
	int ret, i; //HTC_AUD
	pr_info("%s:++\n",__func__);
//htc audio ++
	mutex_init(&htc_audiosphere_enable_mutex);
	mutex_init(&htc_audiosphere_strength_mutex);
//htc audio --


	if (!pdev->dev.of_node) {
		dev_err(&pdev->dev, "No platform supplied from device tree\n");
		return -EINVAL;
	}

	pdata = devm_kzalloc(&pdev->dev,
			sizeof(struct msm8994_asoc_mach_data), GFP_KERNEL);
	if (!pdata) {
		dev_err(&pdev->dev, "Can't allocate msm8994_asoc_mach_data\n");
		return -ENOMEM;
	}

	card->dev = &pdev->dev;
	platform_set_drvdata(pdev, card);
	snd_soc_card_set_drvdata(card, pdata);

	ret = snd_soc_of_parse_card_name(card, "qcom,model");
	if (ret) {
		dev_err(&pdev->dev, "parse card name failed, err:%d\n",
			ret);
		goto err;
	}

	ret = snd_soc_of_parse_audio_routing(card,
			"qcom,audio-routing");
	if (ret) {
		dev_err(&pdev->dev, "parse audio routing failed, err:%d\n",
			ret);
		goto err;
	}
	ret = of_property_read_u32(pdev->dev.of_node,
			"qcom,tomtom-mclk-clk-freq", &pdata->mclk_freq);
	if (ret) {
		dev_err(&pdev->dev,
			"Looking up %s property in node %s failed, err%d\n",
			"qcom,tomtom-mclk-clk-freq",
			pdev->dev.of_node->full_name, ret);
		goto err;
	}

	if (pdata->mclk_freq != 9600000) {
		dev_err(&pdev->dev, "unsupported mclk freq %u\n",
			pdata->mclk_freq);
		ret = -EINVAL;
		goto err;
	}
//htc audio ++
//enable haset mic bias for bring up
#if MSM8994_BRINGUP
//Enable LED_3V3
	pdata->led_gpio = of_get_named_gpio(pdev->dev.of_node,
				"qcom,led_gpio", 0);
	pr_info("%s: pdata->hset-bias-gpio %d\n",__func__,pdata->led_gpio);
	if (pdata->led_gpio < 0) {
		dev_err(&pdev->dev,
			"Looking up %s property in node %s failed %d\n",
			"qcom, led_gpio", pdev->dev.of_node->full_name,
			pdata->led_gpio);
		ret = -ENODEV;
	}

	pdata->hset_bias_gpio = of_get_named_gpio(pdev->dev.of_node,
				"qcom,hset-bias-gpio", 0);
	pr_info("%s: pdata->hset-bias-gpio %d\n",__func__,pdata->hset_bias_gpio);
	if (pdata->hset_bias_gpio < 0) {
		dev_err(&pdev->dev,
			"Looking up %s property in node %s failed %d\n",
			"qcom, hset-bias-gpio", pdev->dev.of_node->full_name,
			pdata->hset_bias_gpio);
		ret = -ENODEV;
	}
	ret = msm8994_enable_hset_bias(card);
	if (ret)
		pr_err("%s: set pdata->hset-bias-gpio %d failed\n",__func__,pdata->hset_bias_gpio);
#endif

#if MSM8994_LDO_WR

	pdata->hph_en_gpio = of_get_named_gpio(pdev->dev.of_node,
				"qcom,hph-en-gpio", 0);
	pr_info("%s: pdata->hph_en_gpio %d\n",__func__,pdata->hph_en_gpio);
	if (pdata->hph_en_gpio < 0) {
		pr_info("Looking up %s property in node %s failed %d\n",
			"qcom, hph_en_gpio", pdev->dev.of_node->full_name,
			pdata->hph_en_gpio);
	} else {
		//Enable hph en gpio, otherwise AKM will hold I2C bus then cause I2C bus error
		ret = msm8994_enable_hph(card);
		if (ret)
			pr_err("%s: set pdata->hph_en_gpio %d failed\n",__func__,pdata->hph_en_gpio);
	}
	hph_en_ready = 1;
#endif
//htc_audio ++
#ifdef CONFIG_USE_CODEC_MBHC
	if(!mbhc_cfg.htc_headset_cfg.htc_headset_init)
	headset_dt_parser(pdev, &mbhc_cfg);
#endif

       tfa_dt_parser(pdev);
//htc_audio --
	pdata->mclk_gpio = of_get_named_gpio(pdev->dev.of_node,
				"qcom,cdc-mclk-gpios", 0);
	if (pdata->mclk_gpio < 0) {
		dev_err(&pdev->dev,
			"Looking up %s property in node %s failed %d\n",
			"qcom, cdc-mclk-gpios", pdev->dev.of_node->full_name,
			pdata->mclk_gpio);
		ret = -ENODEV;
		goto err;
	}

	ret = msm8994_prepare_codec_mclk(card);
	if (ret) {
		dev_err(&pdev->dev, "prepare_codec_mclk failed, err:%d\n",
			ret);
		goto err;
	}
//htc audio ++
#ifdef CONFIG_USE_CODEC_MBHC
	mbhc_cfg.mclk_rate = pdata->mclk_freq;
#endif
	if (of_property_read_bool(pdev->dev.of_node, "acadia-tfa9891-support")) {
		for (i = 0; i< ARRAY_SIZE(msm8994_common_dai_links); i++) {
			if (!strncmp(msm8994_common_dai_links[i].name, LPASS_BE_QUAT_MI2S_RX, sizeof(LPASS_BE_QUAT_MI2S_RX))) {
				msm8994_common_dai_links[i].codec_name = "tfa98xx.24-0034"; // tfa98xx.24-0034; 24 = i2c-bus number in decimal
				msm8994_common_dai_links[i].codec_dai_name = "tfa98xx-aif-18-34"; // tfa98xx-aif-18-34; 18 = i2c-bus number in hexadecimal
			}
		}
	}
//htc audio --
	if (of_property_read_bool(pdev->dev.of_node, "qcom,hdmi-audio-rx")) {
		dev_info(&pdev->dev, "%s: hdmi audio support present\n",
				__func__);
		memcpy(msm8994_dai_links, msm8994_common_dai_links,
			sizeof(msm8994_common_dai_links));
		memcpy(msm8994_dai_links + ARRAY_SIZE(msm8994_common_dai_links),
			msm8994_hdmi_dai_link, sizeof(msm8994_hdmi_dai_link));

		card->dai_link	= msm8994_dai_links;
		card->num_links	= ARRAY_SIZE(msm8994_dai_links);
	} else {
		dev_info(&pdev->dev, "%s: No hdmi audio support\n", __func__);

		card->dai_link	= msm8994_common_dai_links;
		card->num_links	= ARRAY_SIZE(msm8994_common_dai_links);
	}
	mutex_init(&cdc_mclk_mutex);
	atomic_set(&prim_auxpcm_rsc_ref, 0);
	atomic_set(&sec_auxpcm_rsc_ref, 0);
	spdev = pdev;


	ret = msm8994_populate_dai_link_component_of_node(card);
	if (ret) {
		ret = -EPROBE_DEFER;
		goto err;
	}

	ret = snd_soc_register_card(card);
	if (ret == -EPROBE_DEFER) {
		goto err;
	} else if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n",
			ret);
		goto err;
	}
#ifdef CONFIG_USE_CODEC_MBHC //htc_audio
	ret = of_property_read_string(pdev->dev.of_node,
		"qcom,mbhc-audio-jack-type", &mbhc_audio_jack_type);
	if (ret) {
		dev_dbg(&pdev->dev, "Looking up %s property in node %s failed",
			"qcom,mbhc-audio-jack-type",
			pdev->dev.of_node->full_name);
		mbhc_cfg.hw_jack_type = FOUR_POLE_JACK;
		mbhc_cfg.enable_anc_mic_detect = false;
		dev_dbg(&pdev->dev, "Jack type properties set to default");
	} else {
		if (!strcmp(mbhc_audio_jack_type, "4-pole-jack")) {
			mbhc_cfg.hw_jack_type = FOUR_POLE_JACK;
			mbhc_cfg.enable_anc_mic_detect = false;
			dev_dbg(&pdev->dev, "This hardware has 4 pole jack");
		} else if (!strcmp(mbhc_audio_jack_type, "5-pole-jack")) {
			mbhc_cfg.hw_jack_type = FIVE_POLE_JACK;
			mbhc_cfg.enable_anc_mic_detect = true;
			dev_dbg(&pdev->dev, "This hardware has 5 pole jack");
		} else if (!strcmp(mbhc_audio_jack_type, "6-pole-jack")) {
			mbhc_cfg.hw_jack_type = SIX_POLE_JACK;
			mbhc_cfg.enable_anc_mic_detect = true;
			dev_dbg(&pdev->dev, "This hardware has 6 pole jack");
		} else {
			mbhc_cfg.hw_jack_type = FOUR_POLE_JACK;
			mbhc_cfg.enable_anc_mic_detect = false;
			dev_dbg(&pdev->dev, "Unknown value, set to default");
		}
	}
#endif
	/* Parse US-Euro gpio info from DT. Report no error if us-euro
	 * entry is not found in DT file as some targets do not support
	 * US-Euro detection
	 */
	pdata->us_euro_gpio = of_get_named_gpio(pdev->dev.of_node,
				"qcom,us-euro-gpios", 0);
	if (pdata->us_euro_gpio < 0) {
		dev_info(&pdev->dev, "property %s not detected in node %s",
			"qcom,us-euro-gpios",
			pdev->dev.of_node->full_name);
	} else {
		dev_dbg(&pdev->dev, "%s detected %d",
			"qcom,us-euro-gpios", pdata->us_euro_gpio);
#ifdef CONFIG_USE_CODEC_MBHC //htc_audio
		mbhc_cfg.swap_gnd_mic = msm8994_swap_gnd_mic;
#endif
	}
//htc audio ++
#ifndef CONFIG_USE_CODEC_MBHC
	ret = msm8994_prepare_us_euro(card);
	if (ret)
		dev_info(&pdev->dev, "msm8994_prepare_us_euro failed (%d)\n",
			ret);
#endif
	mutex_init(&htc_amp_mutex);

	msm8994_dtparse_rcv(pdev, &htc_rcv_config);
#if 0
	htc_rcv_amp_ctl(1);
	msm8994_dtparse_hset_amp(pdev, &htc_hset_amp_config);
	htc_hset_amp_ctl(1);
#endif

	ret = msm8994_init_ftm_btpcm(pdev, &htc_aud_btpcm_config);
	if (ret < 0) {
		pr_warn("%s: init ftm btpcm failed with %d (Non-issue for non-BRCM BT chip.)", __func__, ret);
	}

//htc audio --
	/* Parse pinctrl info for AUXPCM, if defined */
	ret = msm_auxpcm_get_pinctrl(pdev, SEC_MI2S_PCM);
	if (!ret) {
		pr_debug("%s: Auxpcm pinctrl parsing successful\n", __func__);
	} else {
		dev_info(&pdev->dev,
			"%s: Parsing pinctrl failed with %d. Cannot use Auxpcm Ports\n",
			__func__, ret);
		goto err;
	}

//htc audio ++
	atomic_set(&tert_mi2s_rsc_ref, 0);
	atomic_set(&quat_mi2s_rsc_ref, 0);

	ret = of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);

	if(ret) {
		pr_err("register aud pinctrl device fail\n");
	}

#if 0
	/* Parse pinctrl info for MI2S ports, if defined */
	ret = msm_mi2s_get_pinctrl(pdev, PRI_MI2S_PCM);
	if (!ret) pr_debug("%s: Primary MI2S pinctrl parsing successful\n", __func__);
	if(ret) {
		dev_info(&pdev->dev, "%s: Priamry MI2S Parsing pinctrl failed with %d. Cannot use MI2S Ports\n", __func__, ret);
		goto err1;
	}

	atomic_set(&tert_mi2s_rsc_ref, 0);
	atomic_set(&quat_mi2s_rsc_ref, 0);

	ret = msm_mi2s_get_pinctrl(pdev, TERT_MI2S_PCM);
	if (!ret) pr_debug("%s: Tertiary MI2S pinctrl parsing successful\n", __func__);
	if(ret) {
		dev_info(&pdev->dev, "%s: Tertiary MI2S Parsing pinctrl failed with %d. Cannot use MI2S Ports\n", __func__, ret);
		goto err2;
	}
	ret = msm_mi2s_get_pinctrl(pdev, QUAT_MI2S_PCM);
	if (!ret) pr_debug("%s: Quaternary MI2S pinctrl parsing successful\n", __func__);
	if(ret) { 
		dev_info(&pdev->dev, "%s: Quaternary MI2S Parsing pinctrl failed with %d. Cannot use MI2S Ports\n", __func__, ret);
		goto err3;
	}
#endif

	ret = apq8094_db_device_init();
	if (ret) {
		pr_err("%s: DB8094 init ext devices stat IRQ failed (%d)\n",
				__func__, ret);
		goto err; // htc audio
	}

//htc audio ++
	msm8994_dtparse_and_config_reserved_gpio(pdev);

	if (htc_aud_btpcm_config.init == 1) {
		aud_ftm_btpcm_func.user_data = (void *)pdev;
		aud_ftm_func_register(&aud_ftm_btpcm_func);
	}
//htc audio --

	return 0;

//htc audio ++
#if 0
err3:
	msm_mi2s_release_pinctrl(pdev, TERT_MI2S_PCM);
err2:
	msm_mi2s_release_pinctrl(pdev, PRI_MI2S_PCM);
err1:
	msm_auxpcm_release_pinctrl(pdev, SEC_MI2S_PCM);
#endif
//htc audio --
err:
//htc audio ++
//enable hset bias for bring up
#if MSM8994_BRINGUP
	if (pdata->hset_bias_gpio > 0) {
		dev_dbg(&pdev->dev, "%s free gpio %d\n",
			__func__, pdata->hset_bias_gpio);
		gpio_free(pdata->hset_bias_gpio);
		pdata->hset_bias_gpio = 0;
	}

	if (pdata->led_gpio > 0) {
		dev_dbg(&pdev->dev, "%s free gpio %d\n",
			__func__, pdata->led_gpio);
		gpio_free(pdata->led_gpio);
		pdata->led_gpio = 0;
	}
#endif
//htc audio --
	if (pdata->mclk_gpio > 0) {
		dev_dbg(&pdev->dev, "%s free gpio %d\n",
			__func__, pdata->mclk_gpio);
		gpio_free(pdata->mclk_gpio);
		pdata->mclk_gpio = 0;
	}
	if (pdata->us_euro_gpio > 0) {
		dev_dbg(&pdev->dev, "%s free us_euro gpio %d\n",
			__func__, pdata->us_euro_gpio);
		gpio_free(pdata->us_euro_gpio);
		pdata->us_euro_gpio = 0;
	}
	devm_kfree(&pdev->dev, pdata);
	return ret;
}

static int msm8994_asoc_machine_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);

	if (gpio_is_valid(ext_us_amp_gpio))
		gpio_free(ext_us_amp_gpio);

	gpio_free(pdata->mclk_gpio);
#if MSM8994_BRINGUP
	gpio_free(pdata->hset_bias_gpio); //htc audio, enable hset bias for bring up
	gpio_free(pdata->led_gpio); //htc audio, enable led 3v3 for bring up
#endif
	gpio_free(pdata->us_euro_gpio);

	msm8994_audio_plug_device_remove(msm8994_liquid_dock_dev);

	msm8994_audio_plug_device_remove(apq8094_db_ext_bp_out_dev);
	msm8994_audio_plug_device_remove(apq8094_db_ext_fp_in_dev);
	msm8994_audio_plug_device_remove(apq8094_db_ext_fp_out_dev);

	msm_auxpcm_release_pinctrl(pdev, SEC_MI2S_PCM);
//htc audio ++
#if 0
	msm_mi2s_release_pinctrl(pdev, PRI_MI2S_PCM);
	msm_mi2s_release_pinctrl(pdev, TERT_MI2S_PCM);
	msm_mi2s_release_pinctrl(pdev, QUAT_MI2S_PCM);
#endif
//htc audio --
	snd_soc_unregister_card(card);

	return 0;
}

static const struct of_device_id msm8994_asoc_machine_of_match[]  = {
	{ .compatible = "qcom,msm8994-asoc-snd", },
	{},
};

static struct platform_driver msm8994_asoc_machine_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = msm8994_asoc_machine_of_match,
	},
	.probe = msm8994_asoc_machine_probe,
	.remove = msm8994_asoc_machine_remove,
};
//htc audio ++
static const struct of_device_id msm_aud_pinctrl_dev_dt_match[] = {
	{ .compatible = "aud_pinctrl_dev", },
	{ }
};
MODULE_DEVICE_TABLE(of, msm_aud_pinctrl_dev_dt_match);

static int msm_aud_pinctrl_dev_probe(struct platform_device *pdev)
{
	uint32_t dev_id = 0, type = 0;
	int ret = 0;

	ret = of_property_read_u32(pdev->dev.of_node,
			"pinctrl_type", &type);

	if(ret) {
		pr_err("can't get pinctrl type\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(pdev->dev.of_node,
			"dev_id", &dev_id);

	if(ret) {
		pr_err("get dev id fail\n");
		return -EINVAL;
	}

	pr_info("probe htc pinctrl type %d dev_id %d\n",type, dev_id);

	switch(type) {

		case MI2S_PIN_TYPE:
			ret = msm_mi2s_get_pinctrl(to_platform_device(pdev->dev.parent), pdev, dev_id);
			break;
		case AUXPCM_PIN_TYPE:
			//msm_auxpcm_get_pinctrl(to_platform_device(pdev->dev.parent), pdev, dev_id);
			break;
		case WCD_PIN_TYPE:
			msm_wcd_get_pinctrl(to_platform_device(pdev->dev.parent), pdev, dev_id);
			break;
		default:
			pr_err("%s: unkown pinctrl type %d\n",__func__, type);
			break;
	}

	if(ret) {
		pr_err("msm_mi2s_get_pinctrl fail for dev %d\n",dev_id);
	}

	return 0;
}

static int msm_aud_pinctrl_dev_remove(struct platform_device *pdev)
{
	uint32_t dev_id = 0, type = 0;
	int ret = 0;

	ret = of_property_read_u32(pdev->dev.of_node,
			"pinctrl_type", &type);

	if(ret) {
		pr_err("can't get pinctrl type\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(pdev->dev.of_node,
			"dev_id", &dev_id);

	if(ret) {
		pr_err("get dev id fail\n");
		return -EINVAL;
	}

	switch(type) {

		case MI2S_PIN_TYPE:
			msm_mi2s_release_pinctrl(to_platform_device(pdev->dev.parent), dev_id);
			break;

		case AUXPCM_PIN_TYPE:
			//msm_auxpcm_release_pinctrl(to_platform_device(pdev->dev.parent), dev_id);
			break;

		case WCD_PIN_TYPE:
			break;
		default:
			pr_err("%s: unkown pinctrl type %d\n",__func__, type);
			break;
	}
	return 0;
}

static struct platform_driver msm_aud_pinctrl_dev = {
	.probe  = msm_aud_pinctrl_dev_probe,
	.remove = msm_aud_pinctrl_dev_remove,
	.driver = {
		.name = "aud_pinctrl_dev",
		.owner = THIS_MODULE,
		.of_match_table = msm_aud_pinctrl_dev_dt_match,
	},
};

//module_platform_driver(msm8994_asoc_machine_driver);
static int __init msm8994_audio_init(void)
{
        int ret = 0;

	htc_acoustic_register_ops(&acoustic);
	platform_driver_register(&msm8994_asoc_machine_driver);
//htc audio ++
	platform_driver_register(&msm_aud_pinctrl_dev);
//htc audio --
	return ret;

}
late_initcall(msm8994_audio_init);

static void __exit msm8994_audio_exit(void)
{
	pr_info("%s", __func__);
//htc audio ++
	platform_driver_unregister(&msm_aud_pinctrl_dev);
//htc audio --
	platform_driver_unregister(&msm8994_asoc_machine_driver);
}
module_exit(msm8994_audio_exit);
//htc audio --
MODULE_DESCRIPTION("ALSA SoC msm");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_DEVICE_TABLE(of, msm8994_asoc_machine_of_match);
