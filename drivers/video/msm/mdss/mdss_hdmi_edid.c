/* Copyright (c) 2010-2015, The Linux Foundation. All rights reserved.
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

#include <linux/io.h>
#include <linux/types.h>
#include <linux/stat.h>
#include <linux/slab.h>
#include <linux/device.h>
#include "mdss_hdmi_edid.h"

#define DBC_START_OFFSET 4

#define MAX_DATA_BLOCK_SIZE 31

#define HDMI_VSDB_3D_EVF_DATA_OFFSET(vsd) \
	(!((vsd)[8] & BIT(7)) ? 9 : (!((vsd)[8] & BIT(6)) ? 11 : 13))

#define MAX_NUMBER_ADB                  5
#define MAX_AUDIO_DATA_BLOCK_SIZE	30
#define MAX_SPKR_ALLOC_DATA_BLOCK_SIZE	3

#define MAX_EDID_BLOCK_SIZE (0x80 * 5)

#define MAX_EDID_READ_RETRY	5

#define BUFF_SIZE_3D 128

#define DTD_MAX			0x04
#define DTD_OFFSET		0x36
#define DTD_SIZE		0x12
#define REVISION_OFFSET		0x13
#define EDID_REVISION_FOUR	0x04

enum data_block_types {
	RESERVED,
	AUDIO_DATA_BLOCK,
	VIDEO_DATA_BLOCK,
	VENDOR_SPECIFIC_DATA_BLOCK,
	SPEAKER_ALLOCATION_DATA_BLOCK,
	VESA_DTC_DATA_BLOCK,
	RESERVED2,
	USE_EXTENDED_TAG
};

struct hdmi_edid_sink_data {
	u32 disp_mode_list[HDMI_VFRMT_MAX];
	u32 disp_3d_mode_list[HDMI_VFRMT_MAX];
	u32 disp_multi_3d_mode_list[16];
	u32 disp_multi_3d_mode_list_cnt;
	u32 num_of_elements;
	u32 preferred_video_format;
};

struct hdmi_edid_ctrl {
	u8 pt_scan_info;
	u8 it_scan_info;
	u8 ce_scan_info;
	u16 physical_address;
	u32 video_resolution; 
	u32 sink_mode; 
	u16 audio_latency;
	u16 video_latency;
	u32 present_3d;
	u32 page_id;
	u8 audio_data_block[MAX_NUMBER_ADB * MAX_AUDIO_DATA_BLOCK_SIZE];
	int adb_size;
	u8 spkr_alloc_data_block[MAX_SPKR_ALLOC_DATA_BLOCK_SIZE];
	int sadb_size;
	u8 edid_buf[MAX_EDID_BLOCK_SIZE];

	struct hdmi_edid_sink_data sink_data;
	struct hdmi_edid_init_data init_data;
};

static ssize_t hdmi_edid_sysfs_rda_audio_data_block(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int adb_size, adb_count;
	ssize_t ret;
	char *data = buf;

	struct hdmi_edid_ctrl *edid_ctrl =
		hdmi_get_featuredata_from_sysfs_dev(dev, HDMI_TX_FEAT_EDID);

	if (!edid_ctrl) {
		DEV_ERR("%s: invalid input\n", __func__);
		return -EINVAL;
	}

	adb_count = 1;
	adb_size  = edid_ctrl->adb_size;
	ret       = sizeof(adb_count) + sizeof(adb_size) + adb_size;

	if (ret > PAGE_SIZE) {
		DEV_DBG("%s: Insufficient buffer size\n", __func__);
		return 0;
	}

	
	memcpy(data, &adb_count, sizeof(adb_count));
	data += sizeof(adb_count);
	memcpy(data, &adb_size, sizeof(adb_size));
	data += sizeof(adb_size);
	memcpy(data, edid_ctrl->audio_data_block,
			edid_ctrl->adb_size);

	print_hex_dump(KERN_DEBUG, "AUDIO DATA BLOCK: ", DUMP_PREFIX_NONE,
			32, 8, buf, ret, false);

	return ret;
}
static DEVICE_ATTR(audio_data_block, S_IRUGO,
	hdmi_edid_sysfs_rda_audio_data_block,
	NULL);

static ssize_t hdmi_edid_sysfs_rda_spkr_alloc_data_block(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int sadb_size, sadb_count;
	ssize_t ret;
	char *data = buf;

	struct hdmi_edid_ctrl *edid_ctrl =
		hdmi_get_featuredata_from_sysfs_dev(dev, HDMI_TX_FEAT_EDID);

	if (!edid_ctrl) {
		DEV_ERR("%s: invalid input\n", __func__);
		return -EINVAL;
	}

	sadb_count = 1;
	sadb_size  = edid_ctrl->sadb_size;
	ret        = sizeof(sadb_count) + sizeof(sadb_size) + sadb_size;

	if (ret > PAGE_SIZE) {
		DEV_DBG("%s: Insufficient buffer size\n", __func__);
		return 0;
	}

	
	memcpy(data, &sadb_count, sizeof(sadb_count));
	data += sizeof(sadb_count);
	memcpy(data, &sadb_size, sizeof(sadb_size));
	data += sizeof(sadb_size);
	memcpy(data, edid_ctrl->spkr_alloc_data_block,
			edid_ctrl->sadb_size);

	print_hex_dump(KERN_DEBUG, "SPKR ALLOC DATA BLOCK: ", DUMP_PREFIX_NONE,
			32, 8, buf, ret, false);

	return ret;
}
static DEVICE_ATTR(spkr_alloc_data_block, S_IRUGO,
	hdmi_edid_sysfs_rda_spkr_alloc_data_block, NULL);

static ssize_t hdmi_edid_sysfs_rda_modes(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;
	int i;
	struct hdmi_edid_ctrl *edid_ctrl =
		hdmi_get_featuredata_from_sysfs_dev(dev, HDMI_TX_FEAT_EDID);

	if (!edid_ctrl) {
		DEV_ERR("%s: invalid input\n", __func__);
		return -EINVAL;
	}

	buf[0] = 0;
	if (edid_ctrl->sink_data.num_of_elements) {
		u32 *video_mode = edid_ctrl->sink_data.disp_mode_list;
		for (i = 0; i < edid_ctrl->sink_data.num_of_elements; ++i) {
			if (ret > 0)
				ret += scnprintf(buf + ret, PAGE_SIZE - ret,
					",%d", *video_mode++);
			else
				ret += scnprintf(buf + ret, PAGE_SIZE - ret,
					"%d", *video_mode++);
		}
	} else {
		ret += scnprintf(buf + ret, PAGE_SIZE - ret, "%d",
			edid_ctrl->video_resolution);
	}

	DEV_DBG("%s: '%s'\n", __func__, buf);
	ret += scnprintf(buf + ret, PAGE_SIZE - ret, "\n");

	return ret;
} 
static DEVICE_ATTR(edid_modes, S_IRUGO, hdmi_edid_sysfs_rda_modes, NULL);

static ssize_t hdmi_edid_sysfs_wta_res_info(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int rc, page_id;
	ssize_t ret = strnlen(buf, PAGE_SIZE);
	struct hdmi_edid_ctrl *edid_ctrl =
		hdmi_get_featuredata_from_sysfs_dev(dev, HDMI_TX_FEAT_EDID);

	if (!edid_ctrl) {
		DEV_ERR("%s: invalid input\n", __func__);
		return -EINVAL;
	}

	rc = kstrtoint(buf, 10, &page_id);
	if (rc) {
		DEV_ERR("%s: kstrtoint failed. rc=%d\n", __func__, rc);
		return rc;
	}

	edid_ctrl->page_id = page_id;

	DEV_DBG("%s: %d\n", __func__, edid_ctrl->page_id);
	return ret;
}

static ssize_t hdmi_edid_sysfs_rda_res_info(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret;
	u32 *video_mode;
	u32 no_of_elem;
	u32 i = 0, j, page;
	char *buf_dbg = buf;
	struct msm_hdmi_mode_timing_info info = {0};
	struct hdmi_edid_ctrl *edid_ctrl =
		hdmi_get_featuredata_from_sysfs_dev(dev, HDMI_TX_FEAT_EDID);
	u32 size_to_write = sizeof(info);

	if (!edid_ctrl) {
		DEV_ERR("%s: invalid input\n", __func__);
		return -EINVAL;
	}

	video_mode = edid_ctrl->sink_data.disp_mode_list;
	no_of_elem = edid_ctrl->sink_data.num_of_elements;

	if (edid_ctrl->page_id > MSM_HDMI_INIT_RES_PAGE) {
		page = MSM_HDMI_INIT_RES_PAGE;
		while (page < edid_ctrl->page_id) {
			j = 1;
			while (sizeof(info) * j < PAGE_SIZE) {
				i++;
				j++;
				video_mode++;
			}
			page++;
		}
	}

	for (; i < no_of_elem && size_to_write < PAGE_SIZE; i++) {
		ret = hdmi_get_supported_mode(&info,
			edid_ctrl->init_data.ds_data,
			*video_mode++);

		if (ret || !info.supported)
			continue;

		memcpy(buf, &info, sizeof(info));

		buf += sizeof(info);
		size_to_write += sizeof(info);
	}

	for (i = sizeof(info); i < size_to_write; i += sizeof(info)) {
		struct msm_hdmi_mode_timing_info info_dbg = {0};

		memcpy(&info_dbg, buf_dbg, sizeof(info_dbg));

		DEV_DBG("%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
			info_dbg.video_format, info_dbg.active_h,
			info_dbg.front_porch_h, info_dbg.pulse_width_h,
			info_dbg.back_porch_h, info_dbg.active_low_h,
			info_dbg.active_v, info_dbg.front_porch_v,
			info_dbg.pulse_width_v, info_dbg.back_porch_v,
			info_dbg.active_low_v, info_dbg.pixel_freq,
			info_dbg.refresh_rate, info_dbg.interlaced,
			info_dbg.supported, info_dbg.ar);

		buf_dbg += sizeof(info_dbg);
	}

	return size_to_write - sizeof(info);
}
static DEVICE_ATTR(res_info, S_IRUGO | S_IWUSR, hdmi_edid_sysfs_rda_res_info,
	hdmi_edid_sysfs_wta_res_info);

static ssize_t hdmi_edid_sysfs_rda_audio_latency(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret;
	struct hdmi_edid_ctrl *edid_ctrl =
		hdmi_get_featuredata_from_sysfs_dev(dev, HDMI_TX_FEAT_EDID);

	if (!edid_ctrl) {
		DEV_ERR("%s: invalid input\n", __func__);
		return -EINVAL;
	}
	ret = scnprintf(buf, PAGE_SIZE, "%d\n", edid_ctrl->audio_latency);

	DEV_DBG("%s: '%s'\n", __func__, buf);

	return ret;
} 
static DEVICE_ATTR(edid_audio_latency, S_IRUGO,
	hdmi_edid_sysfs_rda_audio_latency, NULL);

static ssize_t hdmi_edid_sysfs_rda_video_latency(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret;
	struct hdmi_edid_ctrl *edid_ctrl =
		hdmi_get_featuredata_from_sysfs_dev(dev, HDMI_TX_FEAT_EDID);

	if (!edid_ctrl) {
		DEV_ERR("%s: invalid input\n", __func__);
		return -EINVAL;
	}
	ret = scnprintf(buf, PAGE_SIZE, "%d\n", edid_ctrl->video_latency);

	DEV_DBG("%s: '%s'\n", __func__, buf);

	return ret;
} 
static DEVICE_ATTR(edid_video_latency, S_IRUGO,
	hdmi_edid_sysfs_rda_video_latency, NULL);

static ssize_t hdmi_edid_sysfs_rda_physical_address(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret;
	struct hdmi_edid_ctrl *edid_ctrl =
		hdmi_get_featuredata_from_sysfs_dev(dev, HDMI_TX_FEAT_EDID);

	if (!edid_ctrl) {
		DEV_ERR("%s: invalid input\n", __func__);
		return -EINVAL;
	}

	ret = scnprintf(buf, PAGE_SIZE, "%d\n", edid_ctrl->physical_address);
	DEV_DBG("%s: '%d'\n", __func__, edid_ctrl->physical_address);

	return ret;
} 
static DEVICE_ATTR(pa, S_IRUSR, hdmi_edid_sysfs_rda_physical_address, NULL);

static ssize_t hdmi_edid_sysfs_rda_scan_info(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret;
	struct hdmi_edid_ctrl *edid_ctrl =
		hdmi_get_featuredata_from_sysfs_dev(dev, HDMI_TX_FEAT_EDID);

	if (!edid_ctrl) {
		DEV_ERR("%s: invalid input\n", __func__);
		return -EINVAL;
	}

	ret = scnprintf(buf, PAGE_SIZE, "%d, %d, %d\n", edid_ctrl->pt_scan_info,
		edid_ctrl->it_scan_info, edid_ctrl->ce_scan_info);
	DEV_DBG("%s: '%s'\n", __func__, buf);

	return ret;
} 
static DEVICE_ATTR(scan_info, S_IRUGO, hdmi_edid_sysfs_rda_scan_info, NULL);

static ssize_t hdmi_edid_sysfs_rda_3d_modes(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;
	int i;
	char buff_3d[BUFF_SIZE_3D];

	struct hdmi_edid_ctrl *edid_ctrl =
		hdmi_get_featuredata_from_sysfs_dev(dev, HDMI_TX_FEAT_EDID);

	if (!edid_ctrl) {
		DEV_ERR("%s: invalid input\n", __func__);
		return -EINVAL;
	}

	buf[0] = 0;
	if (edid_ctrl->sink_data.num_of_elements) {
		u32 *video_mode = edid_ctrl->sink_data.disp_mode_list;
		u32 *video_3d_mode = edid_ctrl->sink_data.disp_3d_mode_list;
		for (i = 0; i < edid_ctrl->sink_data.num_of_elements; ++i) {
			if (*video_3d_mode == 0) {
				video_3d_mode++;
				video_mode++;
				continue;
			}
			hdmi_get_video_3d_fmt_2string(*video_3d_mode++,
				buff_3d, sizeof(buff_3d));
			if (ret)
				ret += scnprintf(buf + ret, PAGE_SIZE - ret,
					",%d=%s", *video_mode++,
					buff_3d);
			else
				ret += scnprintf(buf + ret, PAGE_SIZE - ret,
					"%d=%s", *video_mode++,
					buff_3d);
		}
	}

	DEV_DBG("%s: '%s'\n", __func__, buf);
	ret += scnprintf(buf + ret, PAGE_SIZE - ret, "\n");

	return ret;
} 
static DEVICE_ATTR(edid_3d_modes, S_IRUGO, hdmi_edid_sysfs_rda_3d_modes, NULL);

static ssize_t hdmi_common_rda_edid_raw_data(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct hdmi_edid_ctrl *edid_ctrl =
		hdmi_get_featuredata_from_sysfs_dev(dev, HDMI_TX_FEAT_EDID);
	u32 size = sizeof(edid_ctrl->edid_buf) < PAGE_SIZE ?
			sizeof(edid_ctrl->edid_buf) : PAGE_SIZE;

	if (!edid_ctrl) {
		DEV_ERR("%s: invalid input\n", __func__);
		return -EINVAL;
	}

	
	memcpy(buf, edid_ctrl->edid_buf, size);

	return size;
} 
static DEVICE_ATTR(edid_raw_data, S_IRUGO, hdmi_common_rda_edid_raw_data, NULL);

static struct attribute *hdmi_edid_fs_attrs[] = {
	&dev_attr_edid_modes.attr,
	&dev_attr_pa.attr,
	&dev_attr_scan_info.attr,
	&dev_attr_edid_3d_modes.attr,
	&dev_attr_edid_raw_data.attr,
	&dev_attr_audio_data_block.attr,
	&dev_attr_spkr_alloc_data_block.attr,
	&dev_attr_edid_audio_latency.attr,
	&dev_attr_edid_video_latency.attr,
	&dev_attr_res_info.attr,
	NULL,
};

static struct attribute_group hdmi_edid_fs_attrs_group = {
	.attrs = hdmi_edid_fs_attrs,
};

static int hdmi_edid_read_block(struct hdmi_edid_ctrl *edid_ctrl, int block,
	u8 *edid_buf)
{
	const u8 *b = NULL;
	u32 ndx, check_sum, print_len;
	int block_size;
	int i, status;
	int retry_cnt = 0, checksum_retry = 0;
	struct hdmi_tx_ddc_data ddc_data;
	b = edid_buf;

	if (!edid_ctrl) {
		DEV_ERR("%s: invalid input\n", __func__);
		return -EINVAL;
	}

read_retry:
	block_size = 0x80;
	status = 0;
	do {
		DEV_DBG("EDID: reading block(%d) with block-size=%d\n",
			block, block_size);
		for (i = 0; i < 0x80; i += block_size) {
			memset(&ddc_data, 0, sizeof(ddc_data));
			ddc_data.dev_addr    = 0xA0;
			ddc_data.offset      = block*0x80 + i;
			ddc_data.data_buf    = edid_buf+i;
			ddc_data.data_len    = block_size;
			ddc_data.request_len = block_size;
			ddc_data.retry       = 1;
			ddc_data.what        = "EDID";
			ddc_data.no_align    = false;

			
			if (block < 2)
				status = hdmi_ddc_read(
					edid_ctrl->init_data.ddc_ctrl,
					&ddc_data);
			else
				status = hdmi_ddc_read_seg(
					edid_ctrl->init_data.ddc_ctrl,
					&ddc_data);
			if (status)
				break;
		}
		if (retry_cnt++ >= MAX_EDID_READ_RETRY)
			block_size /= 2;
	} while (status && (block_size >= 16));

	if (status)
		goto error;

	
	check_sum = 0;
	for (ndx = 0; ndx < 0x80; ++ndx)
		check_sum += edid_buf[ndx];

	if (check_sum & 0xFF) {
		DEV_ERR("%s: failed CHECKSUM (read:%x, expected:%x)\n",
			__func__, (u8)edid_buf[0x7F], (u8)check_sum);
		for (ndx = 0; ndx < 0x100; ndx += 4)
			DEV_DBG("EDID[%02x-%02x] %02x %02x %02x %02x\n",
				ndx, ndx+3,
				b[ndx+0], b[ndx+1], b[ndx+2], b[ndx+3]);
		status = -EPROTO;
		if (checksum_retry++ < 3) {
			DEV_DBG("Retrying reading EDID %d time\n",
							checksum_retry);
			goto read_retry;
		}
		goto error;
	}

	print_len = 0x80;
	for (ndx = 0; ndx < print_len; ndx += 4)
		DEV_DBG("EDID[%02x-%02x] %02x %02x %02x %02x\n",
			ndx, ndx+3,
			b[ndx+0], b[ndx+1], b[ndx+2], b[ndx+3]);

error:
	return status;
} 

#define EDID_BLK_LEN 128
#define EDID_DTD_LEN 18
static const u8 *hdmi_edid_find_block(const u8 *in_buf, u32 start_offset,
	u8 type, u8 *len)
{
	
	u32 offset = start_offset;
	u32 dbc_offset = in_buf[2];

	if (dbc_offset >= EDID_BLK_LEN - EDID_DTD_LEN)
		return NULL;
	*len = 0;

	if ((dbc_offset == 0) || (dbc_offset == 4)) {
		DEV_WARN("EDID: no DTD or non-DTD data present\n");
		return NULL;
	}

	while (offset < dbc_offset) {
		u8 block_len = in_buf[offset] & 0x1F;
		if ((offset + block_len <= dbc_offset) &&
		    (in_buf[offset] >> 5) == type) {
			*len = block_len;
			DEV_DBG("%s: EDID: block=%d found @ 0x%x w/ len=%d\n",
				__func__, type, offset, block_len);

			return in_buf + offset;
		}
		offset += 1 + block_len;
	}
	DEV_WARN("%s: EDID: type=%d block not found in EDID block\n",
		__func__, type);

	return NULL;
} 

static void hdmi_edid_extract_extended_data_blocks(
	struct hdmi_edid_ctrl *edid_ctrl, const u8 *in_buf)
{
	u8 len = 0;
	u32 start_offset = DBC_START_OFFSET;
	u8 const *etag = NULL;

	if (!edid_ctrl) {
		DEV_ERR("%s: invalid input\n", __func__);
		return;
	}

	
	etag = hdmi_edid_find_block(in_buf, start_offset, USE_EXTENDED_TAG,
		&len);

	while (etag != NULL) {
		
		if (len < 2) {
			DEV_DBG("%s: data block of len < 2 bytes. Ignor...\n",
				__func__);
		} else {
			switch (etag[1]) {
			case 0:
				
				DEV_DBG("%s: EDID: VCDB=%02X %02X\n", __func__,
					etag[1], etag[2]);

				edid_ctrl->pt_scan_info =
					(etag[2] & (BIT(4) | BIT(5))) >> 4;
				edid_ctrl->it_scan_info =
					(etag[2] & (BIT(3) | BIT(2))) >> 2;
				edid_ctrl->ce_scan_info =
					etag[2] & (BIT(1) | BIT(0));
				DEV_DBG("%s: Scan Info (pt|it|ce): (%d|%d|%d)",
					__func__,
					edid_ctrl->pt_scan_info,
					edid_ctrl->it_scan_info,
					edid_ctrl->ce_scan_info);
				break;
			default:
				DEV_DBG("%s: Tag Code %d not supported\n",
					__func__, etag[1]);
				break;
			}
		}

		
		start_offset = etag - in_buf + len + 1;
		etag = hdmi_edid_find_block(in_buf, start_offset,
			USE_EXTENDED_TAG, &len);
	}
} 

static void hdmi_edid_extract_3d_present(struct hdmi_edid_ctrl *edid_ctrl,
	const u8 *in_buf)
{
	u8 len, offset;
	const u8 *vsd = NULL;

	if (!edid_ctrl) {
		DEV_ERR("%s: invalid input\n", __func__);
		return;
	}

	vsd = hdmi_edid_find_block(in_buf, DBC_START_OFFSET,
		VENDOR_SPECIFIC_DATA_BLOCK, &len);

	edid_ctrl->present_3d = 0;
	if (vsd == NULL || len == 0 || len > MAX_DATA_BLOCK_SIZE) {
		DEV_DBG("%s: No/Invalid vendor Specific Data Block\n",
			__func__);
		return;
	}

	offset = HDMI_VSDB_3D_EVF_DATA_OFFSET(vsd);
	DEV_DBG("%s: EDID: 3D present @ 0x%x = %02x\n", __func__,
		offset, vsd[offset]);

	if (vsd[offset] >> 7) { 
		DEV_INFO("%s: EDID: 3D present, 3D-len=%d\n", __func__,
			vsd[offset+1] & 0x1F);
		edid_ctrl->present_3d = 1;
	}
} 

static void hdmi_edid_extract_audio_data_blocks(
	struct hdmi_edid_ctrl *edid_ctrl, const u8 *in_buf)
{
	u8 len = 0;
	u8 adb_max = 0;
	const u8 *adb = NULL;
	u32 offset = DBC_START_OFFSET;

	if (!edid_ctrl) {
		DEV_ERR("%s: invalid input\n", __func__);
		return;
	}

	edid_ctrl->adb_size = 0;

	memset(edid_ctrl->audio_data_block, 0,
		sizeof(edid_ctrl->audio_data_block));

	do {
		len = 0;
		adb = hdmi_edid_find_block(in_buf, offset, AUDIO_DATA_BLOCK,
			&len);

		if ((adb == NULL) || (len > MAX_AUDIO_DATA_BLOCK_SIZE ||
			adb_max >= MAX_NUMBER_ADB)) {
			if (!edid_ctrl->adb_size) {
				DEV_DBG("%s: No/Invalid Audio Data Block\n",
					__func__);
				return;
			} else {
				DEV_DBG("%s: No more valid ADB found\n",
					__func__);
			}

			continue;
		}

		memcpy(edid_ctrl->audio_data_block + edid_ctrl->adb_size,
			adb + 1, len);
		offset = (adb - in_buf) + 1 + len;

		edid_ctrl->adb_size += len;
		adb_max++;
	} while (adb);

} 

static void hdmi_edid_extract_speaker_allocation_data(
	struct hdmi_edid_ctrl *edid_ctrl, const u8 *in_buf)
{
	u8 len;
	const u8 *sadb = NULL;

	if (!edid_ctrl) {
		DEV_ERR("%s: invalid input\n", __func__);
		return;
	}

	sadb = hdmi_edid_find_block(in_buf, DBC_START_OFFSET,
		SPEAKER_ALLOCATION_DATA_BLOCK, &len);
	if ((sadb == NULL) || (len != MAX_SPKR_ALLOC_DATA_BLOCK_SIZE)) {
		DEV_DBG("%s: No/Invalid Speaker Allocation Data Block\n",
			__func__);
		return;
	}

	memcpy(edid_ctrl->spkr_alloc_data_block, sadb + 1, len);
	edid_ctrl->sadb_size = len;

	DEV_DBG("%s: EDID: speaker alloc data SP byte = %08x %s%s%s%s%s%s%s\n",
		__func__, sadb[1],
		(sadb[1] & BIT(0)) ? "FL/FR," : "",
		(sadb[1] & BIT(1)) ? "LFE," : "",
		(sadb[1] & BIT(2)) ? "FC," : "",
		(sadb[1] & BIT(3)) ? "RL/RR," : "",
		(sadb[1] & BIT(4)) ? "RC," : "",
		(sadb[1] & BIT(5)) ? "FLC/FRC," : "",
		(sadb[1] & BIT(6)) ? "RLC/RRC," : "");
} 

static void hdmi_edid_extract_latency_fields(struct hdmi_edid_ctrl *edid_ctrl,
	const u8 *in_buf)
{
	u8 len;
	const u8 *vsd = NULL;

	if (!edid_ctrl) {
		DEV_ERR("%s: invalid input\n", __func__);
		return;
	}

	vsd = hdmi_edid_find_block(in_buf, DBC_START_OFFSET,
		VENDOR_SPECIFIC_DATA_BLOCK, &len);

	if (vsd == NULL || len == 0 || len > MAX_DATA_BLOCK_SIZE ||
		!(vsd[8] & BIT(7))) {
		edid_ctrl->video_latency = (u16)-1;
		edid_ctrl->audio_latency = (u16)-1;
		DEV_DBG("%s: EDID: No audio/video latency present\n", __func__);
	} else {
		edid_ctrl->video_latency = vsd[9];
		edid_ctrl->audio_latency = vsd[10];
		DEV_DBG("%s: EDID: video-latency=%04x, audio-latency=%04x\n",
			__func__, edid_ctrl->video_latency,
			edid_ctrl->audio_latency);
	}
} 

static u32 hdmi_edid_extract_ieee_reg_id(struct hdmi_edid_ctrl *edid_ctrl,
	const u8 *in_buf)
{
	u8 len;
	const u8 *vsd = NULL;

	if (!edid_ctrl) {
		DEV_ERR("%s: invalid input\n", __func__);
		return 0;
	}

	vsd = hdmi_edid_find_block(in_buf, DBC_START_OFFSET,
		VENDOR_SPECIFIC_DATA_BLOCK, &len);

	if (vsd == NULL || len == 0 || len > MAX_DATA_BLOCK_SIZE) {
		DEV_DBG("%s: No/Invalid Vendor Specific Data Block\n",
			__func__);
		return 0;
	}

	DEV_DBG("%s: EDID: VSD PhyAddr=%04x, MaxTMDS=%dMHz\n", __func__,
		((u32)vsd[4] << 8) + (u32)vsd[5], (u32)vsd[7] * 5);

	edid_ctrl->physical_address = ((u16)vsd[4] << 8) + (u16)vsd[5];

	return ((u32)vsd[3] << 16) + ((u32)vsd[2] << 8) + (u32)vsd[1];
} 

static void hdmi_edid_extract_vendor_id(const u8 *in_buf,
	char *vendor_id)
{
	u32 id_codes = ((u32)in_buf[8] << 8) + in_buf[9];

	vendor_id[0] = 'A' - 1 + ((id_codes >> 10) & 0x1F);
	vendor_id[1] = 'A' - 1 + ((id_codes >> 5) & 0x1F);
	vendor_id[2] = 'A' - 1 + (id_codes & 0x1F);
	vendor_id[3] = 0;
} 

static u32 hdmi_edid_check_header(const u8 *edid_buf)
{
	return (edid_buf[0] == 0x00) && (edid_buf[1] == 0xff)
		&& (edid_buf[2] == 0xff) && (edid_buf[3] == 0xff)
		&& (edid_buf[4] == 0xff) && (edid_buf[5] == 0xff)
		&& (edid_buf[6] == 0xff) && (edid_buf[7] == 0x00);
} 

static void hdmi_edid_detail_desc(struct hdmi_edid_ctrl *edid_ctrl,
	const u8 *data_buf, u32 *disp_mode)
{
	u32	aspect_ratio_4_3    = false;
	u32	aspect_ratio_5_4    = false;
	u32	interlaced          = false;
	u32	active_h            = 0;
	u32	active_v            = 0;
	u32	blank_h             = 0;
	u32	blank_v             = 0;
	u32	ndx                 = 0;
	u32	img_size_h          = 0;
	u32	img_size_v          = 0;
	u32	pixel_clk           = 0;
	u32	front_porch_h       = 0;
	u32	front_porch_v       = 0;
	u32	pulse_width_h       = 0;
	u32	pulse_width_v       = 0;
	u32	active_low_h        = 0;
	u32	active_low_v        = 0;

	pixel_clk = (u32) (data_buf[0x0] | (data_buf[0x1] << 8));

	
	pixel_clk *= 10;

	front_porch_h = (u32) (data_buf[0x8] |
		(data_buf[0xb] & (0x3 << 6)) << 2);

	pulse_width_h = (u32) (data_buf[0x9] |
		(data_buf[0xb] & (0x3 << 4)) << 4);

	front_porch_v = (u32) (((data_buf[0xa] & (0xF << 4)) >> 4) |
		(data_buf[0xb] & (0x3 << 2)) << 2);

	pulse_width_v = (u32) ((data_buf[0xa] & 0xF) |
		((data_buf[0xb] & 0x3) << 4));

	active_h = ((((u32)data_buf[0x4] >> 0x4) & 0xF) << 8)
		| data_buf[0x2];

	blank_h = (((u32)data_buf[0x4] & 0xF) << 8)
		| data_buf[0x3];

	active_v = ((((u32)data_buf[0x7] >> 0x4) & 0xF) << 8)
		| data_buf[0x5];

	blank_v = (((u32)data_buf[0x7] & 0xF) << 8)
		| data_buf[0x6];

	img_size_h = ((((u32)data_buf[0xE] >> 0x4) & 0xF) << 8)
		| data_buf[0xC];
	img_size_v = (((u32)data_buf[0xE] & 0xF) << 8)
		| data_buf[0xD];

	aspect_ratio_4_3 = (abs(img_size_h * 3 - img_size_v * 4) < 5) ? 1 : 0;

	aspect_ratio_5_4 = (abs(img_size_h * 4 - img_size_v * 5) < 5) ? 1 : 0;

	DEV_DBG("%s: Interlaced mode byte data_buf[0x11]=[%x]\n", __func__,
		data_buf[0x11]);

	interlaced = (data_buf[0x11] & 0x80) >> 7;

	active_low_v = ((data_buf[0x11] & (0x7 << 2)) >> 2) == 0x7 ? 0 : 1;

	active_low_h = ((data_buf[0x11] & BIT(1)) &&
				(data_buf[0x11] & BIT(4))) ? 0 : 1;

	DEV_DBG("%s: A[%ux%u] B[%ux%u] V[%ux%u] %s\n", __func__,
		active_h, active_v, blank_h, blank_v, img_size_h, img_size_v,
		interlaced ? "i" : "p");

	*disp_mode = HDMI_VFRMT_FORCE_32BIT;
	for (ndx = HDMI_VFRMT_UNKNOWN + 1; ndx < HDMI_VFRMT_MAX; ndx++) {
		struct msm_hdmi_mode_timing_info timing = {0};
		u32 ret = hdmi_get_supported_mode(&timing,
				edid_ctrl->init_data.ds_data,
				ndx);

		if (ret || !timing.supported)
			continue;

		if ((interlaced   == timing.interlaced) &&
			(active_h == timing.active_h) &&
			(blank_h  == (timing.front_porch_h +
				timing.pulse_width_h +
				timing.back_porch_h)) &&
			(blank_v  == (timing.front_porch_v +
				timing.pulse_width_v +
				timing.back_porch_v)) &&
			((active_v == timing.active_v) ||
			(active_v  == (timing.active_v + 1)))) {
				*disp_mode = timing.video_format;


			if (aspect_ratio_4_3 &&
				(timing.ar != HDMI_RES_AR_4_3))
				continue;
			else if (!aspect_ratio_4_3 &&
				(timing.ar == HDMI_RES_AR_4_3))
				continue;
			else
				break;
		}
	}

	if (*disp_mode == HDMI_VFRMT_FORCE_32BIT) {
		struct msm_hdmi_mode_timing_info timing = {0};
		u64 rr_tmp = pixel_clk * 1000 * 1000;
		u64 frame_data = (blank_h + active_h) *
			(blank_v + active_v);
		int rc = 0;

		DEV_DBG("%s: found new mode\n", __func__);

		do_div(rr_tmp, frame_data);

		timing.active_h      = active_h;
		timing.front_porch_h = front_porch_h;
		timing.pulse_width_h = pulse_width_h;
		timing.back_porch_h  = blank_h -
					(front_porch_h + pulse_width_h);
		timing.active_low_h  = active_low_h;
		timing.active_v      = active_v;
		timing.front_porch_v = front_porch_v;
		timing.pulse_width_v = pulse_width_v;
		timing.back_porch_v  = blank_v -
					(front_porch_v + pulse_width_v);
		timing.active_low_v  = active_low_v;
		timing.pixel_freq    = pixel_clk;
		timing.refresh_rate  = (u32) rr_tmp;
		timing.interlaced    = interlaced;
		timing.supported     = true;
		timing.ar            = aspect_ratio_4_3 ? HDMI_RES_AR_4_3 :
					(aspect_ratio_5_4 ? HDMI_RES_AR_5_4 :
						HDMI_RES_AR_16_9);

		rc = hdmi_set_resv_timing_info(&timing);
		if (!IS_ERR_VALUE(rc))
			*disp_mode = rc;
	} else {
		DEV_DBG("%s: mode found:%d\n", __func__, *disp_mode);
	}
} 

static void hdmi_edid_add_sink_3d_format(struct hdmi_edid_sink_data *sink_data,
	u32 video_format, u32 video_3d_format)
{
	char string[BUFF_SIZE_3D];
	u32 added = false;
	int i;

	for (i = 0; i < sink_data->num_of_elements; ++i) {
		if (sink_data->disp_mode_list[i] == video_format) {
			sink_data->disp_3d_mode_list[i] |= video_3d_format;
			added = true;
			break;
		}
	}

	hdmi_get_video_3d_fmt_2string(video_3d_format, string, sizeof(string));

	DEV_DBG("%s: EDID[3D]: format: %d [%s], %s %s\n", __func__,
		video_format, msm_hdmi_mode_2string(video_format),
		string, added ? "added" : "NOT added");
} 

static void hdmi_edid_add_sink_video_format(struct hdmi_edid_ctrl *edid_ctrl,
	u32 video_format)
{
	struct msm_hdmi_mode_timing_info timing = {0};
	u32 ret = hdmi_get_supported_mode(&timing,
				edid_ctrl->init_data.ds_data,
				video_format);
	u32 supported = timing.supported;
	struct hdmi_edid_sink_data *sink_data = &edid_ctrl->sink_data;

	if (video_format >= HDMI_VFRMT_MAX) {
		DEV_ERR("%s: video format: %s is not supported\n", __func__,
			msm_hdmi_mode_2string(video_format));
		return;
	}

	DEV_DBG("%s: EDID: format: %d [%s], %s\n", __func__,
		video_format, msm_hdmi_mode_2string(video_format),
		supported ? "Supported" : "Not-Supported");

	if (!ret && supported) {
		
		sink_data->disp_mode_list[sink_data->num_of_elements++] =
			video_format;
	}
} 

static int hdmi_edid_get_display_vsd_3d_mode(const u8 *data_buf,
	struct hdmi_edid_sink_data *sink_data, u32 num_of_cea_blocks)
{
	u8 len, offset, present_multi_3d, hdmi_vic_len;
	int hdmi_3d_len;
	u16 structure_all, structure_mask;
	const u8 *vsd = num_of_cea_blocks ?
		hdmi_edid_find_block(data_buf+0x80, DBC_START_OFFSET,
			VENDOR_SPECIFIC_DATA_BLOCK, &len) : NULL;
	int i;

	if (vsd == NULL || len == 0 || len > MAX_DATA_BLOCK_SIZE) {
		DEV_DBG("%s: No/Invalid Vendor Specific Data Block\n",
			__func__);
		return -ENXIO;
	}

	offset = HDMI_VSDB_3D_EVF_DATA_OFFSET(vsd);
	if (offset >= len - 1)
		return -ETOOSMALL;

	present_multi_3d = (vsd[offset] & 0x60) >> 5;

	offset += 1;

	hdmi_vic_len = (vsd[offset] >> 5) & 0x7;
	hdmi_3d_len = vsd[offset] & 0x1F;
	DEV_DBG("%s: EDID[3D]: HDMI_VIC_LEN = %d, HDMI_3D_LEN = %d\n", __func__,
		hdmi_vic_len, hdmi_3d_len);

	offset += (hdmi_vic_len + 1);
	if (offset >= len - 1)
		return -ETOOSMALL;

	if (present_multi_3d == 1 || present_multi_3d == 2) {
		DEV_DBG("%s: EDID[3D]: multi 3D present (%d)\n", __func__,
			present_multi_3d);
		
		structure_all = (vsd[offset] << 8) | vsd[offset + 1];
		offset += 2;
		if (offset >= len - 1)
			return -ETOOSMALL;
		hdmi_3d_len -= 2;
		if (present_multi_3d == 2) {
			
			structure_mask = (vsd[offset] << 8) | vsd[offset + 1];
			offset += 2;
			hdmi_3d_len -= 2;
		} else
			structure_mask = 0xffff;

		i = 0;
		while (i < 16) {
			if (i >= sink_data->disp_multi_3d_mode_list_cnt)
				break;

			if (!(structure_mask & BIT(i))) {
				++i;
				continue;
			}

			
			if (structure_all & BIT(0))
				hdmi_edid_add_sink_3d_format(sink_data,
					sink_data->
					disp_multi_3d_mode_list[i],
					FRAME_PACKING);

			
			if (structure_all & BIT(6))
				hdmi_edid_add_sink_3d_format(sink_data,
					sink_data->
					disp_multi_3d_mode_list[i],
					TOP_AND_BOTTOM);

			
			if (structure_all & BIT(8))
				hdmi_edid_add_sink_3d_format(sink_data,
					sink_data->
					disp_multi_3d_mode_list[i],
					SIDE_BY_SIDE_HALF);

			++i;
		}
	}

	i = 0;
	while (hdmi_3d_len > 0) {
		if (offset >= len - 1)
			return -ETOOSMALL;
		DEV_DBG("%s: EDID: 3D_Structure_%d @ 0x%x: %02x\n",
			__func__, i + 1, offset, vsd[offset]);
		if ((vsd[offset] >> 4) >=
			sink_data->disp_multi_3d_mode_list_cnt) {
			if ((vsd[offset] & 0x0F) >= 8) {
				offset += 1;
				hdmi_3d_len -= 1;
				DEV_DBG("%s:EDID:3D_Detail_%d @ 0x%x: %02x\n",
					__func__, i + 1, offset,
					vsd[min_t(u32, offset, (len - 1))]);
			}
			i += 1;
			offset += 1;
			hdmi_3d_len -= 1;
			continue;
		}

		switch (vsd[offset] & 0x0F) {
		case 0:
			
			hdmi_edid_add_sink_3d_format(sink_data,
				sink_data->
				disp_multi_3d_mode_list[vsd[offset] >> 4],
				FRAME_PACKING);
			break;
		case 6:
			
			hdmi_edid_add_sink_3d_format(sink_data,
				sink_data->
				disp_multi_3d_mode_list[vsd[offset] >> 4],
				TOP_AND_BOTTOM);
			break;
		case 8:
			
			hdmi_edid_add_sink_3d_format(sink_data,
				sink_data->
				disp_multi_3d_mode_list[vsd[offset] >> 4],
				SIDE_BY_SIDE_HALF);
			break;
		}
		if ((vsd[offset] & 0x0F) >= 8) {
			offset += 1;
			hdmi_3d_len -= 1;
			DEV_DBG("%s: EDID[3D]: 3D_Detail_%d @ 0x%x: %02x\n",
				__func__, i + 1, offset,
				vsd[min_t(u32, offset, (len - 1))]);
		}
		i += 1;
		offset += 1;
		hdmi_3d_len -= 1;
	}
	return 0;
} 

static void hdmi_edid_get_extended_video_formats(
	struct hdmi_edid_ctrl *edid_ctrl, const u8 *in_buf)
{
	u8 db_len, offset, i;
	u8 hdmi_vic_len;
	u32 video_format;
	const u8 *vsd = NULL;

	if (!edid_ctrl) {
		DEV_ERR("%s: invalid input\n", __func__);
		return;
	}

	vsd = hdmi_edid_find_block(in_buf, DBC_START_OFFSET,
		VENDOR_SPECIFIC_DATA_BLOCK, &db_len);

	if (vsd == NULL || db_len == 0 || db_len > MAX_DATA_BLOCK_SIZE) {
		DEV_DBG("%s: No/Invalid Vendor Specific Data Block\n",
			__func__);
		return;
	}

	
	if (!(vsd[8] & BIT(5))) {
		DEV_DBG("%s: extended vfmts are not supported by the sink.\n",
			__func__);
		return;
	}

	offset = HDMI_VSDB_3D_EVF_DATA_OFFSET(vsd);

	hdmi_vic_len = vsd[offset + 1] >> 5;
	if (hdmi_vic_len) {
		DEV_DBG("%s: EDID: EVFRMT @ 0x%x of block 3, len = %02x\n",
			__func__, offset, hdmi_vic_len);

		for (i = 0; i < hdmi_vic_len; i++) {
			video_format = HDMI_VFRMT_END + vsd[offset + 2 + i];
			hdmi_edid_add_sink_video_format(edid_ctrl,
				video_format);
		}
	}
} 

static void hdmi_edid_parse_et3(struct hdmi_edid_ctrl *edid_ctrl,
	const u8 *edid_blk0)
{
	u8  start = DTD_OFFSET, i = 0;
	struct hdmi_edid_sink_data *sink_data = NULL;

	if (!edid_ctrl || !edid_blk0) {
		DEV_ERR("%s: invalid input\n", __func__);
		return;
	}

	sink_data = &edid_ctrl->sink_data;

	
	if (edid_blk0[REVISION_OFFSET] != EDID_REVISION_FOUR)
		return;

	
	while (i < DTD_MAX) {
		u8  iter = start;
		u32 header_1 = 0;
		u8  header_2 = 0;

		header_1 = edid_blk0[iter++];
		header_1 = header_1 << 8 | edid_blk0[iter++];
		header_1 = header_1 << 8 | edid_blk0[iter++];
		header_1 = header_1 << 8 | edid_blk0[iter++];
		header_2 = edid_blk0[iter];

		if (header_1 != 0x000000F7 || header_2 != 0x00)
			goto loop_end;

		
		iter++;

		
		iter++;
		if (edid_blk0[iter] & BIT(3)) {
			pr_debug("%s: DMT 848x480@60\n", __func__);
			hdmi_edid_add_sink_video_format(edid_ctrl,
				HDMI_VFRMT_848x480p60_16_9);
		}

		
		iter++;
		if (edid_blk0[iter] & BIT(1)) {
			pr_debug("%s: DMT 1280x1024@60\n", __func__);
			hdmi_edid_add_sink_video_format(edid_ctrl,
				HDMI_VFRMT_1280x1024p60_5_4);
		}

		if (edid_blk0[iter] & BIT(3)) {
			pr_debug("%s: DMT 1280x960@60\n", __func__);
			hdmi_edid_add_sink_video_format(edid_ctrl,
				HDMI_VFRMT_1280x960p60_4_3);
		}

		
		iter++;
		if (edid_blk0[iter] & BIT(1)) {
			pr_debug("%s: DMT 1400x1050@60\n", __func__);
			hdmi_edid_add_sink_video_format(edid_ctrl,
				HDMI_VFRMT_1400x1050p60_4_3);
		}

		if (edid_blk0[iter] & BIT(5)) {
			pr_debug("%s: DMT 1440x900@60\n", __func__);
			hdmi_edid_add_sink_video_format(edid_ctrl,
				HDMI_VFRMT_1440x900p60_16_10);
		}

		if (edid_blk0[iter] & BIT(7)) {
			pr_debug("%s: DMT 1360x768@60\n", __func__);
			hdmi_edid_add_sink_video_format(edid_ctrl,
				HDMI_VFRMT_1360x768p60_16_9);
		}

		
		iter++;
		if (edid_blk0[iter] & BIT(2)) {
			pr_debug("%s: DMT 1600x1200@60\n", __func__);
			hdmi_edid_add_sink_video_format(edid_ctrl,
				HDMI_VFRMT_1600x1200p60_4_3);
		}

		if (edid_blk0[iter] & BIT(5)) {
			pr_debug("%s: DMT 1680x1050@60\n", __func__);
			hdmi_edid_add_sink_video_format(edid_ctrl,
				HDMI_VFRMT_1680x1050p60_16_10);
		}

		
		iter++;
		if (edid_blk0[iter] & BIT(0)) {
			pr_debug("%s: DMT 1920x1200@60\n", __func__);
			hdmi_edid_add_sink_video_format(edid_ctrl,
				HDMI_VFRMT_1920x1200p60_16_10);
		}

loop_end:
		i++;
		start += DTD_SIZE;
	}
}

static void hdmi_edid_get_display_mode(struct hdmi_edid_ctrl *edid_ctrl,
	const u8 *data_buf, u32 num_of_cea_blocks, u8 write_burst_vic)
{
	u8 i = 0, offset = 0, std_blk = 0;
	u32 video_format = HDMI_VFRMT_640x480p60_4_3;
	u32 has480p = false;
	u8 len = 0;
	int rc;
	const u8 *edid_blk0 = NULL;
	const u8 *edid_blk1 = NULL;
	const u8 *svd = NULL;
	u32 has60hz_mode = false;
	u32 has50hz_mode = false;
	bool read_block0_res = false;
	struct hdmi_edid_sink_data *sink_data = NULL;

	if (!edid_ctrl || !data_buf) {
		DEV_ERR("%s: invalid input\n", __func__);
		return;
	}

	edid_blk0 = &data_buf[0x0];
	edid_blk1 = &data_buf[0x80];
	svd = num_of_cea_blocks ?
		hdmi_edid_find_block(data_buf+0x80, DBC_START_OFFSET,
			VIDEO_DATA_BLOCK, &len) : NULL;

	if (num_of_cea_blocks && (len == 0 || len > MAX_DATA_BLOCK_SIZE)) {
		DEV_DBG("%s: fall back to block 0 res\n", __func__);
		svd = NULL;
		read_block0_res = true;
	}

	sink_data = &edid_ctrl->sink_data;

	sink_data->num_of_elements = 0;
	sink_data->disp_multi_3d_mode_list_cnt = 0;
	if (svd != NULL) {
		++svd;
		for (i = 0; i < len; ++i, ++svd) {
			if (((*svd >= 65) && (*svd <= 127)) || ((*svd >= 193) && (*svd <= 255)))
			{
				switch(*svd) {
					case 93: video_format = HDMI_VFRMT_3840x2160p24_16_9; break;
					case 94: video_format = HDMI_VFRMT_3840x2160p25_16_9; break;
					case 95: video_format = HDMI_VFRMT_3840x2160p30_16_9; break;
					default: break;
				}
			}
			else
				video_format = (*svd & 0x7F);

			hdmi_edid_add_sink_video_format(edid_ctrl,
				video_format);
			
			if (i == 0)
				sink_data->preferred_video_format =
					video_format;

			if (i < 16) {
				sink_data->disp_multi_3d_mode_list[i]
					= video_format;
				sink_data->disp_multi_3d_mode_list_cnt++;
			}

			if (video_format <= HDMI_VFRMT_1920x1080p60_16_9 ||
				video_format == HDMI_VFRMT_2880x480p60_4_3 ||
				video_format == HDMI_VFRMT_2880x480p60_16_9)
				has60hz_mode = true;

			if ((video_format >= HDMI_VFRMT_720x576p50_4_3 &&
				video_format <= HDMI_VFRMT_1920x1080p50_16_9) ||
				video_format == HDMI_VFRMT_2880x576p50_4_3 ||
				video_format == HDMI_VFRMT_2880x576p50_16_9 ||
				video_format == HDMI_VFRMT_1920x1250i50_16_9)
				has50hz_mode = true;

			if (video_format == HDMI_VFRMT_640x480p60_4_3)
				has480p = true;
		}
	} else if (!num_of_cea_blocks || read_block0_res) {
		
		u32 desc_offset = 0;
		while (4 > i && 0 != edid_blk0[0x36+desc_offset]) {
			hdmi_edid_detail_desc(edid_ctrl,
				edid_blk0+0x36+desc_offset,
				&video_format);

			DEV_DBG("[%s:%d] Block-0 Adding vid fmt = [%s]\n",
				__func__, __LINE__,
				msm_hdmi_mode_2string(video_format));

			hdmi_edid_add_sink_video_format(edid_ctrl,
				video_format);

			if (video_format == HDMI_VFRMT_640x480p60_4_3)
				has480p = true;

			
			if (i == 0) {
				sink_data->preferred_video_format =
					video_format;
			}
			desc_offset += 0x12;
			++i;
		}
	} else if (1 == num_of_cea_blocks) {
		u32 desc_offset = 0;

		while (4 > i && 0 != edid_blk0[0x36+desc_offset]) {
			hdmi_edid_detail_desc(edid_ctrl,
				edid_blk0+0x36+desc_offset,
				&video_format);

			DEV_DBG("[%s:%d] Block-0 Adding vid fmt = [%s]\n",
				__func__, __LINE__,
				msm_hdmi_mode_2string(video_format));

			hdmi_edid_add_sink_video_format(edid_ctrl,
				video_format);

			if (video_format == HDMI_VFRMT_640x480p60_4_3)
				has480p = true;

			
			if (i == 0) {
				sink_data->preferred_video_format =
					video_format;
			}
			desc_offset += 0x12;
			++i;
		}

		desc_offset = edid_blk1[0x02];
		while (0 != edid_blk1[desc_offset]) {
			hdmi_edid_detail_desc(edid_ctrl,
				edid_blk1+desc_offset,
				&video_format);

			DEV_DBG("[%s:%d] Block-1 Adding vid fmt = [%s]\n",
				__func__, __LINE__,
				msm_hdmi_mode_2string(video_format));

			hdmi_edid_add_sink_video_format(edid_ctrl,
				video_format);
			if (video_format == HDMI_VFRMT_640x480p60_4_3)
				has480p = true;

			
			if (i == 0) {
				sink_data->preferred_video_format =
					video_format;
			}
			desc_offset += 0x12;
			++i;
		}
	}

	std_blk = 0;
	offset  = 0;
	while (std_blk < 8) {
		if ((edid_blk0[0x26 + offset] == 0x81) &&
		    (edid_blk0[0x26 + offset + 1] == 0x80)) {
			pr_debug("%s: 108MHz: off=[%x] stdblk=[%x]\n",
				 __func__, offset, std_blk);
			hdmi_edid_add_sink_video_format(edid_ctrl,
				HDMI_VFRMT_1280x1024p60_5_4);
		}
		if ((edid_blk0[0x26 + offset] == 0x61) &&
		    (edid_blk0[0x26 + offset + 1] == 0x40)) {
			pr_debug("%s: 65MHz: off=[%x] stdblk=[%x]\n",
				 __func__, offset, std_blk);
			hdmi_edid_add_sink_video_format(edid_ctrl,
				HDMI_VFRMT_1024x768p60_4_3);
			break;
		} else {
			offset += 2;
		}
		std_blk++;
	}

	
	if (edid_blk0[0x23] & BIT(0)) {
		pr_debug("%s: DMT: ETI: HDMI_VFRMT_800x600_4_3\n", __func__);
		hdmi_edid_add_sink_video_format(edid_ctrl,
				HDMI_VFRMT_800x600p60_4_3);
	}

	
	if (edid_blk0[0x24] & BIT(3)) {
		pr_debug("%s: DMT: ETII: HDMI_VFRMT_1024x768p60_4_3\n",
			__func__);
		hdmi_edid_add_sink_video_format(edid_ctrl,
				HDMI_VFRMT_1024x768p60_4_3);
	}

	
	hdmi_edid_parse_et3(edid_ctrl, data_buf);

	hdmi_edid_get_extended_video_formats(edid_ctrl, data_buf+0x80);

	
	if (edid_ctrl->present_3d) {
		if (has60hz_mode) {
			hdmi_edid_add_sink_3d_format(sink_data,
				HDMI_VFRMT_1920x1080p24_16_9,
				FRAME_PACKING | TOP_AND_BOTTOM);
			hdmi_edid_add_sink_3d_format(sink_data,
				HDMI_VFRMT_1280x720p60_16_9,
				FRAME_PACKING | TOP_AND_BOTTOM);
			hdmi_edid_add_sink_3d_format(sink_data,
				HDMI_VFRMT_1920x1080i60_16_9,
				SIDE_BY_SIDE_HALF);
		}

		if (has50hz_mode) {
			hdmi_edid_add_sink_3d_format(sink_data,
				HDMI_VFRMT_1920x1080p24_16_9,
				FRAME_PACKING | TOP_AND_BOTTOM);
			hdmi_edid_add_sink_3d_format(sink_data,
				HDMI_VFRMT_1280x720p50_16_9,
				FRAME_PACKING | TOP_AND_BOTTOM);
			hdmi_edid_add_sink_3d_format(sink_data,
				HDMI_VFRMT_1920x1080i50_16_9,
				SIDE_BY_SIDE_HALF);
		}

		
		rc = hdmi_edid_get_display_vsd_3d_mode(data_buf, sink_data,
			num_of_cea_blocks);
		if (!rc)
			pr_debug("%s: 3D formats in VSD\n", __func__);
	}

	if (!has480p)
		hdmi_edid_add_sink_video_format(edid_ctrl,
			HDMI_VFRMT_640x480p60_4_3);

	pr_err("write_burst_vic: %d\n", write_burst_vic);
	switch (write_burst_vic)
	{
		case 0x5d:
			hdmi_edid_add_sink_video_format(edid_ctrl, HDMI_VFRMT_3840x2160p24_16_9);
			break;
		case 0x5e:
			hdmi_edid_add_sink_video_format(edid_ctrl, HDMI_VFRMT_3840x2160p25_16_9);
			break;
		case 0x5f:
			hdmi_edid_add_sink_video_format(edid_ctrl, HDMI_VFRMT_3840x2160p30_16_9);
			break;
	}
} 

int hdmi_edid_read(void *input, u8 write_burst_vic)
{
	
	u8 *edid_buf = NULL;
	u32 cea_extension_ver = 0;
	u32 num_of_cea_blocks = 0;
	u32 ieee_reg_id = 0;
	u32 i = 1;
	int status = 0;
	char vendor_id[5];
	struct hdmi_edid_ctrl *edid_ctrl = (struct hdmi_edid_ctrl *)input;

	if (!edid_ctrl) {
		DEV_ERR("%s: invalid input\n", __func__);
		return -EINVAL;
	}

	edid_ctrl->page_id = MSM_HDMI_INIT_RES_PAGE;

	edid_buf = edid_ctrl->edid_buf;

	edid_ctrl->pt_scan_info = 0;
	edid_ctrl->it_scan_info = 0;
	edid_ctrl->ce_scan_info = 0;
	edid_ctrl->present_3d = 0;
	memset(&edid_ctrl->sink_data, 0, sizeof(edid_ctrl->sink_data));
	memset(edid_buf, 0, sizeof(edid_ctrl->edid_buf));
	memset(edid_ctrl->audio_data_block, 0,
		sizeof(edid_ctrl->audio_data_block));
	memset(edid_ctrl->spkr_alloc_data_block, 0,
		sizeof(edid_ctrl->spkr_alloc_data_block));
	edid_ctrl->adb_size = 0;
	edid_ctrl->sadb_size = 0;

	hdmi_reset_resv_timing_info();

	status = hdmi_edid_read_block(edid_ctrl, 0, edid_buf);
	if (status || !hdmi_edid_check_header(edid_buf)) {
		if (!status)
			status = -EPROTO;
		DEV_ERR("%s: blk0 fail:%d[%02x%02x%02x%02x%02x%02x%02x%02x]\n",
			__func__, status,
			edid_buf[0], edid_buf[1], edid_buf[2], edid_buf[3],
			edid_buf[4], edid_buf[5], edid_buf[6], edid_buf[7]);
		goto error;
	}
	hdmi_edid_extract_vendor_id(edid_buf, vendor_id);

	
	num_of_cea_blocks = edid_buf[0x7E];
	DEV_DBG("%s: No. of CEA blocks is  [%u]\n", __func__,
		num_of_cea_blocks);
	
	switch (num_of_cea_blocks) {
	case 0: 
		edid_ctrl->sink_mode = false;
		DEV_DBG("HDMI DVI mode: %s\n",
			edid_ctrl->sink_mode ? "no" : "yes");
		break;
	case 1: 
		status = hdmi_edid_read_block(edid_ctrl, 1, &edid_buf[0x80]);
		if (status) {
			DEV_ERR("%s: ddc read block(1) failed: %d\n", __func__,
				status);
			goto error;
		}
		if (edid_buf[0x80] != 2)
			num_of_cea_blocks = 0;
		if (num_of_cea_blocks) {
			ieee_reg_id =
				hdmi_edid_extract_ieee_reg_id(edid_ctrl,
					edid_buf+0x80);
			if (ieee_reg_id == 0x0c03)
				edid_ctrl->sink_mode = true;
			else
				edid_ctrl->sink_mode = false;

			hdmi_edid_extract_latency_fields(edid_ctrl,
				edid_buf+0x80);
			hdmi_edid_extract_speaker_allocation_data(
				edid_ctrl, edid_buf+0x80);
			hdmi_edid_extract_audio_data_blocks(edid_ctrl,
				edid_buf+0x80);
			hdmi_edid_extract_3d_present(edid_ctrl,
				edid_buf+0x80);
			hdmi_edid_extract_extended_data_blocks(edid_ctrl,
				edid_buf+0x80);
		}
		break;
	case 2:
	case 3:
	case 4:
		for (i = 1; i <= num_of_cea_blocks; i++) {
			status = hdmi_edid_read_block(
				edid_ctrl, i, edid_buf + (0x80 * i));
			if (status) {
				DEV_ERR("%s: read blk(%d) failed:%d\n",
					__func__, i, status);
				goto error;
			}
		}
		break;
	default:
		DEV_ERR("%s: ddc read failed, not supported multi-blocks: %d\n",
			__func__, num_of_cea_blocks);
		status = -EPROTO;
		goto error;
	}

	if (num_of_cea_blocks) {
		cea_extension_ver = edid_buf[0x81];
	}

	
	
	DEV_INFO("%s: V=%d.%d #CEABlks=%d[V%d] ID=%s IEEE=%04x Ext=0x%02x\n",
		__func__, edid_buf[0x12], edid_buf[0x13],
		num_of_cea_blocks, cea_extension_ver, vendor_id, ieee_reg_id,
		edid_buf[0x80]);

	hdmi_edid_get_display_mode(edid_ctrl, edid_buf, num_of_cea_blocks, write_burst_vic);

	return 0;

error:
	edid_ctrl->sink_data.num_of_elements = 1;
	edid_ctrl->sink_data.disp_mode_list[0] = edid_ctrl->video_resolution;

	return status;
} 

u8 hdmi_edid_get_sink_scaninfo(void *input, u32 resolution)
{
	u8 scaninfo = 0;
	int use_ce_scan_info = true;
	struct hdmi_edid_ctrl *edid_ctrl = (struct hdmi_edid_ctrl *)input;

	if (!edid_ctrl) {
		DEV_ERR("%s: invalid input\n", __func__);
		goto end;
	}

	if (resolution == edid_ctrl->sink_data.preferred_video_format) {
		use_ce_scan_info = false;
		switch (edid_ctrl->pt_scan_info) {
		case 0:
			DEV_DBG("%s: No underscan info for preferred V fmt\n",
				__func__);
			use_ce_scan_info = true;
			break;
		case 3:
			DEV_DBG("%s: Set underscan bit for preferred V fmt\n",
				__func__);
			scaninfo = BIT(1);
			break;
		default:
			DEV_DBG("%s: Underscan not set for preferred V fmt\n",
				__func__);
			break;
		}
	}

	if (use_ce_scan_info) {
		if (3 == edid_ctrl->ce_scan_info) {
			DEV_DBG("%s: Setting underscan bit for CE video fmt\n",
				__func__);
			scaninfo |= BIT(1);
		} else {
			DEV_DBG("%s: Not setting underscan bit for CE V fmt\n",
				__func__);
		}
	}

end:
	return scaninfo;
} 

u32 hdmi_edid_get_sink_mode(void *input)
{
	struct hdmi_edid_ctrl *edid_ctrl = (struct hdmi_edid_ctrl *)input;

	if (!edid_ctrl) {
		DEV_ERR("%s: invalid input\n", __func__);
		return 0;
	}

	return edid_ctrl->sink_mode;
} 

bool hdmi_edid_is_s3d_mode_supported(void *input, u32 video_mode, u32 s3d_mode)
{
	int i;
	bool ret = false;
	struct hdmi_edid_ctrl *edid_ctrl = (struct hdmi_edid_ctrl *)input;
	struct hdmi_edid_sink_data *sink_data;

	if (!edid_ctrl) {
		DEV_ERR("%s: invalid input\n", __func__);
		return false;
	}
	sink_data = &edid_ctrl->sink_data;
	for (i = 0; i < sink_data->num_of_elements; ++i) {
		if (sink_data->disp_mode_list[i] != video_mode)
			continue;
		if (sink_data->disp_3d_mode_list[i] & (1 << s3d_mode))
			ret = true;
		else
			DEV_DBG("%s: return false: vic=%d caps=%x s3d=%d\n",
				__func__, video_mode,
				sink_data->disp_3d_mode_list[i], s3d_mode);
		break;
	}
	return ret;
}

int hdmi_edid_get_audio_blk(void *input, struct msm_hdmi_audio_edid_blk *blk)
{
	struct hdmi_edid_ctrl *edid_ctrl = (struct hdmi_edid_ctrl *)input;

	if (!edid_ctrl || !blk) {
		DEV_ERR("%s: invalid input\n", __func__);
		return -EINVAL;
	}

	blk->audio_data_blk = edid_ctrl->audio_data_block;
	blk->audio_data_blk_size = edid_ctrl->adb_size;

	blk->spk_alloc_data_blk = edid_ctrl->spkr_alloc_data_block;
	blk->spk_alloc_data_blk_size = edid_ctrl->sadb_size;

	return 0;
} 

void hdmi_edid_set_video_resolution(void *input, u32 resolution)
{
	struct hdmi_edid_ctrl *edid_ctrl = (struct hdmi_edid_ctrl *)input;

	if (!edid_ctrl) {
		DEV_ERR("%s: invalid input\n", __func__);
		return;
	}

	edid_ctrl->video_resolution = resolution;

	if (1 == edid_ctrl->sink_data.num_of_elements)
		edid_ctrl->sink_data.disp_mode_list[0] = resolution;
} 

void hdmi_edid_deinit(void *input)
{
	struct hdmi_edid_ctrl *edid_ctrl = (struct hdmi_edid_ctrl *)input;

	if (edid_ctrl) {
		sysfs_remove_group(edid_ctrl->init_data.sysfs_kobj,
			&hdmi_edid_fs_attrs_group);
		kfree(edid_ctrl);
	}
} 

void *hdmi_edid_init(struct hdmi_edid_init_data *init_data)
{
	struct hdmi_edid_ctrl *edid_ctrl = NULL;

	if (!init_data || !init_data->io ||
		!init_data->mutex || !init_data->sysfs_kobj ||
		!init_data->ddc_ctrl) {
		DEV_ERR("%s: invalid input\n", __func__);
		goto error;
	}

	edid_ctrl = kzalloc(sizeof(*edid_ctrl), GFP_KERNEL);
	if (!edid_ctrl) {
		DEV_ERR("%s: Out of memory\n", __func__);
		goto error;
	}

	edid_ctrl->init_data = *init_data;
	edid_ctrl->sink_mode = false;

	if (sysfs_create_group(init_data->sysfs_kobj,
		&hdmi_edid_fs_attrs_group)) {
		DEV_ERR("%s: EDID sysfs create failed\n", __func__);
		kfree(edid_ctrl);
		edid_ctrl = NULL;
	}

error:
	return (void *)edid_ctrl;
} 
