#include "stu-device.h"
#include "i2c-context.h"
#include "spi-context.h"

static pthread_mutex_t volt_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t temp_lock = PTHREAD_MUTEX_INITIALIZER;

bool b_dc2dc = false;
bool b_abort_exit = false;
bool volt_down = false;
bool already_volt_down = false;
bool volt_up = false;
bool already_volt_up = false;
int duty_driver1 = 0;
int duty_driver2 = 0;
int freq_detector;
float g_borad_volt[3] = {10.5, 11.6, 12.7};
float g_board_temp[3] = {0.0, 0.0,  0.0};
float g_board_maxtemp = 0;
double g_board_freq[3] = {300.33, 500.55, 550.66};
float g_average_temp;
uint8_t g_fan_speed;
float fan_duty;

float vol = 0;


 void flush_spi(struct spi_ctx *ctx)
{
	uint8_t buffer[4*ASIC_CHIP_NUM+10] = {0};
	spi_send_data(ctx, buffer, 4*ASIC_CHIP_NUM+10);
}

unsigned short CRC16 (unsigned char* pchMsg, unsigned short wDataLen)
{
        unsigned short wCRC = 0xFFFF;
        unsigned short i;
        unsigned char chChar;

        unsigned char tmp;
        unsigned short crcAccum = 0xFFFF;

        for (i = 0; i < wDataLen; i++)
        {
                chChar = *pchMsg++;

                tmp = chChar ^ (unsigned char)(crcAccum &0xff);
                tmp ^= (tmp<<4);
                crcAccum = (crcAccum>>8) ^ (tmp<<8) ^ (tmp <<3) ^ (tmp>>4);

        }
		
	return crcAccum;
}

void swap_data(uint8_t *data, int len)
{	
	int i;
	uint8_t tmp;
	for (i=0; i<len; i+=2)
	{
		tmp = data[i];
		data[i] = data[i+1];
		data[i+1] = tmp;
	}
}

uint8_t data_nonce[JOB_LENGTH]=
{
0x01,0x33,0x01,0x00,0x00,0x00,0x20,0x00,0xc8,0x45,0xef,0xaa,0xbb,0xae,0x0d,0x8e,0xd2,0xb4,0x07,0xc7,0xe5,0x16,0xb6,0x5a,0x9e,0xcc,0xf1,
0xd8,0x01,0xde,0x33,0xb9,0x00,0x1b,0x00,0x00,0x00,0x00,0x00,0x00,0xf4,0x04,0x80,0x90,0x71,0x08,0xc9,0xb4,0x0e,0xf2,0x14,0x57,0x43,0x66,
0x59,0x2f,0x01,0x08,0x57,0x67,0xd7,0x2d,0x2f,0xe8,0x6c,0xef,0x79,0x73,0x99,0x67,0x0b,0x83,0xa1,0xf9,0x5d,0x74,0xe9,0x2d,0x19,0x25,
/*0x00,0x00,0x00,0x00,*/
0x00,0xa0,0x00,0x00,
0x07,0x98
};

bool cmd_write_job(struct stu_chain *achain, uint8_t chip_id, uint8_t *job)
{
	bool ret = true;
	struct spi_ctx *ctx = achain->spi_ctx;
	uint8_t spi_tx[MAX_CMD_LENGTH]={0};
	uint8_t spi_rx[MAX_CMD_LENGTH]={0};
	uint16_t tx_crc = 0, rx_crc = 0;

	#if 0 //debug
		job = data_nonce;
	#endif

	#if 0
	applog(LOG_ERR, "cmd_write_job:");
	for (int i=0; i<JOB_LENGTH; i++)
	{
		printf("0x%02x,", job[i]);

		if (i%4 == 16)printf("\n");
	}
	printf("\n");
	#endif

	memcpy(spi_tx, job, JOB_LENGTH);
	ret = spi_write_data(ctx, spi_tx, JOB_LENGTH);

	spi_poll_result(ctx, job[1], chip_id, spi_rx, 4);

	rx_crc = spi_rx[3] << 8 | spi_rx[2];
	tx_crc = spi_tx[JOB_LENGTH-1] << 8 | spi_tx[JOB_LENGTH-2];

	//applog(LOG_ERR, "tx_crc=%04x, rx_crc=%04x", tx_crc, rx_crc);

	
	if(rx_crc != tx_crc)
	{
		if (likely(achain->update_pll_finish))
			achain->chips[chip_id-1].crc_error++;
		//if (unlikely(enlog))
		{
			applog(LOG_ERR, "%s chip_%d, set work crc error!", basename(achain->devname), chip_id);
			applog(LOG_ERR, "tx_crc=%04x, rx_crc=%04x", tx_crc, rx_crc);
		}
		 ret = false;
	}
	else
	{
		if (likely(achain->update_pll_finish))
			achain->chips[chip_id-1].crc_right++;
	}

	//flush_spi(ctx);
	//cgsleep_ms(1);

	return ret;
}

bool cmd_read_result(struct stu_chain *achain, uint8_t chip_id, uint8_t *res)
{
	int i,j;
	int tx_len,index,ret;		
	uint16_t clc_crc; 
	uint16_t res_crc;
	uint8_t spi_tx[24];
	uint8_t spi_rx[24];
	struct spi_ctx *ctx = achain->spi_ctx;

	memset(spi_tx, 0, sizeof(spi_tx));
	spi_tx[0] = chip_id;
	spi_tx[1] = CMD_READ_RESULT;

	//applog(LOG_ERR, "spi write:0x%02x 0x%02x", spi_tx[0], spi_tx[1]);
	if(!spi_write_data(ctx, spi_tx, 2))
	{
		applog(LOG_ERR,"spi read error ~~~~~0~~~~~~~");
		return false;
	}

	tx_len = 4 * ASIC_CHIP_NUM +8;
	memset(spi_rx, 0, sizeof(spi_rx));
	for(i = 0; i < tx_len; i += 2)
	{
		if(!spi_read_data(ctx, spi_rx, 2))		
		{
			applog(LOG_ERR,"spi read error ~~~~~1~~~~~~~");
			return false;
		}

		if((spi_rx[1]== 0x04) && (spi_rx[0] != 0))
		{
			index = 0;	
			do{
				ret = spi_read_data(ctx, spi_rx + 2 + index, 2);
				if(!ret)
				{
					return false;
				}					
				index = index + 2;
			}while(index < ASIC_RESULT_LEN); 

			//if (spi_rx[4]!= 0x00)
			{
				//applog(LOG_ERR, "get nonce :0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x", 
				//spi_rx[0], spi_rx[1], 
				//spi_rx[2], spi_rx[3], spi_rx[4], spi_rx[5], spi_rx[6], spi_rx[7], spi_rx[8], spi_rx[9]);
			}

			memcpy(res, spi_rx, READ_RESULT_LEN);
			return true;
							
		}

		//cgsleep_ms(4);
	}

	return false;

}

//len 是除header(2bytes)以外的数据长度( 包括data+crc)
//buff 数据包括header+data+crc
bool spi_poll_result(struct spi_ctx *ctx, uint8_t cmd, uint8_t chip_id, uint8_t *buff, int len)
{
	int ret1, ret2;
	int tx_len;
	int tmp_len;
	int index,ret;
	uint8_t spi_tx[MAX_CMD_LENGTH];
	uint8_t spi_rx[MAX_CMD_LENGTH];
	
	memset(spi_tx, 0, sizeof(spi_tx));
	memset(spi_rx, 0, sizeof(spi_rx));

	tx_len = ASIC_CHIP_NUM*4+10;	


	//applog(LOG_WARNING, "poll result....");
	for(tmp_len = 0; tmp_len < tx_len; tmp_len += 2)
	{
		if(!spi_read_data(ctx, spi_rx, 2))
		{
			applog(LOG_WARNING, "poll result: transfer fail !");
			return false;
		}

		applog(LOG_ERR, "h1 %02x %02x cmd=%02x", spi_rx[0], spi_rx[1],cmd);
		
		if(spi_rx[1] == cmd)
		{
			index = 0;	
			do{
				ret = spi_read_data(ctx, spi_rx + 2 + index, 2);
				if(!ret)
				{
					applog(LOG_ERR,"spi_read_data return false");
					return false;
				}				
				applog(LOG_ERR, "h2 %02x %02x ", spi_rx[2 + index], spi_rx[2 + index+1]);
				index = index + 2;
				
			}while(index < len);

			memcpy(buff, spi_rx, len+2);

			//读完寄存器数据后再发8字节空值 这样做是为了确保能读到寄存器数据 begin
			//uint8_t tmpbuf[8]={0};
			//spi_send_data(ctx,tmpbuf,8);
			//end
			return true;
		}
		cgsleep_us(2);
	}

	//if (unlikely(enlog))
		applog(LOG_ERR, "spi_poll_result error !!!");

	return false;
}

bool cmd_auto_address(struct spi_ctx *ctx, uint8_t chip_id)
{
	uint8_t spi_tx[MAX_CMD_LENGTH]={0};
	uint8_t spi_rx[MAX_CMD_LENGTH]={0};
	
	memset(spi_tx, 0, sizeof(spi_tx));

	spi_tx[0] = chip_id;
	spi_tx[1] = CMD_AUTO_ADDR;
	spi_tx[2] = 0x3a;
	spi_tx[3] = 0x5c;

	applog(LOG_ERR, "send %s: 0x%02x 0x%02x 0x%02x 0x%02x",ctx->devname, spi_tx[0], spi_tx[1], spi_tx[2],spi_tx[3]);
	
	if(spi_write_data(ctx, spi_tx, 4))
	{

	}
	else
	{
		applog(LOG_ERR, "send command fail !");
		return false;
	}

	cgsleep_ms(5);

	memset(spi_rx, 0, sizeof(spi_rx));
	if(!spi_poll_result(ctx, CMD_AUTO_ADDR, chip_id, spi_rx, 4))
	{
		return false;
	}

	applog(LOG_ERR, "auto address result %s: 0x%02x 0x%02x 0x%02x 0x%02x",ctx->devname, spi_rx[0], spi_rx[1], spi_rx[2], spi_rx[3]);

	return true;
}

bool cmd_write_register_1(struct spi_ctx *ctx, uint8_t chip_id, uint8_t DlyEn, uint16_t DlyVal)
{
	uint8_t spi_tx[MAX_CMD_LENGTH]={0};
	uint8_t spi_rx[MAX_CMD_LENGTH]={0};
	uint16_t crc;
	uint16_t rx_crc;
	
	uint8_t bankVal[8] = {0x00,0x00,0x00,0x00, 0x00, 0x00, 0x00, 0x0a};


	DlyEn &= 0x01; //valid len 1bit
	

	bankVal[5] = (bankVal[5]&0xf0)|(DlyEn);
	bankVal[6] = DlyVal>>8;
	bankVal[7] = DlyVal&0xff;
	

	spi_tx[0] = CMD_WRITE_REG;
	spi_tx[1] = chip_id;
	spi_tx[2] = 0x00;
	spi_tx[3] = 0x01;
	memcpy(spi_tx+4, bankVal, 8);

	applog(LOG_ERR, "bank1:");
	for (int i=0; i<12; i++)
		printf("%02x ", spi_tx[i]);

	swap_data(spi_tx, 12);
	crc = CRC16(spi_tx, 12);
	printf("%04x \n", crc);

	spi_tx[12] = (uint8_t)((crc >> 0) & 0xff);
	spi_tx[13] = (uint8_t)((crc >> 8) & 0xff);

	spi_send_data(ctx, spi_tx, 14);

	if (spi_tx[0] != 0x00)
	{
		spi_poll_result(ctx, spi_tx[1], chip_id, spi_rx, 2);
		applog(LOG_ERR, "chip_%d, bank1 return:%02x %02x %02x %02x",
					chip_id, spi_rx[0],spi_rx[1],spi_rx[2],spi_rx[3] );
		memcpy(&rx_crc, &spi_rx[2], 2);
		applog(LOG_ERR,"crc=%04x, rx_crc=%04x", crc,rx_crc);
		
		if (crc != rx_crc)
		{
			applog(LOG_ERR, "write chip_%d bank1 error!", chip_id);
		}
	}
	flush_spi(ctx);

	return true;
}

bool cmd_write_register_2(struct spi_ctx *ctx, uint8_t chip_id, uint8_t spdEn, uint8_t spdVid, uint8_t glbSpd)
{
	uint8_t spi_tx[MAX_CMD_LENGTH]={0};
	uint8_t spi_rx[MAX_CMD_LENGTH]={0};
	uint16_t crc;
	uint16_t rx_crc;
	
	#ifdef USE_H8
	uint8_t bankVal[16] = {0x68, 0x00, 0x00, 0x30, 0x00, 0x01, 0x00, 0x0a, 0x80, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00};
	#else
	uint8_t bankVal[16] = {0x56, 0x00, 0x00, 0x30, 0x00, 0x01, 0x00, 0x0a, 0x80, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00};
	#endif
	
	spdEn &= 0x01;
	spdVid &= 0x01;
	glbSpd &= 0x07; 
	uint8_t glbHigh = 3;
	uint8_t glbLow = 0;

	#ifdef USE_H8
	uint16_t spdThPass = 10;
	#else
	uint16_t spdThPass = 1;
	#endif
	
	uint16_t spdThFail = 80;
	uint16_t spdThInit = 32768;

	//speed dirtection(up/down) when time out, 1->up, 0->down
	uint8_t spdDirectcion = 0;
	// speed step value when time out
	uint16_t spdThms = 1024;
	// speed timer, 0-> disable
	uint32_t spdThTmout = 0;
	
	//speed enable
	bankVal[1]  = (bankVal[1] &0xfe)|spdEn;
	//spdVId, speed write protect
	bankVal[2] = (bankVal[2]&0xef)|(spdVid<<4);
	//set global speed
	bankVal[2] = (bankVal[2]&0xf0)|glbSpd;

	//global speed upper limit
	bankVal[3] = (bankVal[3]&0x0f)|(glbHigh<<4);
	//global speed lower limit
	bankVal[3] = (bankVal[3]&0xf0)|glbLow;

	//speed upstream step
	bankVal[4] = (spdThPass>>8)&0xff;
	bankVal[5] = spdThPass&0xff;
	//speed downstream step
	bankVal[6] = (spdThFail>>8)&0xff;
	bankVal[7] = spdThFail&0xff;
	//speed init value
	bankVal[8] = (spdThInit>>8)&0xff;
	bankVal[9] = spdThInit&0xff;

	//speed dirtection(up/down) when time out
	spdDirectcion &= 0x01;
	bankVal[10] = (bankVal[10]&0x7f)|(spdDirectcion<<7);
	//speed step value when time out
	bankVal[10] = (bankVal[10]&0x80)|(spdThms>>8);
	bankVal[11] = spdThms&0xff;
	// speed timer
	bankVal[12] = (spdThTmout>>24)&0xff;
	bankVal[13] = (spdThTmout>>16)&0xff;
	bankVal[14] = (spdThTmout>>8)&0xff;
	bankVal[15] = (spdThTmout>>0)&0xff;

	spi_tx[0] = CMD_WRITE_REG;
	spi_tx[1] = chip_id;
	spi_tx[2] = 0x00;
	spi_tx[3] = 0x02;
	memcpy(spi_tx+4, bankVal, 16);

	applog(LOG_ERR, "bank2:");
	for (int i=0; i<20; i++)
		printf("%02x ", spi_tx[i]);

	swap_data(spi_tx, 20);
	crc = CRC16(spi_tx, 20);
	printf("%04x \n", crc);

	spi_tx[20] = (uint8_t)((crc >> 0) & 0xff);
	spi_tx[21] = (uint8_t)((crc >> 8) & 0xff);

	spi_send_data(ctx, spi_tx, 22);


	if (spi_tx[0] != 0x00)
	{
		spi_poll_result(ctx, spi_tx[1], chip_id, spi_rx, 2);
		applog(LOG_ERR, "chip_%d, bank2 return:%02x %02x %02x %02x",
					chip_id, spi_rx[0],spi_rx[1],spi_rx[2],spi_rx[3] );

		memcpy(&rx_crc, &spi_rx[2], 2);
		applog(LOG_ERR,"crc=%04x, rx_crc=%04x", crc,rx_crc);
		
		if (crc != rx_crc)
		{
			applog(LOG_ERR, "write chip_%d bank2 error!", chip_id);
		}
	}
	flush_spi(ctx);

	return true;
}

bool cmd_write_register_3(struct spi_ctx *ctx, uint8_t chip_id, uint8_t crcVal, uint8_t RollTimeEn, uint32_t RollTimeVal)
{
	uint8_t spi_tx[MAX_CMD_LENGTH]={0};
	uint8_t spi_rx[MAX_CMD_LENGTH]={0};
	uint16_t crc;
	uint16_t rx_crc;

	#ifdef USE_H8
		uint8_t bankVal[8]={0x00, 0x0b, 0x50, 0x00, 0x00, 0x00,0x00, 0x64};
	#else //u6
		uint8_t bankVal[8]={0x00, 0x00, 0x80, 0x00, 0x00, 0x00,0x00, 0x64};
	#endif
	
	crcVal &= 0x03;
	RollTimeEn &=0x01;
	RollTimeVal &= 0x00ffffff;

	bankVal[0] = (bankVal[0]&0x0f)|(crcVal<<4);	
	bankVal[3] = (bankVal[3]&0xf0)|RollTimeEn;
	bankVal[5] = (RollTimeVal>>16);
	bankVal[6] = (RollTimeVal>>8);
	bankVal[7] = (RollTimeVal>>0);
	
	spi_tx[0] = CMD_WRITE_REG;
	spi_tx[1] = chip_id;
	spi_tx[2] = 0x00;
	spi_tx[3] = 0x03;
	memcpy(spi_tx+4, bankVal, 8);

	applog(LOG_ERR, "bank3:");
	for (int i=0; i<12; i++)
		printf("%02x ", spi_tx[i]);

	swap_data(spi_tx, 12);
	crc = CRC16(spi_tx, 12);
	printf("%04x \n", crc);

	spi_tx[12] = (uint8_t)((crc >> 0) & 0xff);
	spi_tx[13] = (uint8_t)((crc >> 8) & 0xff);

	spi_send_data(ctx, spi_tx, 14);

	//这个IF只检测私有命令
	if (spi_tx[0] != 0x00)
	{
		spi_poll_result(ctx, spi_tx[1], chip_id, spi_rx, 2);
		applog(LOG_ERR, "chip_%d, bank3 return:%02x %02x %02x %02x",
					chip_id, spi_rx[0],spi_rx[1],spi_rx[2],spi_rx[3] );
		memcpy(&rx_crc, &spi_rx[2], 2);
		applog(LOG_ERR,"crc=%04x, rx_crc=%04x", crc,rx_crc);
		
		if (crc != rx_crc)
		{
			applog(LOG_ERR, "write chip_%d bank3 error!", chip_id);
		}
	}
	flush_spi(ctx);

	return true;
}

bool cmd_enable_ft(struct spi_ctx *ctx, uint8_t chip_id, uint8_t threshold, uint32_t RollTimeVal)
{
	uint8_t spi_tx[MAX_CMD_LENGTH]={0};
	uint8_t spi_rx[MAX_CMD_LENGTH]={0};
	uint16_t crc;

	#ifdef USE_H8
		uint8_t bankVal[8]={0x00, 0x0b, 0x51, 0x10, 0x00, 0x00,0x00, 0x64};
	#else
		uint8_t bankVal[8]={0x00, 0x10, 0x61, 0x10, 0x00, 0x00,0x00, 0x64};
	#endif

	spi_tx[0] = CMD_WRITE_REG;
	spi_tx[1] = chip_id;
	spi_tx[2] = 0x00;
	spi_tx[3] = 0x03;

	//threshod
	#ifdef USE_H8
	bankVal[1] = (bankVal[1]&0xf0)|((threshold&0xf0)>>4);
	bankVal[2] = (bankVal[2]&0x0f)|((threshold&0x0f)<<4);
	#else
	bankVal[2] = (bankVal[2]&0x0f)|((threshold&0x0f)<<4);
	#endif
		
	//RollTimeEn must disable
	RollTimeVal &= 0x00ffffff;
	bankVal[5] = (RollTimeVal>>16);
	bankVal[6] = (RollTimeVal>>8);
	bankVal[7] = (RollTimeVal>>0);
	

	memcpy(spi_tx+4, bankVal, 8);

	applog(LOG_ERR, "enable ft:");
	for (int i=0; i<12; i++)
		printf("%02x ", spi_tx[i]);
	printf("\n");

	swap_data(spi_tx, 12);
	crc = CRC16(spi_tx, 12);

	spi_tx[12] = (uint8_t)((crc >> 0) & 0xff);
	spi_tx[13] = (uint8_t)((crc >> 8) & 0xff);

	spi_send_data(ctx, spi_tx, 14);

	if (spi_tx[0] != 0x00)
	{
		spi_poll_result(ctx, spi_tx[1], chip_id, spi_rx, 2);
		applog(LOG_ERR, "chip_%d, bank2 return:%02x %02x %02x %02x",
					chip_id, spi_rx[0],spi_rx[1],spi_rx[2],spi_rx[3] );
	}
	flush_spi(ctx);

	return true;
}

bool cmd_write_register_4(struct spi_ctx *ctx, uint8_t chip_id, uint32_t nonceTarget)
{
	uint8_t spi_tx[MAX_CMD_LENGTH]={0};
	uint8_t spi_rx[MAX_CMD_LENGTH]={0};
	uint16_t crc;
	uint16_t rx_crc;
	
	#ifdef USE_H8
	uint8_t bankVal[8]={0x00, 0x00, 0x00, 0x00, 0x3f, 0x03,0x01, 0x03};
	#else
	uint8_t bankVal[8]={0xff, 0xff, 0xff, 0xff, 0x00, 0x00,0x00, 0x08};
	#endif

	//diff for cores (number of zeros)
	uint8_t cfgMask = 0x3f;

	#ifdef USE_H8
	bankVal[0] = (nonceTarget>>24);
	bankVal[1] = (nonceTarget>>16);
	bankVal[2] = (nonceTarget>>8);
	bankVal[3] = (nonceTarget>>0);
	#else
	nonceTarget &=0x0000ffff;
	bankVal[2] = (nonceTarget>>8);
	bankVal[3] = (nonceTarget>>0);
	#endif
	
	bankVal[4] = cfgMask;

	spi_tx[0] = CMD_WRITE_REG;
	spi_tx[1] = chip_id;
	spi_tx[2] = 0x00;
	spi_tx[3] = 0x04;
	memcpy(spi_tx+4, bankVal, 8);

	applog(LOG_ERR, "bank4:");

	for (int i=0; i<12; i++)
		printf("%02x ", spi_tx[i]);
	

	swap_data(spi_tx, 12);
	crc = CRC16(spi_tx, 12);
	printf("%04x \n", crc);

	spi_tx[12] = (uint8_t)((crc >> 0) & 0xff);
	spi_tx[13] = (uint8_t)((crc >> 8) & 0xff);

	spi_send_data(ctx, spi_tx, 14);

	if (spi_tx[0] != 0x00)
	{
		spi_poll_result(ctx, spi_tx[1], chip_id, spi_rx, 2);
		applog(LOG_ERR, "chip_%d, bank4 return:%02x %02x %02x %02x",
					chip_id, spi_rx[0],spi_rx[1],spi_rx[2],spi_rx[3] );
		memcpy(&rx_crc, &spi_rx[2], 2);
		applog(LOG_ERR,"crc=%04x, rx_crc=%04x", crc,rx_crc);
		
		if (crc != rx_crc)
		{
			applog(LOG_ERR, "write chip_%d bank4 error!", chip_id);
		}
	}
	flush_spi(ctx);
	
	return true;
}


bool cmd_write_register_5(struct spi_ctx *ctx, uint8_t chip_id, uint8_t spi_div)
{
	uint8_t spi_tx[MAX_CMD_LENGTH]={0};
	uint8_t spi_rx[MAX_CMD_LENGTH]={0};
	uint16_t crc;
	uint16_t rx_crc;

	#ifdef USE_H8
	uint8_t bankVal[8]={0x00, 0x01, 0x17, 0xa4, 0x00, 0x00, 0x00, 0x00};
	#else
	uint8_t bankVal[8]={0x01, 0x10, 0x17, 0xb4, 0x00, 0x00, 0x00, 0x00};
	#endif

	
	spi_div &=0x0f;
	bankVal[2] = (bankVal[2]&0x0f)|(spi_div<<4);

	spi_tx[0] = CMD_WRITE_REG;
	spi_tx[1] = chip_id;
	spi_tx[2] = 0x00;
	spi_tx[3] = 0x05;
	memcpy(spi_tx+4, bankVal, 8);

	applog(LOG_ERR, "bank5:");

	for (int i=0; i<12; i++)
		printf("%02x ", spi_tx[i]);

	swap_data(spi_tx, 12);
	crc = CRC16(spi_tx, 12);
	printf("%04x \n", crc);

	spi_tx[12] = (uint8_t)((crc >> 0) & 0xff);
	spi_tx[13] = (uint8_t)((crc >> 8) & 0xff);

	spi_send_data(ctx, spi_tx, 14);

	if (spi_tx[0] != 0x00)
	{
		spi_poll_result(ctx, spi_tx[1], chip_id, spi_rx, 2);
		applog(LOG_ERR, "chip_%d, bank5 return:%02x %02x %02x %02x",
					chip_id, spi_rx[0],spi_rx[1],spi_rx[2],spi_rx[3] );
		memcpy(&rx_crc, &spi_rx[2], 2);
		applog(LOG_ERR,"crc=%04x, rx_crc=%04x", crc,rx_crc);
		
		if (crc != rx_crc)
		{
			applog(LOG_ERR, "write chip_%d bank5 error!", chip_id);
		}
	}
	flush_spi(ctx);

	return true;
}

bool cmd_write_register_6(struct spi_ctx *ctx, uint8_t chip_id, uint8_t restProtect, uint8_t cfgRest,
						uint8_t spdGo,uint8_t sycNum, uint32_t spdSetupT)	
{
	uint8_t spi_tx[MAX_CMD_LENGTH]={0};
	uint8_t spi_rx[MAX_CMD_LENGTH]={0};
	uint16_t crc;
	uint16_t rx_crc;
	#ifdef USE_H8
	uint8_t bankVal[8]={0x00, 0x00, 0x00, 0xb5, 0x00, 0x00,0x00, 0x08};
	#else
	uint8_t bankVal[8]={0x00, 0x00, 0x00, 0x07, 0x10, 0x00, 0x00, 0x08};
	#endif

	
	restProtect &= 0x01;
	cfgRest &= 0x01;
	spdGo &= 0x01;
	spdSetupT &= 0x00ffffff;

	bankVal[0] = restProtect||(restProtect<<4);
	bankVal[2] = (bankVal[2]&0x0f)|(spdGo<<4);

	#ifdef USE_H8
	bankVal[3] = sycNum;
	#else
	bankVal[3] = sycNum&0x07;
	#endif
	bankVal[4] = 0; //reserve
	bankVal[5] = (spdSetupT>>16)&0xff;
	bankVal[6] = (spdSetupT>>8)&0xff;
	bankVal[7] = (spdSetupT>>0)&0xff;
	
	spi_tx[0] = CMD_WRITE_REG;
	spi_tx[1] = chip_id;
	spi_tx[2] = 0x00;
	spi_tx[3] = 0x06;
	memcpy(spi_tx+4, bankVal, 8);

	applog(LOG_ERR, "bank6:");
	for (int i=0; i<12; i++)
		printf("%02x ", spi_tx[i]);

	swap_data(spi_tx, 12);
	crc = CRC16(spi_tx, 12);
	printf("%04x \n", crc);

	spi_tx[12] = (uint8_t)((crc >> 0) & 0xff);
	spi_tx[13] = (uint8_t)((crc >> 8) & 0xff);

	spi_send_data(ctx, spi_tx, 14);

	if (spi_tx[0] != 0x00)
	{
		spi_poll_result(ctx, spi_tx[1], chip_id, spi_rx, 2);
		applog(LOG_ERR, "chip_%d, bank6 return:%02x %02x %02x %02x",
					chip_id, spi_rx[0],spi_rx[1],spi_rx[2],spi_rx[3] );
		memcpy(&rx_crc, &spi_rx[2], 2);
		applog(LOG_ERR,"crc=%04x, rx_crc=%04x", crc,rx_crc);
		
		if (crc != rx_crc)
		{
			applog(LOG_ERR, "write chip_%d bank6 error!", chip_id);
		}
	}
	flush_spi(ctx);

	return true;
}

// en_cores 1~6
//close all core
bool cmd_write_register_7(struct spi_ctx *ctx, uint8_t chip_id, uint8_t en_cores)
{
	uint8_t spi_tx[128]={0};
	uint8_t spi_rx[128]={0};
	uint16_t crc;
	uint16_t rx_crc;
	uint8_t bankVal[92]={0};

	
	spi_tx[0] = CMD_WRITE_REG;
	spi_tx[1] = chip_id;
	spi_tx[2] = 0x00;
	spi_tx[3] = 0x07;

	memcpy(spi_tx+4, bankVal, 92);
	
	applog(LOG_ERR, "bank7:");
	for (int i=0; i<96; i++)
		printf("%02x ", spi_tx[i]);
	
	swap_data(spi_tx, 96);
	crc = CRC16(spi_tx, 96);
	printf("%04x \n", crc);

	spi_tx[96] = (uint8_t)((crc >> 0) & 0xff);
	spi_tx[97] = (uint8_t)((crc >> 8) & 0xff);

	spi_send_data(ctx, spi_tx, 98);

	if (spi_tx[0] != 0x00)
	{
		spi_poll_result(ctx, spi_tx[1], chip_id, spi_rx, 2);
		applog(LOG_ERR, "chip_%d, bank7 return:%02x %02x %02x %02x",
					chip_id, spi_rx[0],spi_rx[1],spi_rx[2],spi_rx[3] );
		memcpy(&rx_crc, &spi_rx[2], 2);
		applog(LOG_ERR,"crc=%04x, rx_crc=%04x", crc,rx_crc);
		
		if (crc != rx_crc)
		{
			applog(LOG_ERR, "write chip_%d bank7 error!", chip_id);
		}
	}
	flush_spi(ctx);

	return true;
}

bool cmd_write_register_8(struct spi_ctx *ctx, uint8_t chip_id, uint32_t initNonce)
{
	uint8_t spi_tx[MAX_CMD_LENGTH]={0};
	uint8_t spi_rx[MAX_CMD_LENGTH]={0};
	uint16_t crc;
	uint16_t rx_crc;
	uint8_t bankVal[4]={0};

	bankVal[0] = (initNonce>>24);
	bankVal[1] = (initNonce>>16);
	bankVal[2] = (initNonce>>8);
	bankVal[3] = (initNonce>>0);

	spi_tx[0] = CMD_WRITE_REG;
	spi_tx[1] = chip_id;
	spi_tx[2] = 0x00;
	spi_tx[3] = 0x08;
	
	memcpy(spi_tx+4, initNonce, 4);

	for (int i=0; i<8; i++)
		printf("%02x ", spi_tx[i]);
	printf("\n");
	swap_data(spi_tx, 8);
	crc = CRC16(spi_tx, 8);

	spi_tx[8] = (uint8_t)((crc >> 0) & 0xff);
	spi_tx[9] = (uint8_t)((crc >> 8) & 0xff);

	spi_send_data(ctx, spi_tx, 10);
	
	if (spi_tx[0] != 0x00)
	{
		spi_poll_result(ctx, spi_tx[1], chip_id, spi_rx, 2);
		applog(LOG_ERR, "chip_%d, bank8 return:%02x %02x %02x %02x",
					chip_id, spi_rx[0],spi_rx[1],spi_rx[2],spi_rx[3] );

		memcpy(&rx_crc, &spi_rx[2], 2);
		applog(LOG_ERR,"crc=%04x, rx_crc=%04x", crc,rx_crc);
		
		if (crc != rx_crc)
		{
			applog(LOG_ERR, "write chip_%d bank8 error!", chip_id);
		}
	}
	flush_spi(ctx);
}

bool cmd_write_register_14(struct spi_ctx *ctx, uint8_t chip_id)
{
	uint8_t spi_tx[MAX_CMD_LENGTH]={0};
	uint8_t spi_rx[MAX_CMD_LENGTH]={0};
	uint16_t crc;
	uint16_t rx_crc;
	uint8_t bankVal[8]={0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	spi_tx[0] = CMD_WRITE_REG;
	spi_tx[1] = chip_id;
	spi_tx[2] = 0x00;
	spi_tx[3] = 0x0e;
	memcpy(spi_tx+4, bankVal, 8);

	applog(LOG_ERR, "bank14:");

	for (int i=0; i<12; i++)
		printf("%02x ", spi_tx[i]);

	swap_data(spi_tx, 12);
	crc = CRC16(spi_tx, 12);
	printf("%04x \n", crc);

	spi_tx[12] = (uint8_t)((crc >> 0) & 0xff);
	spi_tx[13] = (uint8_t)((crc >> 8) & 0xff);

	spi_send_data(ctx, spi_tx, 14);

	if (spi_tx[0] != 0x00)
	{
		spi_poll_result(ctx, spi_tx[1], chip_id, spi_rx, 2);
		applog(LOG_ERR, "chip_%d, bank14 return:%02x %02x %02x %02x",
					chip_id, spi_rx[0],spi_rx[1],spi_rx[2],spi_rx[3] );
		memcpy(&rx_crc, &spi_rx[2], 2);
		applog(LOG_ERR,"crc=%04x, rx_crc=%04x", crc,rx_crc);
		
		if (crc != rx_crc)
		{
			applog(LOG_ERR, "write chip_%d bank5 error!", chip_id);
		}
	}
	flush_spi(ctx);

	return true;
}

// step: 1~6
bool cmd_switch_pll0_to_pll1(struct spi_ctx *ctx, uint8_t chip_id, uint8_t step)
{
	uint8_t spi_tx[MAX_CMD_LENGTH]={0};
	uint8_t spi_rx[MAX_CMD_LENGTH]={0};
	uint16_t crc;
	uint16_t rx_crc;
	uint8_t bankVal[4]={0};
	uint32_t coreVal = 0x00000000;

	
	switch  (step)//051423
	{
		case 1:
			coreVal = 0x00010000;
			break;
		case 2:
			coreVal = 0x00010010;
			break;
		case 3:
			coreVal = 0x00110010;
			break;
		case 4:
			coreVal = 0x00110011;
			break;
		case 5:
			coreVal = 0x01110011;
			break;
		case 6:
			coreVal = 0x11110011;
			break;
		default:
			applog(LOG_ERR, "invalid reg7 value");
			return false;
	}

	
	coreVal = swab32(coreVal);
	
	memcpy(bankVal, &coreVal, 4);

	spi_tx[0] = CMD_WRITE_REG;
	spi_tx[1] = chip_id;
	spi_tx[2] = 0x00;
	spi_tx[3] = 0x07;

	memcpy(spi_tx+4, bankVal, 4);
	
	applog(LOG_ERR, "bank7:");
	for (int i=0; i<8; i++)
		printf("%02x ", spi_tx[i]);
	
	swap_data(spi_tx, 8);
	crc = CRC16(spi_tx, 8);
	printf("%04x \n", crc);

	spi_tx[8] = (uint8_t)((crc >> 0) & 0xff);
	spi_tx[9] = (uint8_t)((crc >> 8) & 0xff);

	spi_send_data(ctx, spi_tx, 10);

	if (spi_tx[0] != 0x00)
	{
		spi_poll_result(ctx, spi_tx[1], chip_id, spi_rx, 2);
		applog(LOG_ERR, "chip_%d, bank7 return:%02x %02x %02x %02x",
					chip_id, spi_rx[0],spi_rx[1],spi_rx[2],spi_rx[3] );
		memcpy(&rx_crc, &spi_rx[2], 2);
		applog(LOG_ERR,"crc=%04x, rx_crc=%04x", crc,rx_crc);
		
		if (crc != rx_crc)
		{
			applog(LOG_ERR, "write chip_%d bank7 error!", chip_id);
		}
	}
	flush_spi(ctx);

	return true;
}

// step: 1~6
bool cmd_switch_pll1_to_pll0(struct spi_ctx *ctx, uint8_t chip_id, uint8_t step)
{
	uint8_t spi_tx[MAX_CMD_LENGTH]={0};
	uint8_t spi_rx[MAX_CMD_LENGTH]={0};
	uint16_t crc;
	uint16_t rx_crc;
	uint8_t bankVal[4]={0};
	uint32_t coreVal = 0x11111111;
	
	switch  (step)//051423
	{
		case 1:
			coreVal = 0x11101111;
			break;
		case 2:
			coreVal = 0x11101101;
			break;
		case 3:
			coreVal = 0x11001101;
			break;
		case 4:
			coreVal = 0x11001100;
			break;
		case 5:
			coreVal = 0x10001100;
			break;
		case 6:
			coreVal = 0x00001100;
			break;
		default:
			applog(LOG_ERR, "invalid reg7 value");
			return false;
	}

	
	coreVal = swab32(coreVal);
	
	memcpy(bankVal, &coreVal, 4);

	spi_tx[0] = CMD_WRITE_REG;
	spi_tx[1] = chip_id;
	spi_tx[2] = 0x00;
	spi_tx[3] = 0x07;

	memcpy(spi_tx+4, bankVal, 4);
	
	applog(LOG_ERR, "bank7:");
	for (int i=0; i<8; i++)
		printf("%02x ", spi_tx[i]);
	
	swap_data(spi_tx, 8);
	crc = CRC16(spi_tx, 8);
	printf("%04x \n", crc);

	spi_tx[8] = (uint8_t)((crc >> 0) & 0xff);
	spi_tx[9] = (uint8_t)((crc >> 8) & 0xff);

	spi_send_data(ctx, spi_tx, 10);

	if (spi_tx[0] != 0x00)
	{
		spi_poll_result(ctx, spi_tx[1], chip_id, spi_rx, 2);
		applog(LOG_ERR, "chip_%d, bank7 return:%02x %02x %02x %02x",
					chip_id, spi_rx[0],spi_rx[1],spi_rx[2],spi_rx[3] );
		memcpy(&rx_crc, &spi_rx[2], 2);
		applog(LOG_ERR,"crc=%04x, rx_crc=%04x", crc,rx_crc);
		
		if (crc != rx_crc)
		{
			applog(LOG_ERR, "write chip_%d bank7 error!", chip_id);
		}
	}
	flush_spi(ctx);

	return true;
}

bool disable_core(struct spi_ctx *ctx, uint8_t chip_id, uint8_t cores_num)
{
	uint8_t spi_tx[MAX_CMD_LENGTH]={0};
	uint8_t spi_rx[MAX_CMD_LENGTH]={0};
	uint16_t crc;
	uint16_t rx_crc;
	uint8_t bankVal[4]={0};
	uint32_t coreVal = 0x00;

	/**********************disable core****************************/
		switch  (cores_num)
		{
			case 1:
				coreVal = 0x40000000;
				break;
			case 2:
				coreVal = 0x04000000;
				break;
			case 3:
				coreVal = 0x00400000;
				break;
			case 4:
				coreVal = 0x00040000;
				break;
			case 5:
				coreVal = 0x00000040;
				break;
			case 6:
				coreVal = 0x00000004;
				break;
		}

	
	coreVal = swab32(coreVal);
	
	memcpy(bankVal, &coreVal, 4);

	spi_tx[0] = CMD_WRITE_REG;
	spi_tx[1] = chip_id;
	spi_tx[2] = 0x00;
	spi_tx[3] = 0x07;

	memcpy(spi_tx+4, bankVal, 4);
	
	applog(LOG_ERR, "bank7:");
	for (int i=0; i<8; i++)
		printf("%02x ", spi_tx[i]);
	
	swap_data(spi_tx, 8);
	crc = CRC16(spi_tx, 8);
	printf("%04x \n", crc);

	spi_tx[8] = (uint8_t)((crc >> 0) & 0xff);
	spi_tx[9] = (uint8_t)((crc >> 8) & 0xff);

	spi_send_data(ctx, spi_tx, 10);

	if (spi_tx[0] != 0x00)
	{
		spi_poll_result(ctx, spi_tx[1], chip_id, spi_rx, 2);
		applog(LOG_ERR, "chip_%d, bank7 return:%02x %02x %02x %02x",
					chip_id, spi_rx[0],spi_rx[1],spi_rx[2],spi_rx[3] );
		memcpy(&rx_crc, &spi_rx[2], 2);
		applog(LOG_ERR,"crc=%04x, rx_crc=%04x", crc,rx_crc);
		
		if (crc != rx_crc)
		{
			applog(LOG_ERR, "write chip_%d bank7 error!", chip_id);
		}
	}
	flush_spi(ctx);
}

// en_cores 1~6
bool cmd_core_reboot(struct spi_ctx *ctx, uint8_t chip_id, uint8_t cores_num)
{
	uint8_t spi_tx[MAX_CMD_LENGTH]={0};
	uint8_t spi_rx[MAX_CMD_LENGTH]={0};
	uint16_t crc;
	uint16_t rx_crc;
	uint8_t bankVal[4]={0};
	uint32_t coreVal = 0x00;

	/**********************disable core****************************/
		switch  (cores_num)
		{
			case 1:
				coreVal = 0x40000000;
				break;
			case 2:
				coreVal = 0x04000000;
				break;
			case 3:
				coreVal = 0x00400000;
				break;
			case 4:
				coreVal = 0x00040000;
				break;
			case 5:
				coreVal = 0x00000040;
				break;
			case 6:
				coreVal = 0x00000004;
				break;
		}

	
	coreVal = swab32(coreVal);
	
	memcpy(bankVal, &coreVal, 4);

	spi_tx[0] = CMD_WRITE_REG;
	spi_tx[1] = chip_id;
	spi_tx[2] = 0x00;
	spi_tx[3] = 0x07;

	memcpy(spi_tx+4, bankVal, 4);
	
	applog(LOG_ERR, "bank7:");
	for (int i=0; i<8; i++)
		printf("%02x ", spi_tx[i]);
	
	swap_data(spi_tx, 8);
	crc = CRC16(spi_tx, 8);
	printf("%04x \n", crc);

	spi_tx[8] = (uint8_t)((crc >> 0) & 0xff);
	spi_tx[9] = (uint8_t)((crc >> 8) & 0xff);

	spi_send_data(ctx, spi_tx, 10);

	if (spi_tx[0] != 0x00)
	{
		spi_poll_result(ctx, spi_tx[1], chip_id, spi_rx, 2);
		applog(LOG_ERR, "chip_%d, bank7 return:%02x %02x %02x %02x",
					chip_id, spi_rx[0],spi_rx[1],spi_rx[2],spi_rx[3] );
		memcpy(&rx_crc, &spi_rx[2], 2);
		applog(LOG_ERR,"crc=%04x, rx_crc=%04x", crc,rx_crc);
		
		if (crc != rx_crc)
		{
			applog(LOG_ERR, "write chip_%d bank7 error!", chip_id);
		}
	}
	flush_spi(ctx);

	cmd_write_register_6(ctx, ADDR_BROADCAST, 0,1,
									1, 5,  100);
	
	/***********************enable core***************************/
	cgsleep_ms(1000);
	coreVal = 0x00;
	coreVal = swab32(coreVal);
	
	memcpy(bankVal, &coreVal, 4);

	spi_tx[0] = CMD_WRITE_REG;
	spi_tx[1] = chip_id;
	spi_tx[2] = 0x00;
	spi_tx[3] = 0x07;

	memcpy(spi_tx+4, bankVal, 4);
	
	applog(LOG_ERR, "bank7:");
	for (int i=0; i<8; i++)
		printf("%02x ", spi_tx[i]);
	
	swap_data(spi_tx, 8);
	crc = CRC16(spi_tx, 8);
	printf("%04x \n", crc);

	spi_tx[8] = (uint8_t)((crc >> 0) & 0xff);
	spi_tx[9] = (uint8_t)((crc >> 8) & 0xff);

	spi_send_data(ctx, spi_tx, 10);

	if (spi_tx[0] != 0x00)
	{
		spi_poll_result(ctx, spi_tx[1], chip_id, spi_rx, 2);
		applog(LOG_ERR, "chip_%d, bank7 return:%02x %02x %02x %02x",
					chip_id, spi_rx[0],spi_rx[1],spi_rx[2],spi_rx[3] );
		memcpy(&rx_crc, &spi_rx[2], 2);
		applog(LOG_ERR,"crc=%04x, rx_crc=%04x", crc,rx_crc);
		
		if (crc != rx_crc)
		{
			applog(LOG_ERR, "write chip_%d bank7 error!", chip_id);
		}
	}
	flush_spi(ctx);

	return true;
}



/*
regName  len(bytes, len of valid value)
reg0		8
reg1		16
reg2		16
reg3		8
reg4		8
reg5		8
reg6		8
reg7		64
*/
bool cmd_read_register(struct spi_ctx *ctx, uint8_t chip_id, uint8_t regAddr, uint8_t regDataLen, uint8_t *buffer)
{
	uint16_t crc;
	uint16_t rx_crc;
	uint8_t spi_tx[MAX_CMD_LENGTH]={0};
	uint8_t spi_rx[MAX_CMD_LENGTH]={0};

	spi_tx[0] = chip_id;
	spi_tx[1] = CMD_READ_REG;
	spi_tx[2] = regAddr;
	spi_tx[3] = 0x00;

	applog(LOG_ERR,"read register %d %s chip_%d:\n",regAddr, basename(ctx->devname),chip_id);
	
	if(spi_send_data(ctx, spi_tx, 4))
	{

	}
	else
	{
		applog(LOG_ERR, "cmd_read_register send command fail !");
		return false;
	}

	memset(spi_rx, 0, sizeof(spi_rx));
	if(!spi_poll_result(ctx, CMD_READ_REG, chip_id, spi_rx, regDataLen+4))
	{
		return false;
	}

	crc = CRC16(spi_rx, regDataLen+4);

	swap_data(spi_rx, regDataLen+6);
	for (int i=0; i<(regDataLen+6); i++)
	{
		if(i%4 == 0)
			printf("\n");

		printf("0x%02x ", spi_rx[i]);
	}
	printf("\n");
	
	rx_crc = spi_rx[(regDataLen+6)-2] << 8 | spi_rx[(regDataLen+6)-1];
	cgsleep_ms(1);
	flush_spi(ctx);

	if(crc != rx_crc)
	{
		applog(LOG_ERR, "read register chipid=%d,regid=%d error rx_crc=%04x, cal_crc=%04x", chip_id,regAddr,rx_crc, crc);
		return false;
	}

	if (NULL != buffer)
	{
		memcpy(buffer, spi_rx+4, regDataLen);
	}

	return true;
}

bool cmd_detect_chipx(struct spi_ctx *ctx, uint8_t chip_id)
{
	bool ret;
	ret = cmd_read_register(ctx, chip_id, 3, 8, NULL);

	return ret;
}

bool write_register_2_quick_spd(struct spi_ctx *ctx, uint8_t chip_id, uint8_t spdEn, uint8_t spdVid, uint8_t glbSpd)
{
	uint8_t spi_tx[MAX_CMD_LENGTH]={0};
	uint8_t spi_rx[MAX_CMD_LENGTH]={0};
	uint16_t crc;
	uint8_t bankVal[16] = {0x38, 0x00, 0x00, 0x30, 0x00, 0x01, 0x00, 0x0a, 0x80, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00};

	spdEn &= 0x01;
	spdVid &= 0x01;
	glbSpd &= 0x03; 
	uint8_t glbHigh = 3;
	uint8_t glbLow = 0;

	uint16_t spdThPass = 60;
	uint16_t spdThFail = 80;
	uint16_t spdThInit = 32768;

	//speed dirtection(up/down) when time out, 1->up, 0->down
	uint8_t spdDirectcion = 0;
	// speed value when time out
	uint16_t spdTOutVal = 1024;
	// speed timer, 0-> disable
	uint32_t spdThTmout = 0;
	

	spi_tx[0] = CMD_WRITE_REG;
	spi_tx[1] = chip_id;
	spi_tx[2] = 0x00;
	spi_tx[3] = 0x02;

	//speed enable
	bankVal[1]  = (bankVal[1] &0xfe)|spdEn;
	//spdVId, speed write protect
	bankVal[2] = (bankVal[2]&0xef)|(spdVid<<4);
	//set global speed
	bankVal[2] = (bankVal[2]&0xf0)|glbSpd;

	//global speed upper limit
	bankVal[3] = (bankVal[3]&0x0f)|(glbHigh<<4);
	//global speed lower limit
	bankVal[3] = (bankVal[3]&0xf0)|glbLow;

	//speed upstream step
	bankVal[4] = (spdThPass>>8)&0xff;
	bankVal[5] = spdThPass&0xff;
	//speed downstream step
	bankVal[6] = (spdThFail>>8)&0xff;
	bankVal[7] = spdThFail&0xff;
	//speed init value
	bankVal[8] = (spdThInit>>8)&0xff;
	bankVal[9] = spdThInit&0xff;

	//speed dirtection(up/down) when time out
	spdDirectcion &= 0x01;
	bankVal[10] = (bankVal[10]&0x7f)|(spdDirectcion<<7);
	//speed value when time out
	bankVal[10] = (bankVal[10]&0x80)|(spdTOutVal>>8);
	bankVal[11] = spdTOutVal&0xff;
	// speed timer
	bankVal[12] = (spdThTmout>>24)&0xff;
	bankVal[13] = (spdThTmout>>16)&0xff;
	bankVal[14] = (spdThTmout>>8)&0xff;
	bankVal[15] = (spdThTmout>>0)&0xff;

	memcpy(spi_tx+4, bankVal, 16);

	applog(LOG_ERR, "bank2:");
	for (int i=0; i<20; i++)
		printf("%02x ", spi_tx[i]);

	swap_data(spi_tx, 20);
	crc = CRC16(spi_tx, 20);
	printf("%04x \n", crc);

	spi_tx[20] = (uint8_t)((crc >> 0) & 0xff);
	spi_tx[21] = (uint8_t)((crc >> 8) & 0xff);

	spi_send_data(ctx, spi_tx, 22);


	if (spi_tx[0] != 0x00)
	{
		spi_poll_result(ctx, spi_tx[1], chip_id, spi_rx, 2);
		applog(LOG_ERR, "chip_%d, bank2 return:%02x %02x %02x %02x",
					chip_id, spi_rx[0],spi_rx[1],spi_rx[2],spi_rx[3] );
	}
	flush_spi(ctx);
	
}

/*
uint8_t job1[]={0x01,0x33,0x11,0x00,0xa1,0x8f,0xfb,0x33,0x66,0x02,0x8f,0xac,0x17,0xba,0x09,
0x99,0xe5,0x6b,0xd3,0x48,0x46,0x04,0x18,0x85,0xf7,0xb2,0xd4,0x24,0x26,0x12,
0xe9,0x46,0x14,0x2c,0x73,0x0a,0x54,0x8f,0xfb,0x33,0x66,0x02,0x8f,0xac,0x17,
0xba,0x09,0x99,0xe5,0x6b,0xd3,0x48,0x46,0x04,0x18,0x85,0xf7,0xb2,0xd4,0x24,
0x26,0x12,0xe9,0x46,0x14,0x2c,0x73,0x0a,0xf0,0xf1,0xca,0xf3,0x5c,0x92,0xf3,
0x34,0x17,0x2e,0x61,0x17,0x8f,0x4e,0x2a,0x03,0x8d,0x19};
uint8_t job2[]={0x02,0x33,0x11,0x00,0xa1,0x8f,0xfb,0x33,0x66,0x02,0x8f,0xac,0x17,0xba,0x09,
0x99,0xe5,0x6b,0xd3,0x48,0x46,0x04,0x18,0x85,0xf7,0xb2,0xd4,0x24,0x26,0x12,
0xe9,0x46,0x14,0x2c,0x73,0x0a,0x54,0x8f,0xfb,0x33,0x66,0x02,0x8f,0xac,0x17,
0xba,0x09,0x99,0xe5,0x6b,0xd3,0x48,0x46,0x04,0x18,0x85,0xf7,0xb2,0xd4,0x24,
0x26,0x12,0xe9,0x46,0x14,0x2c,0x73,0x0a,0xf0,0xf1,0xca,0xf3,0x5c,0x92,0xf3,
0x34,0x17,0x2e,0x61,0x17,0x8f,0x4e,0x2a,0x03,0x0f,0x2a};

void ft_test(struct stu_chain *achain)
{
	struct spi_ctx *ctx  = achain->spi_ctx;
	flush_spi(ctx);
	cgsleep_ms(50);

	cmd_set_pll3(ctx, ADDR_BROADCAST, 256);
	cmd_set_pll2(ctx, ADDR_BROADCAST, 200);
	cmd_set_pll1(ctx, ADDR_BROADCAST, 150);
	cmd_set_pll0(ctx, ADDR_BROADCAST, 100);
	cmd_write_register_2(ctx, ADDR_BROADCAST, 1, 1, 0);
	cmd_write_register_6(ctx, ADDR_BROADCAST, 0,0,
								1, 2,  0);
	cmd_write_register_4(ctx, ADDR_BROADCAST, achain->current_HTarget6);

	cmd_write_job(achain, 1, job1);
	cmd_write_job(achain, 2, job2);
	flush_spi(ctx);
	cgsleep_ms(500);
	
	cmd_enable_ft(ctx, 1, 2);

	cgsleep_ms(50);
	cmd_close_gate_pll0(ctx, ADDR_BROADCAST, 100);
	cmd_close_gate_pll1(ctx, ADDR_BROADCAST, 150);
	cmd_close_gate_pll2(ctx, ADDR_BROADCAST, 200);
	cmd_close_gate_pll3(ctx, ADDR_BROADCAST, 256);

	cmd_read_register(ctx,  1, 9, 12, NULL);
}
*/

void close_core_by_step(struct stu_chain *chain)
{

	for (int i=TOTAL_CORE-1; i>=0; i--)
	{
		#ifdef USE_H8
			if(0 != (i%30)) // 181个core分6次关闭
				continue;
		#endif
		
		cmd_write_register_6(chain->spi_ctx, ADDR_BROADCAST, 0,1,
									1, i,  100);
		cgsleep_ms(200);
	}
}

void shut_down_all_devices(void)
{	
	int i;
	struct cgpu_info *cgpu;
	struct stu_chain *chain;


	applog(LOG_NOTICE, "shuting down all devices!!!");
	
	for (i = 0; i<total_devices; i++)
	{
		cgpu = devices[i];
		cgpu->shutdown = true;
		chain = cgpu->device_data;	
		close_core_by_step(chain);
	}

	/*retry shutdown reset,powe,led*/
	power_on_off(0);
	for (i = 0; i < ASIC_CHAIN_NUM; i++)
	{
		stu_board_set_reset_low(i);
		Set_Led_OnOf(i, LED_DISABLE);
	}

	stu_board_fan_pwm_set(100.0 , 100.0);
	//exit(0);
}

void config_hash_board(struct stu_chain *achain)
{
	struct spi_ctx *ctx  = achain->spi_ctx;
	achain->current_HTarget6 = 0xffffffff; //init diff
	int i, j;

	applog(LOG_ERR, "config_hash_board");
	
	cmd_set_pll0(ctx, ADDR_BROADCAST, pllstart);
	cmd_set_pll1(ctx, ADDR_BROADCAST, pll1);
	cmd_set_pll2(ctx, ADDR_BROADCAST, pll2);
	cmd_set_pll3(ctx, ADDR_BROADCAST, pll3);
	
	cmd_write_register_3(ctx, ADDR_BROADCAST, 0, 1, 600000);

	cmd_write_register_2(ctx, ADDR_BROADCAST, 0, 1, 0);
	cmd_write_register_4(ctx, ADDR_BROADCAST, achain->current_HTarget6);
	
	#if 1 //debug
	cmd_write_register_14(ctx, 1);
	applog(LOG_ERR, "read pll registers:");
	cmd_read_register(ctx, 1,14, 8, NULL);cgsleep_ms(4);
	#endif

	

	//enable cores
	//for (int i=0; i<TOTAL_CORE; i++)
	{
		printf("%s enable core %d\n", basename(ctx->devname),  i);
		cmd_write_register_6(ctx, ADDR_BROADCAST, 0,1,
								1, 1,  0);
		usleep(10);
		flush_spi(ctx);
		cgsleep_ms(20);	
	}

	for (int i=0; i<ASIC_CHIP_NUM; i++)
	{
		achain->chips[i].pll[0] = pll0;
		achain->chips[i].pll[1] = pll1;
		achain->chips[i].pll[2] = pll2;
		achain->chips[i].pll[3] = pll3;
	}
	achain->newpll = pllstart;
	achain->finalpll = pll0;

	cgtime(&achain->last_update_pll_t);
	cgtime(&achain->last_get_nonce_t);

	applog(LOG_ERR, "config_hash_board finish");
}

void reconfig_hash_board(struct stu_chain *achain)
{
	cmd_auto_address(achain->spi_ctx, ADDR_BROADCAST);
	cgsleep_ms(2);
	cmd_write_register_4(achain->spi_ctx, ADDR_BROADCAST, achain->current_HTarget6);
}

void reset_hash_board(struct stu_chain *achain)
{
	achain->reboot_cnt++;
	close_core_by_step(achain);
	stu_board_set_reset_low(achain->chain_id);
	cgsleep_ms(2);
	stu_board_hw_reset(achain->chain_id);

	cmd_auto_address(achain->spi_ctx, ADDR_BROADCAST);

	config_hash_board(achain);
}


/*****************************************************/

int SPI_PIN_POWER_EN[4] = {
117,
118,
123,
124,
};

int SPI_PIN_RESET[4] = {
1,
26,
85,
19,
};

void asic_gpio_init(int gpio, int direction)
{
	int fd;
	char fvalue[64];
	char fpath[64];

	fd = open(SYSFS_GPIO_EXPORT, O_WRONLY);
	if(fd == -1)
	{
		return;
	}
	memset(fvalue, 0, sizeof(fvalue));
	sprintf(fvalue, "%d", gpio);
	write(fd, fvalue, strlen(fvalue));
	close(fd);

	memset(fpath, 0, sizeof(fpath));
	sprintf(fpath, SYSFS_GPIO_DIR_STR, gpio);	
	fd = open(fpath, O_WRONLY);
	if(fd == -1)
	{
		return;
	}
	if(direction == 0)
	{
		write(fd, SYSFS_GPIO_DIR_OUT, sizeof(SYSFS_GPIO_DIR_OUT));
	}
	else
	{
		write(fd, SYSFS_GPIO_DIR_IN, sizeof(SYSFS_GPIO_DIR_IN));
	}	
	close(fd);
}

void asic_gpio_write(int gpio, int value)
{
	int fd;
	char fvalue[64];
	char fpath[64];

	memset(fpath, 0, sizeof(fpath));
	sprintf(fpath, SYSFS_GPIO_VAL_STR, gpio);
	fd = open(fpath, O_WRONLY);
	if(fd == -1)
	{
		return;
	}

	if(value == 0)
	{
		write(fd, SYSFS_GPIO_VAL_LOW, sizeof(SYSFS_GPIO_VAL_LOW));
	}
	else
	{
		write(fd, SYSFS_GPIO_VAL_HIGH, sizeof(SYSFS_GPIO_VAL_HIGH));
	}	
	close(fd);	
}

int asic_gpio_read(int gpio)
{
	int fd;
	char fvalue[64];
	char fpath[64];

	memset(fpath, 0, sizeof(fpath));
	sprintf(fpath, SYSFS_GPIO_VAL_STR, gpio);
	fd = open(fpath, O_RDONLY);
	if(fd == -1)
	{
		return -1;
	}
	memset(fvalue, 0, sizeof(fvalue));
	read(fd, fvalue, 1);
	close(fd);
	if(fvalue[0] == '0')
	{
		return 0;
	}
	else
	{
		return 1;
	}	
} 

bool stu_gpio_init(int chain_id)
{
	asic_gpio_init(SPI_PIN_POWER_EN[chain_id], 0); //0 --> ouput
	asic_gpio_init(SPI_PIN_RESET[chain_id], 0);
}


bool stu_board_hw_reset(int chain_id)
{
	asic_gpio_write(SPI_PIN_RESET[chain_id], 0);
	cgsleep_ms(500);
	asic_gpio_write(SPI_PIN_RESET[chain_id], 1);
	cgsleep_ms(500);
	asic_gpio_write(SPI_PIN_RESET[chain_id], 0);
	cgsleep_ms(500);
}

void stu_board_set_reset_low(int chain_id)
{
	asic_gpio_write(SPI_PIN_RESET[chain_id], 1);
}

int Write_Sys_file(unsigned char num, char *path, unsigned char *value)
{
			int fd;
			char fpath[64];
			int ret, i, len;
	
	
			memset(fpath, 0, sizeof(fpath));
			sprintf(fpath, path, num);
			fd = open(fpath, O_WRONLY);
			if(fd == -1)
			{
				return -1;
			}
			len = strlen(value);

			for(i = 0; i < 5; i++)
			{
				ret = write(fd, value, len);
				if(ret >= 0)
				{
					printf("write sys file %d\n", ret);
					break;
				}
				printf("write sys file fail \n");
			}

			close(fd);
			return ret;
}

bool stu_dev_init(int chain_id)
{
	cgsleep_ms(500);
	stu_gpio_init(chain_id);
	cgsleep_ms(50);
	
	stu_board_hw_reset(chain_id);

	return true;
}

int Get_fan_fre(unsigned char num)
{
	int olds, news;
    int total;
    int speed, freq;
    struct timeval last;
    struct timeval  new;
    float time;

    total = 0;
   // speed = 0;
    freq = 0;
    asic_gpio_init(num, 1);

    unsigned long i = 0;
    int fdr;


    unsigned long data = 0;
    fdr = open ("/dev/rtc", O_RDONLY);
    if(fdr < 0)
    {
            perror("open");
            exit(errno);
    }

    if(ioctl(fdr, RTC_IRQP_SET,  2000) < 0)
    {
            perror("ioctl(RTC_IRQP_SET)");
            close(fdr);
            exit(errno);
    }
    /* Enable periodic interrupts */
    if(ioctl(fdr, RTC_PIE_ON, 0) < 0)
    {
            perror("ioctl(RTC_PIE_ON)");
            close(fdr);
            exit(errno);
    }

    olds = asic_gpio_read(num);
    gettimeofday(&last, NULL);
    for(i = 0; i < 3000; i++)
    {
             if(read(fdr, &data, sizeof(unsigned long)) < 0)
            {
                    perror("read");
                    close(fdr);
                    exit(errno);

            }
            gettimeofday(&new, NULL);
            time = (new.tv_sec - last.tv_sec) * 1000000 + (new.tv_usec-last.tv_usec);
            if(time > 1000000)
                break;

            news = asic_gpio_read(num);
            if(news != olds)
            {
                    total++;
                    olds = news;
            }
            //printf(" i = %d  /  total = %d  /  new = %d\n",i, total, news);
    }

    freq = total/2;

   // speed = freq*60/2;
  
    /* Disable periodic interrupts */
    ioctl(fdr, RTC_PIE_OFF, 0);
    close(fdr);

	return freq;
}


/*
获取风扇反馈的转速，有偏差，大概100-200转速
由于要匹配两种风扇，所以需要cgminer一起来就要把风速调到100%计算采样频率freq_vendor，如果大于300就是永亿豪,反而就是仁海;
*/

int Get_fan_speed(unsigned char val){
    int speed;

    speed = 7800*(fan_duty/100);
    return speed;
}

void stu_set_pwm(unsigned char fan_id, int frequency, int duty)
{
	int fdexport, fdperid, fdduty;
        char period_s[8];
        char duty_s[8];
        //char ctl_s[2];

        char fpath[64];
        int fan;

        if(1==fan_id){
                fan = 4;
        }else{
                fan = 3;
        }
		if(duty > frequency)
		{
			duty = frequency;
		}
        memset(fpath, 0, sizeof(fpath));
        sprintf(fpath, SYSFS_PWM_PWM0, fan);
        if((access(fpath, F_OK)) == -1)
        {
                Write_Sys_file(fan, SYSFS_PWM_EXPORT, PWM); //export
        }

        sprintf(period_s, "%d", frequency);
        sprintf(duty_s, "%d", duty);
        //sprintf(ctl_s, "%d", ctl);


	Write_Sys_file(fan, SYSFS_PWM_DUTY_CYCLE, duty_s);
	cgsleep_ms(500);
   	Write_Sys_file(fan, SYSFS_PWM_PERIOD, period_s);
   	cgsleep_ms(500);
	Write_Sys_file(fan, SYSFS_PWM_ENABLE, ENABLE);  
}

void stu_board_fan_pwm_set(float duty1,  float duty2)
{
    int fd = 0;
    
	if(duty1 > 100 | duty2 > 100)
	{
		duty1 = 100.0;
		duty2 = 100.0;
	}
//	if(duty1 < 10 | duty2 < 10)
//	{
//		duty1 = 10.0;
//		duty2 = 10.0;
//	}

	if(duty1 == 0 | duty2 == 0)
	{
		duty_driver1 = 0;
		duty_driver2 = 0;
	}else{
		duty_driver1 = (int)(FAN_PWM_FREQ_TARGET / 100 * duty1);
		duty_driver2 = (int)(FAN_PWM_FREQ_TARGET / 100 * duty2);
	}

	stu_set_pwm(fanid, FAN_PWM_FREQ_TARGET, duty_driver1);//fan 1
	stu_set_pwm(fanid+1, FAN_PWM_FREQ_TARGET, duty_driver2);//fan 2

	fan_duty = duty1;
}

void HextoTwo(int num, char *point)
//void HextoTwo(int num)
{
    int res;
    int i = 0;
    char buf[BUFSIZ][5] = {"0000"};
    char reference[16][5] = {"0000","0001","0010","0011",\
                        "0100","0101","0110","0111",\
                        "1000","1001","1010","1011",\
                        "1100","1101","1110","1111"};

    while(num / 16 !=  0)
    {
        res = num % 16;
        strcpy(buf[i++], reference[res]);
        num = num / 16;
    }

    res = num % 16;
    strcpy(buf[i++], reference[res]);
	//printf("i = %d\n", i);
	
   
	for(i = 0; i < 4; i++)
	{
		point[i] = buf[1][i];
        
	}
	//while(i > 0)
	//{
	//printf("%s ", buf[1]);
	//}
}

float Temp_point(int temp)
{
	float temp_v, temp_t, temp_p;
	char str[5];
	int i;
	
	temp_p = 0.0f;
	float point[4] = {0.5, 0.25, 0.125, 0.0625};
	temp_v = (float)(temp & 0x00ff);
	//printf("temp_v = %f, %d", temp_v, temp>>12);
	HextoTwo(temp >> 8, str);
	for(i = 0; i < 4; i++)
	{
		//printf("str[%d] = %c  \n",i, str[i]);
		if(49 == str[i])
		{
			//printf("i = %d\n", i);
			temp_p += point[i];
		}
		
	}
	temp_t = temp_v + temp_p;
	//printf("temp_t = %f\n", temp_t);
	return temp_t;
}	

void stu_board_get_temp(uint8_t sensor_id)
{
	float retval = 0;
	char path[16];
	uint16_t buf = 0;
	struct i2c_ctx *T_sensor;
	bool ret;
	int i;


	if(sensor_id > 2)
		sensor_id = 2;

	pthread_mutex_lock(&temp_lock);

	memset(path, 0, sizeof(path));
	sprintf(path, TEMP_BUS, sensor_id);
	//printf("path = %s\n", path);
	T_sensor = i2c_slave_open(path, TEMP_ADDR);
	
	if(NULL == T_sensor){
		printf("t-sensor null \n");
		pthread_mutex_unlock(&temp_lock);
		return;
	}

	for(i = 0; i < 5; i++)
	{
		buf = T_sensor->read_word(T_sensor, 0);
		if(buf != -1)
		{
			retval = Temp_point(buf);
			if(retval < 100 && retval > 0)// ct75xx -40~125
			{
				g_board_temp[sensor_id] = (float)retval;
				//printf("mylog tempss %d = %f\n",sensor_id, g_board_temp[sensor_id]);
				break;
			}
		}  
		
		/*ret = T_sensor->read(T_sensor, 0, &retval);
		if(ret){
			if(retval < 100 && retval > 0)// ct75xx -40~125
			{
				g_board_temp[sensor_id] = (float)retval;
				break;
			}
		}*/
		
	}		
	T_sensor->exit(T_sensor);
	pthread_mutex_unlock(&temp_lock);

}

#if 0
float get_temp_val(void)
{
	int i;
	float max = 0.0f;
	
	for(i = 0; i < 3; i++)
	{
		stu_board_get_temp(i);
	}
	max = (g_board_temp[0]) > (float)(g_board_temp[1]) ? (float)(g_board_temp[0]) : (float)(g_board_temp[1]);
	max = (max > g_board_temp[2]) ? max : (float)(g_board_temp[2]);

	g_board_maxtemp = max;

	return max;
}
#endif

#if 1
float get_temp_val(void)
{	
	int i;	
	float max = 0.0f;	
	float maxx = 0.0f;		
	for(i = 0; i < 3; i++)
	{
		stu_board_get_temp(i);
	}
	max = (g_board_temp[0]) > (float)(g_board_temp[1]) ? (float)(g_board_temp[0]) : (float)(g_board_temp[1]);
	if (g_board_temp[2] == 0.0)	
	{		
		g_board_maxtemp = max;
		if(max > g_board_temp[0])		
		{			
			if(((g_board_temp[1] - g_board_temp[0]) >= 15)) 			
			{				
				//printf("1\n");
				return g_board_temp[1];			
			}else			
			{				
				//printf("2\n");				
				return ((max + g_board_temp[0])/2);			
			} 		
		}else		
		{			
			if(((g_board_temp[0] - g_board_temp[1]) >= 15)) 			
			{				
				//printf("3\n");
				return g_board_temp[0];			
			}else			
			{				
				//printf("4\n");				
				return ((max + g_board_temp[1])/2);			
			}		
		}	
	}
	else
	{		
		maxx = (max > g_board_temp[2]) ? max : (float)(g_board_temp[2]);		
		//printf("2.0--%f\n", maxx);		
		g_board_maxtemp = maxx;
		if(maxx > max)		
		{			
			if(((g_board_temp[2] - max) >= 15)) 			
			{				
				//printf("5\n");
				return maxx;			
			}else			
			{				
				//printf("6\n");				
				return ((max + g_board_temp[2])/2);			
			} 		
		}else		
		{			
			if(((max - g_board_temp[2]) >= 15))  			
			{				
				//printf("7\n");
				return maxx;			
			}else			
			{				
				//printf("8\n");				
				return ((max + g_board_temp[2])/2);			
			}		
		}				
	}	
}
#endif

void reset_hashboard_log(char * msg, char *devname)
{
	char cmd[128] = {0};
	system("date >> /home/root/reset.log");
	
	sprintf(cmd, "echo %s %s >> /home/root/reset.log", basename(devname),msg);
	system(cmd);
}

void stu_board_set_voltage_log(char * msg, uint32_t volt)
{
	char cmd[128] = {0};
	system("date >> /home/root/volt.log");

	sprintf(cmd, "echo %s: %d >> /home/root/volt.log",msg, volt);
	system(cmd);
}

//value: 1->on, 0->close
bool power_on_off(uint8_t value)
{
	struct i2c_ctx *Vol_control;
	bool ret;
	
	pthread_mutex_lock(&volt_lock);

	Vol_control =  i2c_slave_open(I2C3_BUS, 0x2c); 
	if (!Vol_control)
	{
		pthread_mutex_unlock(&volt_lock);
		return 0;
	}

	if (value)
		ret = Vol_control->write(Vol_control, 0x02, 0x01);
	else
		ret = Vol_control->write(Vol_control, 0x02, 0x00);
	
	if(!ret)
	{
		pthread_mutex_unlock(&volt_lock);
		return 0;
	}

	pthread_mutex_unlock(&volt_lock);
	
	return 1;
}

uint16_t stu_board_get_voltage()
{
	pthread_mutex_lock(&volt_lock);
	uint16_t buf = 0;
	int i;

	static struct i2c_ctx *Vol_control;

	Vol_control =  i2c_slave_open(I2C3_BUS, 0x2c); //set external vol
	if (!Vol_control)
	{
		pthread_mutex_unlock(&volt_lock);
		return 0;
	}

	//先读厂商ID， 判断厂商和型号，暂时没有添加

	//printf("write vale = 0x%04x\n", Wrvalue)

	for(i = 0; i < 5; i++)
	{
		buf = Vol_control->read_word(Vol_control, 0x12); //add seymour
		printf("write buf vale = 0x%04x\n", buf);
		if(buf != -1)
		{
			break;
		}  
		
	}	

	Vol_control->exit(Vol_control);

	pthread_mutex_unlock(&volt_lock);
	return buf;
}


bool stu_board_set_voltage(float voltage)
{
	pthread_mutex_lock(&volt_lock);
	uint16_t retval = 0;
	uint32_t ret;

	static struct i2c_ctx *Vol_control;

	
	uint16_t Wrvalue  = (uint16_t)(voltage*100);

	stu_board_set_voltage_log("stu_board_set_voltage", voltage);

	Vol_control =  i2c_slave_open(I2C3_BUS, 0x2c); //set external vol
	if ((voltage < 0) ||(voltage > 14.0)||!Vol_control)
	{
		pthread_mutex_unlock(&volt_lock);
		return 0;
	}

	//先读厂商ID， 判断厂商和型号，暂时没有添加

	printf("write vale = 0x%04x\n", Wrvalue);
	ret = Vol_control->write_word(Vol_control, 0x12, Wrvalue); //add seymour, changed by fire to 1800w
	/*if(!ret)
	{
		Vol_control->exit(Vol_control);
		pthread_mutex_unlock(&volt_lock);
		return 0;
	}*/

	/*check*/
	usleep(10000);
	retval = Vol_control->read_word(Vol_control, 0x12);
	Vol_control->exit(Vol_control);

	pthread_mutex_unlock(&volt_lock);
	return 1;
}

bool stu_board_fan_update(void)
{
	float temp_max;
	float new_temp;
	float pwm_v;
	int num;
	
	 temp_max  = get_temp_val();
	g_average_temp = ((g_board_temp[0] + g_board_temp[1] + g_board_temp[2])/3);

	///applog(LOG_ERR, "temp[0]: %.1f, temp[1]: %.1f, temp[2]: %.1f, g_average_temp: %.1f\n", g_board_temp[0], g_board_temp[1], g_board_temp[2], g_average_temp);
	///applog(LOG_ERR, "temp_max: %.1f\n", temp_max);

	applog(LOG_ERR, "max fan temperature------------------------------------ =%0.1f, %0.1f, %0.1f %0.1f", g_board_temp[0], g_board_temp[1], g_board_temp[2], temp_max);

	/*if(temp_max > ASIC_STU_FAN_TEMP_SHUTDOWN_THRESHOLD)
	{
		shut_down_all_devices();
	}else*/ 
	
	if(75.0f < temp_max)
	{
			shut_down_all_devices();
			applog(LOG_ERR, "over heat exit miner");
    		system("echo overheat_exit_miner >> /home/root/tmp.log");
    		exit(0);
	}else if(51.0f < temp_max)
	{
		if(100.0 != fan_duty)
			stu_board_fan_pwm_set(100.0, 100.0);
#if 1
		if(62.0f <= temp_max)
		{
			if (vol != 12.5)
			{
				stu_board_set_voltage(12.5);
				vol = 12.5;
			}
		}else if(61.0f > temp_max && 60.0f <= temp_max)
		{
			if (vol != 12.7)
			{
				stu_board_set_voltage(12.7);
				vol = 12.7;
			}
		}
		else if(59.0f > temp_max && 58.0f <= temp_max)
		{
			if (vol != 12.8)
			{
				stu_board_set_voltage(12.8);
				vol = 12.8;
			}
		}
		else if(57.0f > temp_max && 56.0f <= temp_max)
		{
			if (vol != 12.9)
			{
				stu_board_set_voltage(12.9);
				vol = 12.9;
			}
		}
		else if(55.0f > temp_max && 54.0f <= temp_max)
		{
			if (vol != 12.95)
			{
				stu_board_set_voltage(12.95);
				vol = 12.95;
			}
		}else if(53.0f > temp_max && 52.5f <= temp_max){
			if (vol != 13.0)
			{
				stu_board_set_voltage(13.0);
				vol = 13.0;
			}
		}
		
#endif		
	}
	else if((48.5f < temp_max) && (temp_max <= 50.0f))
	{
		 if(90.0 != fan_duty) 
			stu_board_fan_pwm_set(90.0, 90.0);
		 if (vol != 13.15)
			{
				stu_board_set_voltage(13.15);
				vol = 13.15;
			}
	}
	else if((47.5f  < temp_max) && (temp_max <= 48.0f))
	{	
		 if(80 != fan_duty) 
			stu_board_fan_pwm_set(80, 80);	
	}
	else if((46.5f < temp_max) && (temp_max <= 47.0f))
	{
		 if(70.0 != fan_duty) 
			stu_board_fan_pwm_set(70.0, 70.0);
		 if (vol != 13.3)
			{
				stu_board_set_voltage(13.3);
				vol = 13.3;
			}
	}
	else if((45.5f < temp_max) && (temp_max <= 46.0f))
	{
		 if(60.0 != fan_duty) 
			stu_board_fan_pwm_set(60.0, 60.0);
		 if (vol != 13.3)
			{
				stu_board_set_voltage(13.3);
				vol = 13.3;
			}
	}
	else if((44.5f < temp_max) && (temp_max <= 45.0f))
	{
		 if(50.0 != fan_duty) 
			stu_board_fan_pwm_set(50.0, 50.0);
		 if (vol != 13.3)
			{
				stu_board_set_voltage(13.3);
				vol = 13.3;
			}
	}
	else if((43.5f < temp_max) && (temp_max <= 44.0f))
	{
		 if(40.0 != fan_duty) 
			stu_board_fan_pwm_set(40.0, 40.0);
	}
	else if((42.5f < temp_max) && (temp_max <= 43.0f))
	{
		 if(30.0 != fan_duty) 
			stu_board_fan_pwm_set(30.0, 30.0);
	}
	else if((41.5f < temp_max) && (temp_max <= 42.0f))
	{
		 if(20.0 != fan_duty) 
			stu_board_fan_pwm_set(20.0, 20.0);
	}
	else if((40.5f < temp_max) && (temp_max <= 41.0f))
	{
		 if(15.0 != fan_duty) 
			stu_board_fan_pwm_set(15.0, 15.0);
	}
	/*else if((38.0f < temp_max) && (temp_max <= 40.0f))
	{
		 if(10.0 != fan_duty) 
			stu_board_fan_pwm_set(10.0, 10.0);
	}*/
	else if(temp_max < 40.0f)
	{
		volt_up = true;

		 if(0.0 != fan_duty)
			stu_board_fan_pwm_set(0.0, 0.0);
	}
	else
	{
		///printf("============ stu_board_fan_update, temp_max < ASIC_STU_FAN_TEMP_DOWN_THRESHOLD ==============\n");
	}

#if 0
	if(volt_down)
	{
		if(false == already_volt_down)
		{
			changevol = changevol - 1;

			if(changevol <= 0)
			{
				changevol = 0;
				already_volt_down = true;
			}
			else if(changevol > startvol)
			{
				changevol = startvol;
			}

			stu_board_set_voltage(changevol);
			volt_down = false;

			printf(" ***************************** volt down to %f ***************************** \n", workvolt);
		}
	}
	if(volt_up)
	{
		if(false == already_volt_up)
		{
			changevol = changevol + 1;

			if(changevol <= 0)
			{
				changevol = 0;
				already_volt_up = true;
			}
			else if(changevol > startvol)
			{
				changevol = startvol;
			}

			stu_board_set_voltage(changevol);
			volt_up = false;

			printf(" ***************************** volt up to %f ***************************** \n", workvolt);
		}
	}
#endif
}



 void auto_adjust_volt(uint32_t max_av_pll, uint32_t min_av_pll)
 {
	if (max_av_pll < pll3)
	{
		if(changevol < workvolt)
		{
			changevol +=1;
			stu_board_set_voltage(changevol);
		}
	}
	if (min_av_pll>pll3+15)
	{
		if (changevol>workvolt)
		{
			changevol -=1;
			stu_board_set_voltage(changevol);
		}
	}
 }

void Set_Led_OnOf(unsigned char num, unsigned char *val)
{
	Write_Sys_file(num+1, SYSFS_LED_BT, val);
}

void Set_Led_timer(unsigned char num)
{
	Write_Sys_file(num+1, SYSFS_LED_TR, SYSFS_LED_TIMER);
}

void print_chip_nonceInfo(struct stu_chain *achain)
{
	struct stu_chip *chip;
	double chip_total_right=0;
	double chip_total_hwe=0;
	double total_percent;
	
	double total_right_5min=0;
	double total_hwe_5min=0;
	double percent_5min;

	for (int i=0 ;i<ASIC_CHIP_NUM; i++)
	{
		chip = &achain->chips[i];
		chip_total_right = (chip->core_acc[0]+chip->core_acc[1]+chip->core_acc[2]+chip->core_acc[3]+chip->core_acc[4]+chip->core_acc[5]);
		chip_total_hwe = (chip->core_hwe[0]+chip->core_hwe[1]+chip->core_hwe[2]+chip->core_hwe[3]+chip->core_hwe[4]+chip->core_hwe[5]);
		if(0.0 == (chip_total_right+chip_total_hwe))
			total_percent=100;
		else
			total_percent = (chip_total_hwe/(chip_total_right+chip_total_hwe))*100;

		total_right_5min = chip->acc_5min;
		total_hwe_5min = chip->hwe_5min;
		if (0 == (total_hwe_5min+total_right_5min))
			percent_5min=100;
		else	
			percent_5min = (total_hwe_5min/(total_hwe_5min+total_right_5min))*100;
		
		applog(LOG_ERR, "%s:chip_%d:total_right=%d,total_hwe=%d (%0.1f%%), 5min_right=%d, 5min_hwe=%d (%0.1f%%)", basename(achain->devname),i+1,
								(int)chip_total_right, (int)chip_total_hwe, total_percent,chip->acc_5min,chip->hwe_5min,percent_5min);

		chip->acc_5min=0;
		chip->hwe_5min=0;
	}
		
}

void print_core_nonce_info(struct stu_chain *achain, struct stu_chip *chip, uint8_t chip_id)
{
		applog(LOG_ERR, "+++%s chip_%d+++ : \n                           core0=%d(%d), core1=%d(%d), core2=%d(%d),\
						\n                           core3=%d(%d), core4=%d(%d), core5=%d(%d)",
				basename(achain->devname),
				chip_id, 
				chip->core_acc[0],chip->core_hwe[0],chip->core_acc[1],chip->core_hwe[1],chip->core_acc[2],chip->core_hwe[2],
				chip->core_acc[3],chip->core_hwe[3],chip->core_acc[4],chip->core_hwe[4],chip->core_acc[5], chip->core_hwe[5]);
		applog(LOG_ERR, "%s chain-total-right-nonce == %d", basename(achain->devname), achain->right_nonce);
		applog(LOG_ERR, "%s chain-total-wrong-nonce == %d", basename(achain->devname), achain->wrong_nonce);

		int cnt=0;
		int cnt1=0;
		for (int i= 0; i<ASIC_CHIP_NUM; i++)
		{
			cnt += achain->chips[i].core_acc[0];
			cnt1 += achain->chips[i].core_hwe[0];
		}
		applog(LOG_ERR, "core0-total-nonce == %d(%d)", cnt, cnt1);
		 cnt=0;
		 cnt1=0;
		for (int i= 0; i<ASIC_CHIP_NUM; i++)
		{
			cnt += achain->chips[i].core_acc[1];
			cnt1 += achain->chips[i].core_hwe[1];
		}
		applog(LOG_ERR, "core1-total-nonce == %d(%d)", cnt, cnt1);
		 cnt=0;
		 cnt1=0;
		for (int i= 0; i<ASIC_CHIP_NUM; i++)
		{
			cnt += achain->chips[i].core_acc[2];
			cnt1 += achain->chips[i].core_hwe[2];
		}
		applog(LOG_ERR, "core2-total-nonce == %d(%d)", cnt, cnt1);
		 cnt=0;
		 cnt1=0;
		for (int i= 0; i<ASIC_CHIP_NUM; i++)
		{
			cnt += achain->chips[i].core_acc[3];
			cnt1 += achain->chips[i].core_hwe[3];
		}
		applog(LOG_ERR, "core3-total-nonce == %d(%d)", cnt, cnt1);
		 cnt=0;
		 cnt1=0;
		for (int i= 0; i<ASIC_CHIP_NUM; i++)
		{
			cnt += achain->chips[i].core_acc[4];
			cnt1 += achain->chips[i].core_hwe[4];
		}
		applog(LOG_ERR, "core4-total-nonce == %d(%d)", cnt, cnt1);
		 cnt=0;
		 cnt1=0;
		for (int i= 0; i<ASIC_CHIP_NUM; i++)
		{
			cnt += achain->chips[i].core_acc[5];
			cnt1 += achain->chips[i].core_hwe[5];
		}
		applog(LOG_ERR, "core5-total-nonce == %d(%d)", cnt, cnt1);
}

bool ifcheck_crc_pass(struct stu_chain *achain)
{
	int crc_err_ChipCnt=0;
	int threhold = achain->set_job_cnt*0.5; 

	for (int i=0; i<ASIC_CHIP_NUM; i++)
	{
		if(achain->chips[i].crc_error> threhold)
		{
			crc_err_ChipCnt++;
		}
		achain->chips[i].crc_error=0;
	}

	//有10颗以上芯片的crc error 大于阈值需要重启
	if (crc_err_ChipCnt>10)
	{
		return false;
	}

	return true;
}

uint8_t get_regLen(uint8_t reg)
{
	uint8_t len=8;

	if ((reg == 2))
	{
		len=16;
	}
	else if (reg==7)
	{
		len=92;
	}
	else if (reg==8)
	{
		len=4;
	}
	else if ((reg==9)||(reg == 15))
	{
		len=12;
	}
	else if((reg>=10)&&(reg<=13)||(reg==16))
	{
		len=4;
	}


	return len;
}

uint8_t get_regLenEx(uint8_t reg)
{
	uint8_t len=8;
	switch(reg)
	{
		case 1:
		case 3:
		case 4:
		case 5:
		case 6:
		case 14:
		case 20:
			len=8;break;
		case 2:
			len=16;break;
		case 8:
		case 10:
		case 11:
		case 12:
		case 13:
		case 16:
		case 17:
		case 18:
		case 19:
			len=4;break;
		case 7:
			len=92;break;
		case 9:
		case 15:
			len=12;break;
		default:
			len=8;
	}
	return len;
}



















