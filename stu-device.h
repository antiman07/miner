#ifndef  __STU_DEVICE__H__
#define  __STU_DEVICE__H__
#include <libgen.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/time.h>
#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <stdbool.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <linux/rtc.h>
#include <time.h>
#include "config.h"
#include "spi-context.h"
#include "miner.h"
#include "elist.h"

#ifdef USE_U6
#define TOTAL_CORE  6

#else //h8
#define TOTAL_CORE  181
#endif
#define spiFreq	0x01
#define CMD_AUTO_ADDR		0xa5
#define CMD_READ_RESULT	0x44
#define CMD_READ_REG		0x6b
#define CMD_WRITE_REG		0x86
#define ADDR_BROADCAST		0x00
#define CMD_WRITE_JOB		0x33
#define INIT_DIFF		0xffff01ff

#define ASIC_RESULT_LEN		8
#define READ_RESULT_LEN		(ASIC_RESULT_LEN+2)

#define ASIC_CHAIN_NUM		3
#define ASIC_CHIP_NUM		3
//#define ASIC_CHIP_NUM		181

#define FPGA_DEBUG 		1

#define JOB_LENGTH		80

#define MAX_CMD_LENGTH		(JOB_LENGTH + ASIC_CHIP_NUM*2*2+64)
#define spiDiv 0x01

struct work_ent {
	struct work *work;
	struct list_head head;
};

struct work_queue {
	int num_elems;
	struct list_head head;
};

struct stu_chip 
{
	uint8_t if_chip_ok; // 1:okchip, 0:error
	uint8_t ft_pass_cnt;
	struct work *work[2];
	uint8_t pllCnt[4];
	int hw_errors;
	uint32_t pll[4];
	uint32_t chipNewpll;
	int stales;
	int chip_nonces_found;
	int core_acc[TOTAL_CORE+1];
	int core_hwe[TOTAL_CORE+1];
	int acc_5min;
	int hwe_5min;
	int acc_10min;
	int hwe_10min;
	int crc_error;
	int crc_right;
};

struct stu_chain 
{
	int fd;
	int hw_errors;
	char *devname;
	uint32_t  set_job_cnt;
	int stales;
	int nonces_found;
	int nonce_ranges_done;
	int chain_id;
	int num_chips;
	int num_cores;
	int num_active_chips;

	uint32_t newpll;
	uint32_t finalpll;
	uint32_t avpll;
	uint8_t read_chipid;
	bool need_flush_job;
	bool need_reset_board;
	bool update_pll_finish;
	bool start_update_pll;
	bool need_wtlevel;
	bool need_autopll;
	bool enable_auto_volt;
	bool start_speed;
	int right_nonce;
	int wrong_nonce;

	
	uint32_t current_HTarget6;
 
	pthread_mutex_t lock;

	struct stu_chip *chips;
	struct spi_ctx *spi_ctx;
	struct cgpu_info *cgpu;
	struct work_queue active_wq;
	struct timeval last_set_work_t;
	struct timeval last_get_nonce_t;
	struct timeval last_update_core_t;
	uint8_t last_update_core_num;
	bool core_all_boot;
	uint8_t last_reboot_core_num;
	struct timeval last_reboot_core_t;

	//for update pll0 <-> pll1
	struct timeval last_switchPll_t;
	uint32_t total_ft_pass_core;
	uint8_t last_switchCore_num;
	uint8_t last_swtichChip_num;
	uint8_t switch_direct;// 0(pll0->pll1), 1(pll1->pll0)
	uint8_t last_ft_chipnum;
	uint8_t last_autopll_chip;
	int reboot_cnt;
	struct timeval last_ft_t;
	struct timeval last_cfg_reg_t;
	struct timeval last_update_pll_t;
	struct timeval last_autopll_t;
	struct timeval last_log_t;
	struct timeval update_pll_finish_t;
	struct timeval last_checkCrc_t;
	struct timeval last_confReg_t;
	struct timeval last_vol;
};

#define I2C0_BUS		"/dev/i2c-0"
#define I2C1_BUS		"/dev/i2c-1"
#define I2C2_BUS		"/dev/i2c-2"
#define I2C3_BUS		"/dev/i2c-3"
#define SYSFS_GPIO_EXPORT	"/sys/class/gpio/export"
#define SYSFS_GPIO_DIR_STR	"/sys/class/gpio/gpio%d/direction"
#define SYSFS_GPIO_VAL_STR	"/sys/class/gpio/gpio%d/value"

#define SYSFS_SPI_EXPORT "/sys/devices/soc0/amba/f8007000.devcfg/fclk_export"
#define SYSFS_SPI_VAL_STR "/sys/devices/soc0/amba/f8007000.devcfg/fclk/fclk1/set_rate"

#define SYSFS_GPIO_DIR_OUT	"out"
#define SYSFS_GPIO_DIR_IN	"in"

/*ADD SEYMOUR LED POWER*/
#define LED_DISABLE "0"
#define LED_ENABLE "1"

/*

lnhere 风扇
Duty_cycle        R.P.M
100            6300(+/-)10%

period(300hz-60khz)              duty_cycle  是period的10%-100%之间，不能超出这个范围，不然会出现无效的参数，无法设置；
1000000（1kHz）                    100000
500000（2kHz）
250000（4khz）
.
.
40000(25khz)
*/

#define SYSFS_PWM_EXPORT "/sys/class/pwm/pwmchip%d/export" // hw pwm5 , fan2, J1103
#define SYSFS_PWM_PWM0 "/sys/class/pwm/pwmchip%d/pwm0"
#define SYSFS_PWM_ENABLE "/sys/class/pwm/pwmchip%d/pwm0/enable" 
#define SYSFS_PWM_PERIOD "/sys/class/pwm/pwmchip%d/pwm0/period" 
#define SYSFS_PWM_DUTY_CYCLE "/sys/class/pwm/pwmchip%d/pwm0/duty_cycle"

#define SYSFS_AD_IN1_GPIO 11  //FAN2 GPIO1_11
#define SYSFS_AD_IN2_GPIO 12  //FAN1 GPIO1_12
#define ENABLE "1"
#define DISABLE "0"

#define PWM "0"
#define FAN_PWM_FREQ_TARGET 500000 //(2Khz) //40000 // (25Khz)
#define FAN_PWM_FREQ 1000000 //1Khz
#define fanid 1


/*
led 

*/
#define SYSFS_LED_BT "/sys/class/leds/user%d/brightness"
#define SYSFS_LED_TR "/sys/class/leds/user%d/trigger"
	
#define SYSFS_LED_TIMER "timer"
#define SYSFS_LED_HEARTBEAT "heartbeat"

struct i2c_ctx *temp_sensor[3];
struct i2c_ctx *volt_setting;

#define ASIC_STU_FAN_PWM0_DEVICE_NAME  ("/dev/pwmgen0.0")
#define ASIC_STU_FAN_PWM_STEP          (10)
#define ASIC_STU_FAN_PWM_DUTY_MAX      (100)
#define ASIC_STU_FAN_PWM_FREQ_TARGET   (7000)
#define ASIC_STU_FAN_PWM_FREQ          (50000000 / ASIC_STU_FAN_PWM_FREQ_TARGET)
#define ASIC_STU_FAN_TEMP_VAL_THRESHOLD (445)
#define ASIC_STU_FAN_TEMP_SHUTDOWN_THRESHOLD (90.0f)
#define ASIC_STU_FAN_TEMP_MAX_THRESHOLD (75.0f)
#define ASIC_STU_FAN_TEMP_UP_THRESHOLD (60.0f)
#define ASIC_STU_FAN_TEMP_MID_THRESHOLD (50.0f)
#define ASIC_STU_FAN_TEMP_DOWN_THRESHOLD (30.0f)
#define ASIC_STU_FAN_TEMP_MARGIN_RATE  (5.0f / 72)
#define ASIC_STU_FAN_CTLR_FREQ_DIV     (1000)


extern bool if_hashboard_cooldown ;
extern bool opt_enable_hc;
extern float g_borad_volt[3];
extern double g_board_freq[3];
extern float g_board_temp[3];
extern float g_board_maxtemp;
extern bool volt_down;
#define TEMP_BUS "/dev/i2c-%d"
#define TEMP_ADDR 0x48

#define SYSFS_GPIO_VAL_LOW	"0"
#define SYSFS_GPIO_VAL_HIGH	"1"

extern int SPI_PIN_POWER_EN[4];
extern int SPI_PIN_RESET[4];
extern float workvolt;
extern float startvol;
extern float changevol;

extern float fan0;
extern float fan1;
extern uint32_t pll0;
extern uint32_t pll1;
extern uint32_t pll2;
extern uint32_t pll3;
extern uint32_t STEP;
extern uint32_t pllstart;
extern char g_miner_version[40];
extern char g_miner_compiletime[40];
extern char g_miner_type[40];

extern uint32_t upperpll;
extern uint32_t lowerpll;
extern uint32_t gt_upstep;
extern uint32_t gt_downstep;
extern uint32_t lt_upstep;
extern uint32_t lt_downstep;
extern float gt_uppercent;
extern float gt_lowpercent;
extern float lt_uppercent;
extern float lt_lowpercent;
extern bool enlog;



extern void inline upgrade_pll(struct stu_chain *achain);
extern void swap_data(uint8_t *data, int len);
extern bool spi_poll_result(struct spi_ctx *ctx, uint8_t cmd, uint8_t chip_id, uint8_t *buff, int len);
extern bool cmd_auto_address(struct spi_ctx *ctx, uint8_t chip_id);
extern bool cmd_set_pll3(struct spi_ctx *ctx, uint8_t chip_id, uint32_t pll);
extern bool cmd_set_pll2(struct spi_ctx *ctx, uint8_t chip_id, uint32_t pll);
extern bool cmd_set_pll1(struct spi_ctx *ctx, uint8_t chip_id, uint32_t pll);
extern bool cmd_set_pll0(struct spi_ctx *ctx, uint8_t chip_id, uint32_t pll);
extern bool cmd_update_pll3(struct spi_ctx *ctx, uint8_t chip_id, uint32_t pll);
extern bool cmd_update_pll2(struct spi_ctx *ctx, uint8_t chip_id, uint32_t pll);
extern bool cmd_update_pll1(struct spi_ctx *ctx, uint8_t chip_id, uint32_t pll);
extern bool cmd_update_pll0(struct spi_ctx *ctx, uint8_t chip_id, uint32_t pll);
extern bool cmd_write_register_1(struct spi_ctx *ctx, uint8_t chip_id, uint8_t DlyEn, uint16_t DlyVal);
extern bool cmd_write_register_2(struct spi_ctx *ctx, uint8_t chip_id, uint8_t spdEn, uint8_t spdVid, uint8_t glbSpd);
extern bool cmd_write_register_3(struct spi_ctx *ctx, uint8_t chip_id, uint8_t crcVal, uint8_t RollTimeEn, uint32_t RollTimeVal);
extern bool cmd_write_register_4(struct spi_ctx *ctx, uint8_t chip_id, uint32_t nonceTarget);
extern bool cmd_write_register_5(struct spi_ctx *ctx, uint8_t chip_id, uint8_t spi_div);
extern bool cmd_write_register_6(struct spi_ctx *ctx, uint8_t chip_id, uint8_t restProtect, uint8_t cfgRest,
						uint8_t spdGo,uint8_t sycNum, uint32_t spdSetupT);
extern bool cmd_write_register_7(struct spi_ctx *ctx, uint8_t chip_id, uint8_t en_cores);
extern bool power_on_off(uint8_t value);
extern void inline auto_adjust_pll3(struct stu_chain *achain);
extern bool stu_board_set_voltage(float voltage);
extern bool stu_board_hw_reset(int chain_id);
extern void stu_board_set_reset_low(int chain_id);
extern void stu_board_fan_pwm_set(float duty1,  float duty2);
extern float get_temp_val(void);

extern int Get_fan_fre(unsigned char num);
extern bool stu_board_fan_update(void);


#endif
