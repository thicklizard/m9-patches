#ifndef __LINUX_MSM_CAM_SENSOR_H
#define __LINUX_MSM_CAM_SENSOR_H

#ifdef MSM_CAMERA_BIONIC
#include <sys/types.h>
#endif

#ifndef CONFIG_CAMERA_DRIVER_VER_M
#include <linux/v4l2-mediabus.h>
#endif
#include <media/msm_camsensor_sdk.h>

#include <linux/types.h>
#include <linux/i2c.h>
#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif

#define I2C_SEQ_REG_SETTING_MAX   5

#define MSM_SENSOR_MCLK_8HZ   8000000
#define MSM_SENSOR_MCLK_16HZ  16000000
#define MSM_SENSOR_MCLK_24HZ  24000000

#define MAX_SENSOR_NAME 32
#define MAX_ACTUATOR_AF_TOTAL_STEPS 1024

#define MAX_OIS_MOD_NAME_SIZE 32
#define MAX_OIS_NAME_SIZE 32
#define MAX_OIS_REG_SETTINGS 800

#define MOVE_NEAR 0
#define MOVE_FAR  1

#define MSM_ACTUATOR_MOVE_SIGNED_FAR -1
#define MSM_ACTUATOR_MOVE_SIGNED_NEAR  1

#define MAX_EEPROM_NAME 32

#define MAX_AF_ITERATIONS 3
#define MAX_NUMBER_OF_STEPS 47

#ifdef CONFIG_CAMERA_DRIVER_VER_M
#define MAX_LED_TRIGGERS 3
#else
#define MSM_V4L2_PIX_FMT_META v4l2_fourcc('M', 'E', 'T', 'A') /* META */
#define MSM_V4L2_PIX_FMT_SBGGR14 v4l2_fourcc('B', 'G', '1', '4')
	/* 14  BGBG.. GRGR.. */
#define MSM_V4L2_PIX_FMT_SGBRG14 v4l2_fourcc('G', 'B', '1', '4')
	/* 14  GBGB.. RGRG.. */
#define MSM_V4L2_PIX_FMT_SGRBG14 v4l2_fourcc('B', 'A', '1', '4')
	/* 14  GRGR.. BGBG.. */
#define MSM_V4L2_PIX_FMT_SRGGB14 v4l2_fourcc('R', 'G', '1', '4')
	/* 14  RGRG.. GBGB.. */
#endif

/*HTC_START, HTC_VCM, Harvey 20130628 - Porting read OTP*/
#define MAX_ACT_NAME_SIZE 32
#define LC898212_HEX_MAX 0x7FFF //0x6A00
#define LC898212_HEX_MIN 0x8001 //0x9600
#define LC898212_DEC_MAX 1023
/*HTC_END, HTC_VCM*/

enum flash_type {
	LED_FLASH = 1,
	STROBE_FLASH,
	GPIO_FLASH
};

enum msm_sensor_resolution_t {
	MSM_SENSOR_RES_FULL,
	MSM_SENSOR_RES_QTR,
	MSM_SENSOR_RES_2,
	MSM_SENSOR_RES_3,
	MSM_SENSOR_RES_4,
	MSM_SENSOR_RES_5,
	MSM_SENSOR_RES_6,
	MSM_SENSOR_RES_7,
	MSM_SENSOR_INVALID_RES,
};

enum msm_camera_stream_type_t {
	MSM_CAMERA_STREAM_PREVIEW,
	MSM_CAMERA_STREAM_SNAPSHOT,
	MSM_CAMERA_STREAM_VIDEO,
	MSM_CAMERA_STREAM_INVALID,
};

enum sensor_sub_module_t {
	SUB_MODULE_SENSOR,
	SUB_MODULE_CHROMATIX,
	SUB_MODULE_ACTUATOR,
	SUB_MODULE_EEPROM,
	SUB_MODULE_LED_FLASH,
	SUB_MODULE_STROBE_FLASH,
	SUB_MODULE_CSID,
	SUB_MODULE_CSID_3D,
	SUB_MODULE_CSIPHY,
	SUB_MODULE_CSIPHY_3D,
	SUB_MODULE_OIS,
	SUB_MODULE_MAX,
};

enum {
	MSM_CAMERA_EFFECT_MODE_OFF,
	MSM_CAMERA_EFFECT_MODE_MONO,
	MSM_CAMERA_EFFECT_MODE_NEGATIVE,
	MSM_CAMERA_EFFECT_MODE_SOLARIZE,
	MSM_CAMERA_EFFECT_MODE_SEPIA,
	MSM_CAMERA_EFFECT_MODE_POSTERIZE,
	MSM_CAMERA_EFFECT_MODE_WHITEBOARD,
	MSM_CAMERA_EFFECT_MODE_BLACKBOARD,
	MSM_CAMERA_EFFECT_MODE_AQUA,
	MSM_CAMERA_EFFECT_MODE_EMBOSS,
	MSM_CAMERA_EFFECT_MODE_SKETCH,
	MSM_CAMERA_EFFECT_MODE_NEON,
	MSM_CAMERA_EFFECT_MODE_MAX
};

enum {
	MSM_CAMERA_WB_MODE_AUTO,
	MSM_CAMERA_WB_MODE_CUSTOM,
	MSM_CAMERA_WB_MODE_INCANDESCENT,
	MSM_CAMERA_WB_MODE_FLUORESCENT,
	MSM_CAMERA_WB_MODE_WARM_FLUORESCENT,
	MSM_CAMERA_WB_MODE_DAYLIGHT,
	MSM_CAMERA_WB_MODE_CLOUDY_DAYLIGHT,
	MSM_CAMERA_WB_MODE_TWILIGHT,
	MSM_CAMERA_WB_MODE_SHADE,
	MSM_CAMERA_WB_MODE_OFF,
	MSM_CAMERA_WB_MODE_MAX
};

enum {
	MSM_CAMERA_SCENE_MODE_OFF,
	MSM_CAMERA_SCENE_MODE_AUTO,
	MSM_CAMERA_SCENE_MODE_LANDSCAPE,
	MSM_CAMERA_SCENE_MODE_SNOW,
	MSM_CAMERA_SCENE_MODE_BEACH,
	MSM_CAMERA_SCENE_MODE_SUNSET,
	MSM_CAMERA_SCENE_MODE_NIGHT,
	MSM_CAMERA_SCENE_MODE_PORTRAIT,
	MSM_CAMERA_SCENE_MODE_BACKLIGHT,
	MSM_CAMERA_SCENE_MODE_SPORTS,
	MSM_CAMERA_SCENE_MODE_ANTISHAKE,
	MSM_CAMERA_SCENE_MODE_FLOWERS,
	MSM_CAMERA_SCENE_MODE_CANDLELIGHT,
	MSM_CAMERA_SCENE_MODE_FIREWORKS,
	MSM_CAMERA_SCENE_MODE_PARTY,
	MSM_CAMERA_SCENE_MODE_NIGHT_PORTRAIT,
	MSM_CAMERA_SCENE_MODE_THEATRE,
	MSM_CAMERA_SCENE_MODE_ACTION,
	MSM_CAMERA_SCENE_MODE_AR,
	MSM_CAMERA_SCENE_MODE_FACE_PRIORITY,
	MSM_CAMERA_SCENE_MODE_BARCODE,
	MSM_CAMERA_SCENE_MODE_HDR,
	MSM_CAMERA_SCENE_MODE_MAX
};

enum csid_cfg_type_t {
	CSID_INIT,
	CSID_CFG,
	CSID_RELEASE,
};

enum csiphy_cfg_type_t {
	CSIPHY_INIT,
	CSIPHY_CFG,
	CSIPHY_RELEASE,
};

enum camera_vreg_type {
	VREG_TYPE_DEFAULT,
	VREG_TYPE_CUSTOM,
	//HTC_CAM_START
	VREG_TYPE_GPIO,
	//HTC_CAM_END
};

enum sensor_af_t {
	SENSOR_AF_FOCUSSED,
	SENSOR_AF_NOT_FOCUSSED,
};

enum cci_i2c_master_t {
	MASTER_0,
	MASTER_1,
	MASTER_MAX,
};

struct msm_camera_i2c_array_write_config {
	struct msm_camera_i2c_reg_setting conf_array;
	uint16_t slave_addr;
};

struct msm_camera_i2c_read_config {
	uint16_t slave_addr;
	uint16_t reg_addr;
	enum msm_camera_i2c_data_type data_type;
	uint16_t data;
};

struct msm_camera_csi2_params {
	struct msm_camera_csid_params csid_params;
	struct msm_camera_csiphy_params csiphy_params;
	uint8_t csi_clk_scale_enable;
};

struct msm_camera_csi_lane_params {
	uint16_t csi_lane_assign;
	uint16_t csi_lane_mask;
};

struct csi_lane_params_t {
	uint16_t csi_lane_assign;
	uint8_t csi_lane_mask;
	uint8_t csi_if;
	int8_t csid_core[2];
	uint8_t csi_phy_sel;
};

struct msm_sensor_info_t {
	char     sensor_name[MAX_SENSOR_NAME];
	uint32_t session_id;
	int32_t  subdev_id[SUB_MODULE_MAX];
	int32_t  subdev_intf[SUB_MODULE_MAX];
	uint8_t  is_mount_angle_valid;
	uint32_t sensor_mount_angle;
	int modes_supported;
	enum camb_position_t position;
};

struct camera_vreg_t {
	const char *reg_name;
	int min_voltage;
	int max_voltage;
	int op_mode;
	uint32_t delay;
	const char *custom_vreg_name;
	enum camera_vreg_type type;
	//HTC_CAM_START
	int32_t gpios_index;
	//HTC_CAM_END
};

/*HTC_START, HTC_VCM, Harvey 20130628 - Porting read OTP*/
struct fuse_id{
	uint32_t fuse_id_word1;
	uint32_t fuse_id_word2;
	uint32_t fuse_id_word3;
	uint32_t fuse_id_word4;
};

typedef struct{
	char    ACT_NAME[MAX_ACT_NAME_SIZE]; /*HTC Harvey 20130701 - Set otp af value*/
	uint8_t VCM_START_MSB;
	uint8_t VCM_START_LSB;
	uint8_t AF_INF_MSB;
	uint8_t AF_INF_LSB;
	uint8_t AF_MACRO_MSB;
	uint8_t AF_MACRO_LSB;
	uint8_t VCM_BIAS;
	uint8_t VCM_OFFSET;
	uint8_t VCM_BOTTOM_MECH_MSB;
	uint8_t VCM_BOTTOM_MECH_LSB;
	uint8_t VCM_TOP_MECH_MSB;
	uint8_t VCM_TOP_MECH_LSB;
	uint8_t VCM_VENDOR_ID_VERSION;
	uint8_t VCM_VENDOR;
	uint8_t ACT_ID;
	uint32_t MODULE_ID_AB;
}af_value_t;
/*HTC_END, HTC_VCM*/

/*HTC_START, read alpha setting*/
struct alpha_value{
	uint8_t Alpha_Gb;
	uint8_t Alpha_B;
	uint8_t Alpha_R;
	uint8_t Alpha_Gr;
};
/*HTC_END*/

struct sensorb_cfg_data {
	int cfgtype;
	/*HTC_START, HTC_VCM, Harvey 20130628 - Porting read OTP*/
	int8_t sensor_ver;
	int8_t lens_id;
	af_value_t af_value;
	/*HTC_END, HTC_VCM*/
	union {
		struct msm_sensor_info_t      sensor_info;
		struct msm_sensor_init_params sensor_init_params;
		void                         *setting;
		/*HTC_START, HTC_VCM, Harvey 20130628 - Porting read OTP*/
		struct fuse_id fuse;
		/*HTC_END, HTC_VCM*/
	} cfg;
	/*HTC_START, check module type*/
	int module_type;    //0x00: In House module;   0x01: Altek module
	/*HTC_END*/
	/*HTC_START, PDAF data ptr*/
	uint8_t *pdaf_buffer;
	uint32_t pdaf_size;
	uint8_t is_pdaf_supported;
	/*HTC_END*/
	/*HTC_START, read alpha setting*/
	struct alpha_value alpha;
	/*HTC_END*/
};

struct csid_cfg_data {
	enum csid_cfg_type_t cfgtype;
	union {
		uint32_t csid_version;
		struct msm_camera_csid_params *csid_params;
	} cfg;
};

struct csiphy_cfg_data {
	enum csiphy_cfg_type_t cfgtype;
	union {
		struct msm_camera_csiphy_params *csiphy_params;
		struct msm_camera_csi_lane_params *csi_lane_params;
	} cfg;
};

enum eeprom_cfg_type_t {
	CFG_EEPROM_GET_INFO,
	CFG_EEPROM_GET_CAL_DATA,
	CFG_EEPROM_READ_CAL_DATA,
	CFG_EEPROM_WRITE_DATA,
	CFG_EEPROM_GET_MM_INFO,
};

struct eeprom_get_t {
	uint32_t num_bytes;
};

struct eeprom_read_t {
	uint8_t *dbuffer;
	uint32_t num_bytes;
};

struct eeprom_write_t {
	uint8_t *dbuffer;
	uint32_t num_bytes;
};

struct eeprom_get_cmm_t {
	uint32_t cmm_support;
	uint32_t cmm_compression;
	uint32_t cmm_size;
};

struct msm_eeprom_cfg_data {
	enum eeprom_cfg_type_t cfgtype;
	uint8_t is_supported;
	union {
		char eeprom_name[MAX_SENSOR_NAME];
		struct eeprom_get_t get_data;
		struct eeprom_read_t read_data;
		struct eeprom_write_t write_data;
		struct eeprom_get_cmm_t get_cmm_data;
	} cfg;
};

#ifdef CONFIG_COMPAT
struct msm_sensor_power_setting32 {
	enum msm_sensor_power_seq_type_t seq_type;
	uint16_t seq_val;
	compat_uint_t config_val;
	uint16_t delay;
	compat_uptr_t data[10];
};

struct msm_sensor_power_setting_array32 {
	struct msm_sensor_power_setting32 power_setting_a[MAX_POWER_CONFIG];
	compat_uptr_t power_setting;
	uint16_t size;
	struct msm_sensor_power_setting32
		power_down_setting_a[MAX_POWER_CONFIG];
	compat_uptr_t power_down_setting;
	uint16_t size_down;
};

struct msm_camera_sensor_slave_info32 {
	char sensor_name[32];
	char eeprom_name[32];
	char actuator_name[32];
	char ois_name[32];
	char flash_name[32];
	enum msm_sensor_camera_id_t camera_id;
	uint16_t slave_addr;
	enum i2c_freq_mode_t i2c_freq_mode;
	enum msm_camera_i2c_reg_addr_type addr_type;
	struct msm_sensor_id_info_t sensor_id_info;
	struct msm_sensor_power_setting_array32 power_setting_array;
	uint8_t  is_init_params_valid;
	struct msm_sensor_init_params sensor_init_params;
	uint8_t is_flash_supported;
};

struct msm_camera_csid_lut_params32 {
	uint8_t num_cid;
	struct msm_camera_csid_vc_cfg vc_cfg_a[MAX_CID];
	compat_uptr_t vc_cfg[MAX_CID];
};

struct msm_camera_csid_params32 {
	uint8_t lane_cnt;
	uint16_t lane_assign;
	uint8_t phy_sel;
	uint32_t csi_clk;
	struct msm_camera_csid_lut_params32 lut_params;
};

struct msm_camera_csi2_params32 {
	struct msm_camera_csid_params32 csid_params;
	struct msm_camera_csiphy_params csiphy_params;
	uint8_t csi_clk_scale_enable;
};

struct csid_cfg_data32 {
	enum csid_cfg_type_t cfgtype;
	union {
		uint32_t csid_version;
		compat_uptr_t csid_params;
	} cfg;
};

struct eeprom_read_t32 {
	compat_uptr_t dbuffer;
	uint32_t num_bytes;
};

struct eeprom_write_t32 {
	compat_uptr_t dbuffer;
	uint32_t num_bytes;
};

struct msm_eeprom_cfg_data32 {
	enum eeprom_cfg_type_t cfgtype;
	uint8_t is_supported;
	union {
		char eeprom_name[MAX_SENSOR_NAME];
		struct eeprom_get_t get_data;
		struct eeprom_read_t32 read_data;
		struct eeprom_write_t32 write_data;
	} cfg;
};

struct msm_camera_i2c_seq_reg_setting32 {
	compat_uptr_t reg_setting;
	uint16_t size;
	enum msm_camera_i2c_reg_addr_type addr_type;
	uint16_t delay;
};
#endif

enum msm_sensor_cfg_type_t {
	CFG_SET_SLAVE_INFO,
	CFG_SLAVE_READ_I2C,
	CFG_WRITE_I2C_ARRAY,
	CFG_SLAVE_WRITE_I2C_ARRAY,
	CFG_WRITE_I2C_SEQ_ARRAY,
	CFG_POWER_UP,
	CFG_POWER_DOWN,
	CFG_SET_STOP_STREAM_SETTING,
	CFG_GET_SENSOR_INFO,
	CFG_GET_SENSOR_INIT_PARAMS,
	CFG_SET_INIT_SETTING,
	CFG_SET_RESOLUTION,
	CFG_SET_STOP_STREAM,
	CFG_SET_START_STREAM,
	CFG_SET_SATURATION,
	CFG_SET_CONTRAST,
	CFG_SET_SHARPNESS,
	CFG_SET_ISO,
	CFG_SET_EXPOSURE_COMPENSATION,
	CFG_SET_ANTIBANDING,
	CFG_SET_BESTSHOT_MODE,
	CFG_SET_EFFECT,
	CFG_SET_WHITE_BALANCE,
	CFG_SET_AUTOFOCUS,
	CFG_CANCEL_AUTOFOCUS,
	CFG_SET_STREAM_TYPE,
/*HTC_START, HTC_VCM, Harvey 20130628 - Porting read OTP*/
	CFG_I2C_IOCTL_R_OTP,
/*HTC_END, HTC_VCM*/
/*HTC_START, read camera emmc*/
	CFG_I2C_IOCTL_R_EMMC,
/*HTC_END*/
/*HTC_START, GYRO Calibration*/
	CFG_SET_GYRO_CALIBRATION,
/*HTC_END*/
/*HTC_START, PDAF data ptr*/
	CFG_GET_PDAF_SIZE,
	CFG_READ_PDAF_DATA,
/*HTC_END*/
};

enum msm_actuator_cfg_type_t {
/*HTC_START, HTC_VCM, Harvey 20130701 - Set otp af value*/
	CFG_SET_ACTUATOR_AF_VALUE,
/*HTC_END, HTC_VCM*/
	CFG_GET_ACTUATOR_INFO,
	CFG_SET_ACTUATOR_INFO,
	CFG_SET_DEFAULT_FOCUS,
	CFG_MOVE_FOCUS,
	CFG_SET_POSITION,
	CFG_ACTUATOR_POWERDOWN,
	CFG_ACTUATOR_POWERUP,
	CFG_ACTUATOR_INIT,
};

enum msm_ois_cfg_type_t {
	CFG_OIS_INIT,
	CFG_OIS_POWERDOWN,
	CFG_OIS_POWERUP,
	CFG_OIS_CONTROL,
	CFG_OIS_I2C_WRITE_SEQ_TABLE,
};

enum msm_ois_i2c_operation {
	MSM_OIS_WRITE = 0,
	MSM_OIS_POLL,
};

struct reg_settings_ois_t {
	uint16_t reg_addr;
	enum msm_camera_i2c_reg_addr_type addr_type;
	uint32_t reg_data;
	enum msm_camera_i2c_data_type data_type;
	enum msm_ois_i2c_operation i2c_operation;
	uint32_t delay;
};

struct msm_ois_params_t {
	uint16_t data_size;
	uint16_t setting_size;
	uint32_t i2c_addr;
	enum msm_camera_i2c_reg_addr_type i2c_addr_type;
	enum msm_camera_i2c_data_type i2c_data_type;
	struct reg_settings_ois_t *settings;
};

struct msm_ois_set_info_t {
	struct msm_ois_params_t ois_params;
};

struct msm_actuator_move_params_t {
	int8_t dir;
	int8_t sign_dir;
	int16_t dest_step_pos;
	int32_t num_steps;
	uint16_t curr_lens_pos;
	struct damping_params_t *ringing_params;
};

struct msm_actuator_tuning_params_t {
	int16_t initial_code;
	uint16_t pwd_step;
	uint16_t region_size;
	uint32_t total_steps;
	struct region_params_t *region_params;
};

struct park_lens_data_t {
	uint32_t damping_step;
	uint32_t damping_delay;
	uint32_t hw_params;
	uint32_t max_step;
};

struct msm_actuator_params_t {
	enum actuator_type act_type;
	uint8_t reg_tbl_size;
	uint16_t data_size;
	uint16_t init_setting_size;
	uint32_t i2c_addr;
	enum msm_actuator_addr_type i2c_addr_type;
	enum msm_actuator_data_type i2c_data_type;
	struct msm_actuator_reg_params_t *reg_tbl_params;
	struct reg_settings_t *init_settings;
	struct park_lens_data_t park_lens;
	/*HTC_START, HTC_VCM*/
	char ACT_NAME[MAX_ACT_NAME_SIZE];
	/*HTC_END, HTC_VCM*/
};

/*HTC_START, HTC_VCM, support multiple I2C access type for actuator modulation*/
enum actuator_I2C_func_select {
	WRITE_SEQ_TABLE,
	WRITE_TABLE_W_MICRODELAY,
	WRITE_MULTI_TABLE
};
/*HTC_END, HTC_VCM*/

struct msm_actuator_set_info_t {
	struct msm_actuator_params_t actuator_params;
	struct msm_actuator_tuning_params_t af_tuning_params;
/*HTC_START, HTC_VCM, for actuator modulation*/
	uint8_t enable_focus_step_log;
	uint16_t *step_position_table;                //Move step position table to user space
	enum actuator_I2C_func_select act_i2c_select; //support multiple I2C access type
/*HTC_END, HTC_VCM*/
};

struct msm_actuator_get_info_t {
	uint32_t focal_length_num;
	uint32_t focal_length_den;
	uint32_t f_number_num;
	uint32_t f_number_den;
	uint32_t f_pix_num;
	uint32_t f_pix_den;
	uint32_t total_f_dist_num;
	uint32_t total_f_dist_den;
	uint32_t hor_view_angle_num;
	uint32_t hor_view_angle_den;
	uint32_t ver_view_angle_num;
	uint32_t ver_view_angle_den;
};

enum af_camera_name {
	ACTUATOR_MAIN_CAM_0,
	ACTUATOR_MAIN_CAM_1,
	ACTUATOR_MAIN_CAM_2,
	ACTUATOR_MAIN_CAM_3,
	ACTUATOR_MAIN_CAM_4,
	ACTUATOR_MAIN_CAM_5,
	ACTUATOR_WEB_CAM_0,
	ACTUATOR_WEB_CAM_1,
	ACTUATOR_WEB_CAM_2,
};

/*HTC_START, HTC_VCM, Harvey 20130701 - Set otp af value*/
struct msm_actuator_af_OTP_info_t {
	uint8_t VCM_OTP_Read;
	uint16_t VCM_Start;
	uint16_t VCM_Infinity;
	uint16_t VCM_Macro;
	/* HTC_START pg 20130220 lc898212 act enable */
	uint8_t VCM_Bias;
	uint8_t VCM_Offset;
	uint16_t VCM_Bottom_Mech;
	uint16_t VCM_Top_Mech;
	uint8_t VCM_Vendor_Id_Version;
	/* HTC_END pg 20130220 lc898212 act enable */
	uint8_t VCM_Vendor;
	uint8_t act_id;
	char act_name[MAX_SENSOR_NAME];
};
/*HTC_END, HTC_VCM*/

struct msm_ois_cfg_data {
	int cfgtype;
	union {
		struct msm_ois_set_info_t set_info;
		struct msm_camera_i2c_seq_reg_setting *settings;
	} cfg;
};

struct msm_actuator_set_position_t {
	uint16_t number_of_steps;
	uint16_t pos[MAX_NUMBER_OF_STEPS];
	uint16_t delay[MAX_NUMBER_OF_STEPS];
};

struct msm_actuator_cfg_data {
	int cfgtype;
	uint8_t is_af_supported;
	union {
		struct msm_actuator_move_params_t move;
		struct msm_actuator_set_info_t set_info;
		struct msm_actuator_get_info_t get_info;
		struct msm_actuator_set_position_t setpos;
		enum af_camera_name cam_name;
		/*HTC_START, HTC_VCM, Harvey 20130701 - Set otp af value*/
		af_value_t af_value;
		/*HTC_END, HTC_VCM*/
	} cfg;
};

enum msm_camera_led_config_t {
	MSM_CAMERA_LED_OFF,
	MSM_CAMERA_LED_LOW,
	MSM_CAMERA_LED_HIGH,
	MSM_CAMERA_LED_INIT,
	MSM_CAMERA_LED_RELEASE,
};

struct msm_camera_led_cfg_t {
	enum msm_camera_led_config_t cfgtype;
	int32_t torch_current[MAX_LED_TRIGGERS];
	int32_t flash_current[MAX_LED_TRIGGERS];
	int32_t flash_duration[MAX_LED_TRIGGERS];
};

#ifdef CONFIG_CAMERA_DRIVER_VER_M
struct msm_flash_init_info_t {
	enum msm_flash_driver_type flash_driver_type;
	struct msm_sensor_power_setting_array *power_setting_array;
	struct msm_camera_i2c_reg_setting_array *settings;
};
#else
struct msm_flash_init_info_t {
	enum msm_flash_driver_type flash_driver_type;
	uint32_t slave_addr;
	struct msm_sensor_power_setting_array *power_setting_array;
	struct msm_camera_i2c_reg_setting_array *settings;
};
#endif

struct msm_flash_cfg_data_t {
	enum msm_flash_cfg_type_t cfg_type;
	int32_t flash_current[MAX_LED_TRIGGERS];
	int32_t flash_duration[MAX_LED_TRIGGERS];
	union {
		struct msm_flash_init_info_t *flash_init_info;
		struct msm_camera_i2c_reg_setting_array *settings;
	} cfg;
};

/* sensor init structures and enums */
enum msm_sensor_init_cfg_type_t {
	CFG_SINIT_PROBE,
	CFG_SINIT_PROBE_DONE,
	CFG_SINIT_PROBE_WAIT_DONE,
};

struct sensor_init_cfg_data {
	enum msm_sensor_init_cfg_type_t cfgtype;
	struct msm_sensor_info_t        probed_info;
	char                            entity_name[MAX_SENSOR_NAME];
	union {
		void *setting;
	} cfg;
};

#define VIDIOC_MSM_SENSOR_CFG \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 1, struct sensorb_cfg_data)

#define VIDIOC_MSM_SENSOR_RELEASE \
	_IO('V', BASE_VIDIOC_PRIVATE + 2)

#define VIDIOC_MSM_SENSOR_GET_SUBDEV_ID \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 3, uint32_t)

#define VIDIOC_MSM_CSIPHY_IO_CFG \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 4, struct csiphy_cfg_data)

#define VIDIOC_MSM_CSID_IO_CFG \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 5, struct csid_cfg_data)

#define VIDIOC_MSM_ACTUATOR_CFG \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 6, struct msm_actuator_cfg_data)

#define VIDIOC_MSM_FLASH_LED_DATA_CFG \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 7, struct msm_camera_led_cfg_t)

#define VIDIOC_MSM_EEPROM_CFG \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 8, struct msm_eeprom_cfg_data)

#define VIDIOC_MSM_SENSOR_GET_AF_STATUS \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 9, uint32_t)

#define VIDIOC_MSM_SENSOR_INIT_CFG \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 10, struct sensor_init_cfg_data)

#define VIDIOC_MSM_OIS_CFG \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 11, struct msm_ois_cfg_data)

#define VIDIOC_MSM_FLASH_CFG \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 13, struct msm_flash_cfg_data_t)

#ifdef CONFIG_COMPAT
struct msm_camera_i2c_reg_setting32 {
	compat_uptr_t reg_setting;
	uint16_t size;
	enum msm_camera_i2c_reg_addr_type addr_type;
	enum msm_camera_i2c_data_type data_type;
	uint16_t delay;
};

struct msm_actuator_tuning_params_t32 {
	int16_t initial_code;
	uint16_t pwd_step;
	uint16_t region_size;
	uint32_t total_steps;
	compat_uptr_t region_params;
};

struct msm_actuator_params_t32 {
	enum actuator_type act_type;
	uint8_t reg_tbl_size;
	uint16_t data_size;
	uint16_t init_setting_size;
	uint32_t i2c_addr;
	enum msm_actuator_addr_type i2c_addr_type;
	enum msm_actuator_data_type i2c_data_type;
	compat_uptr_t reg_tbl_params;
	compat_uptr_t init_settings;
	struct park_lens_data_t park_lens;
	/*HTC_START, HTC_VCM*/
	char ACT_NAME[MAX_ACT_NAME_SIZE];
	/*HTC_END, HTC_VCM*/
};

struct msm_actuator_set_info_t32 {
	struct msm_actuator_params_t32 actuator_params;
	struct msm_actuator_tuning_params_t32 af_tuning_params;
/*HTC_START, HTC_VCM, for actuator modulation*/
	uint8_t enable_focus_step_log;
	uint16_t *step_position_table;                //Move step position table to user space
	enum actuator_I2C_func_select act_i2c_select; //support multiple I2C access type
/*HTC_END, HTC_VCM*/
};

struct sensor_init_cfg_data32 {
	enum msm_sensor_init_cfg_type_t cfgtype;
	struct msm_sensor_info_t        probed_info;
	char                            entity_name[MAX_SENSOR_NAME];
	union {
		compat_uptr_t setting;
	} cfg;
};

struct msm_actuator_move_params_t32 {
	int8_t dir;
	int8_t sign_dir;
	int16_t dest_step_pos;
	int32_t num_steps;
	uint16_t curr_lens_pos;
	compat_uptr_t ringing_params;
};

struct msm_actuator_cfg_data32 {
	int cfgtype;
	uint8_t is_af_supported;
	union {
		struct msm_actuator_move_params_t32 move;
		struct msm_actuator_set_info_t32 set_info;
		struct msm_actuator_get_info_t get_info;
		struct msm_actuator_set_position_t setpos;
		enum af_camera_name cam_name;
		/*HTC_START, HTC_VCM, Harvey 20130701 - Set otp af value*/
		af_value_t af_value;
		/*HTC_END, HTC_VCM*/
	} cfg;
};

struct csiphy_cfg_data32 {
	enum csiphy_cfg_type_t cfgtype;
	union {
		compat_uptr_t csiphy_params;
		compat_uptr_t csi_lane_params;
	} cfg;
};

struct sensorb_cfg_data32 {
	int cfgtype;
	/*HTC_START, HTC_VCM, Harvey 20130628 - Porting read OTP*/
	int8_t sensor_ver;
	int8_t lens_id;
	af_value_t af_value;
	/*HTC_END, HTC_VCM*/
	union {
		struct msm_sensor_info_t      sensor_info;
		struct msm_sensor_init_params sensor_init_params;
		compat_uptr_t                 setting;
		/*HTC_START, HTC_VCM, Harvey 20130628 - Porting read OTP*/
		struct fuse_id fuse;
		/*HTC_END, HTC_VCM*/
	} cfg;
	/*HTC_START, check module type*/
	int module_type;    //0x00: In House module;   0x01: Altek module
	/*HTC_END*/
	/*HTC_START, PDAF data ptr*/
	compat_uptr_t pdaf_buffer;
	uint32_t pdaf_size;
	uint8_t is_pdaf_supported;
	/*HTC_END*/
	/*HTC_START, read alpha setting*/
	struct alpha_value alpha;
	/*HTC_END*/
};

struct msm_ois_params_t32 {
	uint16_t data_size;
	uint16_t setting_size;
	uint32_t i2c_addr;
	enum msm_camera_i2c_reg_addr_type i2c_addr_type;
	enum msm_camera_i2c_data_type i2c_data_type;
	compat_uptr_t settings;
};

struct msm_ois_set_info_t32 {
	struct msm_ois_params_t32 ois_params;
};

struct msm_ois_cfg_data32 {
	int cfgtype;
	union {
		struct msm_ois_set_info_t32 set_info;
		compat_uptr_t settings;
	} cfg;
};

#ifdef CONFIG_CAMERA_DRIVER_VER_M
struct msm_flash_init_info_t32 {
	enum msm_flash_driver_type flash_driver_type;
	compat_uptr_t power_setting_array;
	compat_uptr_t settings;
};
#else
struct msm_flash_init_info_t32 {
	enum msm_flash_driver_type flash_driver_type;
	uint32_t slave_addr;
	compat_uptr_t power_setting_array;
	compat_uptr_t settings;
};
#endif

struct msm_flash_cfg_data_t32 {
	enum msm_flash_cfg_type_t cfg_type;
	int32_t flash_current[MAX_LED_TRIGGERS];
	int32_t flash_duration[MAX_LED_TRIGGERS];
	union {
		compat_uptr_t flash_init_info;
		compat_uptr_t settings;
	} cfg;
};

#define VIDIOC_MSM_ACTUATOR_CFG32 \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 6, struct msm_actuator_cfg_data32)

#define VIDIOC_MSM_SENSOR_INIT_CFG32 \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 10, struct sensor_init_cfg_data32)

#define VIDIOC_MSM_CSIPHY_IO_CFG32 \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 4, struct csiphy_cfg_data32)

#define VIDIOC_MSM_SENSOR_CFG32 \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 1, struct sensorb_cfg_data32)

#define VIDIOC_MSM_EEPROM_CFG32 \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 8, struct msm_eeprom_cfg_data32)

#define VIDIOC_MSM_OIS_CFG32 \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 11, struct msm_ois_cfg_data32)

#define VIDIOC_MSM_CSID_IO_CFG32 \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 5, struct csid_cfg_data32)

#define VIDIOC_MSM_FLASH_CFG32 \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 13, struct msm_flash_cfg_data_t32)
#endif

#endif /* __LINUX_MSM_CAM_SENSOR_H */
