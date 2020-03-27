#include "stu-device.h"
#include "spi-context.h"
#include "stu-pll.h"

void get_pll_MPS(struct pll_M_P_S *pll_mps, uint32_t pll)
{
	uint16_t (*p)[4]=pll_array;

	//set default , 500M
	pll_mps->regPll_M = 480;
	pll_mps->regPll_P =6;
	pll_mps->regPll_P = 1;

	while((*p)[0] !=  0)
	{
		if ((*p)[0] >= pll)
		{
			pll_mps->regPll_M = (*p)[1];
			pll_mps->regPll_P = (*p)[2];
			pll_mps->regPll_S= (*p)[3];
			break;
		}

		p++;
	}
}

bool cmd_set_pll0(struct spi_ctx *ctx, uint8_t chip_id, uint32_t pll)
{
	uint8_t spi_tx[MAX_CMD_LENGTH]={0};
	uint8_t spi_rx[MAX_CMD_LENGTH]={0};
	uint16_t crc;
	uint16_t rx_crc;
	struct pll_M_P_S pll_mps;
	uint8_t regpll[4]={0x40, 0x00, 0x00, 0x65};

	get_pll_MPS(&pll_mps, pll);

	spi_tx[0] = CMD_WRITE_REG;
	spi_tx[1] = chip_id;
	spi_tx[2] = 0x00;
	spi_tx[3] = 0x0a;

	//P
	regpll[1] = (regpll[1]&0x0f)|(pll_mps.regPll_P<<4);
	//M
	regpll[1] = (regpll[1]&0xf0)|((pll_mps.regPll_M&0x0f00)>>8);
	regpll[2] = pll_mps.regPll_M&0xff;
	//S
	regpll[3] = (regpll[3]&0x0f)|(pll_mps.regPll_S<<4);

	spi_tx[8] = (uint8_t)((crc >> 0) & 0xff);
	spi_tx[9] = (uint8_t)((crc >> 8) & 0xff);
	memcpy(spi_tx+4, regpll, 4);

	applog(LOG_ERR, " set pll0 P=%d, M=%d, S=%d:", 
			pll_mps.regPll_P, pll_mps.regPll_P, pll_mps.regPll_S);
	for (int i=0; i<8; i++)
		printf("%02x ", spi_tx[i]);
	printf("\n");
	swap_data(spi_tx, 8);
	crc = CRC16(spi_tx, 8);

	spi_tx[8] = (uint8_t)((crc >> 0) & 0xff);
	spi_tx[9] = (uint8_t)((crc >> 8) & 0xff);

	spi_send_data(ctx, spi_tx, 10);
	applog(LOG_ERR,"--sppi_tx[0]=%02x",spi_tx[0]);
	
	if (spi_tx[0] != 0x00)
	{
		spi_poll_result(ctx, spi_tx[1], chip_id, spi_rx, 2);
		applog(LOG_ERR, "chip_%d, bank10 return:%02x %02x %02x %02x",
					chip_id, spi_rx[0],spi_rx[1],spi_rx[2],spi_rx[3] );

		memcpy(&rx_crc, &spi_rx[2], 2);
		applog(LOG_ERR,"crc=%04x, rx_crc=%04x", crc,rx_crc);
		
		if (crc != rx_crc)
		{
			applog(LOG_ERR, "write chip_%d pll0 error!", chip_id);
		}
	}
	flush_spi(ctx);
}

bool cmd_set_pll1(struct spi_ctx *ctx, uint8_t chip_id, uint32_t pll)
{
	uint8_t spi_tx[MAX_CMD_LENGTH]={0};
	uint8_t spi_rx[MAX_CMD_LENGTH]={0};
	uint16_t crc;
	uint16_t rx_crc;
	struct pll_M_P_S pll_mps;
	uint8_t regpll[4]={0x40, 0x00, 0x00, 0x65};

	get_pll_MPS(&pll_mps, pll);

	spi_tx[0] = CMD_WRITE_REG;
	spi_tx[1] = chip_id;
	spi_tx[2] = 0x00;
	spi_tx[3] = 0x0b;

	//P
	regpll[1] = (regpll[1]&0x0f)|(pll_mps.regPll_P<<4);
	//M
	regpll[1] = (regpll[1]&0xf0)|((pll_mps.regPll_M&0x0f00)>>8);
	regpll[2] = pll_mps.regPll_M&0xff;
	//S
	regpll[3] = (regpll[3]&0x0f)|(pll_mps.regPll_S<<4);

	spi_tx[8] = (uint8_t)((crc >> 0) & 0xff);
	spi_tx[9] = (uint8_t)((crc >> 8) & 0xff);
	memcpy(spi_tx+4, regpll, 4);

	applog(LOG_ERR, " set pll1 P=%d, M=%d, S=%d:", 
			pll_mps.regPll_P, pll_mps.regPll_P, pll_mps.regPll_S);
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
		applog(LOG_ERR, "chip_%d, bank11 return:%02x %02x %02x %02x",
					chip_id, spi_rx[0],spi_rx[1],spi_rx[2],spi_rx[3] );

		memcpy(&rx_crc, &spi_rx[2], 2);
		applog(LOG_ERR,"crc=%04x, rx_crc=%04x", crc,rx_crc);
		
		if (crc != rx_crc)
		{
			applog(LOG_ERR, "write chip_%d pll1 error!", chip_id);
		}
	}
	flush_spi(ctx);
}

bool cmd_set_pll2(struct spi_ctx *ctx, uint8_t chip_id, uint32_t pll)
{
	uint8_t spi_tx[MAX_CMD_LENGTH]={0};
	uint8_t spi_rx[MAX_CMD_LENGTH]={0};
	uint16_t crc;
	uint16_t rx_crc;
	struct pll_M_P_S pll_mps;
	uint8_t regpll[4]={0x40, 0x00, 0x00, 0x65};

	get_pll_MPS(&pll_mps, pll);

	spi_tx[0] = CMD_WRITE_REG;
	spi_tx[1] = chip_id;
	spi_tx[2] = 0x00;
	spi_tx[3] = 0x0c;

	//P
	regpll[1] = (regpll[1]&0x0f)|(pll_mps.regPll_P<<4);
	//M
	regpll[1] = (regpll[1]&0xf0)|((pll_mps.regPll_M&0x0f00)>>8);
	regpll[2] = pll_mps.regPll_M&0xff;
	//S
	regpll[3] = (regpll[3]&0x0f)|(pll_mps.regPll_S<<4);

	spi_tx[8] = (uint8_t)((crc >> 0) & 0xff);
	spi_tx[9] = (uint8_t)((crc >> 8) & 0xff);
	memcpy(spi_tx+4, regpll, 4);

	applog(LOG_ERR, " set pll2 P=%d, M=%d, S=%d:", 
			pll_mps.regPll_P, pll_mps.regPll_P, pll_mps.regPll_S);
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
		applog(LOG_ERR, "chip_%d, bank12 return:%02x %02x %02x %02x",
					chip_id, spi_rx[0],spi_rx[1],spi_rx[2],spi_rx[3] );

		memcpy(&rx_crc, &spi_rx[2], 2);
		applog(LOG_ERR,"crc=%04x, rx_crc=%04x", crc,rx_crc);
		
		if (crc != rx_crc)
		{
			applog(LOG_ERR, "write chip_%d pll2 error!", chip_id);
		}
	}
	flush_spi(ctx);
}

bool cmd_set_pll3(struct spi_ctx *ctx, uint8_t chip_id, uint32_t pll)
{
	uint8_t spi_tx[MAX_CMD_LENGTH]={0};
	uint8_t spi_rx[MAX_CMD_LENGTH]={0};
	uint16_t crc;
	uint16_t rx_crc;
	struct pll_M_P_S pll_mps;
	uint8_t regpll[4]={0x40, 0x00, 0x00, 0x65};

	get_pll_MPS(&pll_mps, pll);

	spi_tx[0] = CMD_WRITE_REG;
	spi_tx[1] = chip_id;
	spi_tx[2] = 0x00;
	spi_tx[3] = 0x0d;

	//P
	regpll[1] = (regpll[1]&0x0f)|(pll_mps.regPll_P<<4);
	//M
	regpll[1] = (regpll[1]&0xf0)|((pll_mps.regPll_M&0x0f00)>>8);
	regpll[2] = pll_mps.regPll_M&0xff;
	//S
	regpll[3] = (regpll[3]&0x0f)|(pll_mps.regPll_S<<4);

	spi_tx[8] = (uint8_t)((crc >> 0) & 0xff);
	spi_tx[9] = (uint8_t)((crc >> 8) & 0xff);
	memcpy(spi_tx+4, regpll, 4);

	applog(LOG_ERR, " set pll3 P=%d, M=%d, S=%d:", 
			pll_mps.regPll_P, pll_mps.regPll_P, pll_mps.regPll_S);
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
		applog(LOG_ERR, "chip_%d, bank13 return:%02x %02x %02x %02x",
					chip_id, spi_rx[0],spi_rx[1],spi_rx[2],spi_rx[3] );

		memcpy(&rx_crc, &spi_rx[2], 2);
		applog(LOG_ERR,"crc=%04x, rx_crc=%04x", crc,rx_crc);
		
		if (crc != rx_crc)
		{
			applog(LOG_ERR, "write chip_%d pll3 error!", chip_id);
		}
	}
	flush_spi(ctx);
}

bool cmd_write_register_16(struct spi_ctx *ctx, uint8_t chip_id, uint8_t step)
{
	uint8_t spi_tx[MAX_CMD_LENGTH]={0};
	uint8_t spi_rx[MAX_CMD_LENGTH]={0};
	uint16_t crc;
	uint16_t rx_crc;
	uint8_t bankVal[4]={0x01, 0x00, 0x83, 0xf5};
	
	spi_tx[0] = CMD_WRITE_REG;
	spi_tx[1] = chip_id;
	spi_tx[2] = 0x00;
	spi_tx[3] = 16;

	memcpy(spi_tx+4, bankVal, 4);
	
	applog(LOG_ERR, "bank16:");
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
		applog(LOG_ERR, "chip_%d, bank16 return:%02x %02x %02x %02x",
					chip_id, spi_rx[0],spi_rx[1],spi_rx[2],spi_rx[3] );
		memcpy(&rx_crc, &spi_rx[2], 2);
		applog(LOG_ERR,"crc=%04x, rx_crc=%04x", crc,rx_crc);
		
		if (crc != rx_crc)
		{
			applog(LOG_ERR, "write chip_%d bank16 error!", chip_id);
		}
	}
	flush_spi(ctx);

	return true;
}

bool cmd_write_register_17(struct spi_ctx *ctx, uint8_t chip_id, uint8_t step)
{
	uint8_t spi_tx[MAX_CMD_LENGTH]={0};
	uint8_t spi_rx[MAX_CMD_LENGTH]={0};
	uint16_t crc;
	uint16_t rx_crc;
	uint8_t bankVal[4]={0x01, 0x00, 0x83, 0xf5};
	
	spi_tx[0] = CMD_WRITE_REG;
	spi_tx[1] = chip_id;
	spi_tx[2] = 0x00;
	spi_tx[3] = 16;

	memcpy(spi_tx+4, bankVal, 4);
	
	applog(LOG_ERR, "bank17:");
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
		applog(LOG_ERR, "chip_%d, bank17 return:%02x %02x %02x %02x",
					chip_id, spi_rx[0],spi_rx[1],spi_rx[2],spi_rx[3] );
		memcpy(&rx_crc, &spi_rx[2], 2);
		applog(LOG_ERR,"crc=%04x, rx_crc=%04x", crc,rx_crc);
		
		if (crc != rx_crc)
		{
			applog(LOG_ERR, "write chip_%d bank17 error!", chip_id);
		}
	}
	flush_spi(ctx);

	return true;
}

bool cmd_write_register_18(struct spi_ctx *ctx, uint8_t chip_id, uint8_t step)
{
	uint8_t spi_tx[MAX_CMD_LENGTH]={0};
	uint8_t spi_rx[MAX_CMD_LENGTH]={0};
	uint16_t crc;
	uint16_t rx_crc;
	uint8_t bankVal[4]={0x01, 0x00, 0x83, 0xf5};
	
	spi_tx[0] = CMD_WRITE_REG;
	spi_tx[1] = chip_id;
	spi_tx[2] = 0x00;
	spi_tx[3] = 16;

	memcpy(spi_tx+4, bankVal, 4);
	
	applog(LOG_ERR, "bank18:");
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
		applog(LOG_ERR, "chip_%d, bank18 return:%02x %02x %02x %02x",
					chip_id, spi_rx[0],spi_rx[1],spi_rx[2],spi_rx[3] );
		memcpy(&rx_crc, &spi_rx[2], 2);
		applog(LOG_ERR,"crc=%04x, rx_crc=%04x", crc,rx_crc);
		
		if (crc != rx_crc)
		{
			applog(LOG_ERR, "write chip_%d bank18 error!", chip_id);
		}
	}
	flush_spi(ctx);

	return true;
}

bool cmd_write_register_19(struct spi_ctx *ctx, uint8_t chip_id, uint8_t step)
{
	uint8_t spi_tx[MAX_CMD_LENGTH]={0};
	uint8_t spi_rx[MAX_CMD_LENGTH]={0};
	uint16_t crc;
	uint16_t rx_crc;
	uint8_t bankVal[4]={0x01, 0x00, 0x83, 0xf5};
	
	spi_tx[0] = CMD_WRITE_REG;
	spi_tx[1] = chip_id;
	spi_tx[2] = 0x00;
	spi_tx[3] = 16;

	memcpy(spi_tx+4, bankVal, 4);
	
	applog(LOG_ERR, "bank19:");
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
		applog(LOG_ERR, "chip_%d, bank19 return:%02x %02x %02x %02x",
					chip_id, spi_rx[0],spi_rx[1],spi_rx[2],spi_rx[3] );
		memcpy(&rx_crc, &spi_rx[2], 2);
		applog(LOG_ERR,"crc=%04x, rx_crc=%04x", crc,rx_crc);
		
		if (crc != rx_crc)
		{
			applog(LOG_ERR, "write chip_%d bank19 error!", chip_id);
		}
	}
	flush_spi(ctx);

	return true;
}

void inline upgrade_pll(struct stu_chain *achain)
{
	struct spi_ctx *ctx = achain->spi_ctx;
	achain->newpll += STEP;

	applog(LOG_ERR, "newpll=%d, finalpll=%d",achain->newpll ,achain->finalpll);
	applog(LOG_ERR, "%s new pll = %d", basename(achain->devname), achain->newpll);

	cmd_set_pll0(ctx, ADDR_BROADCAST, achain->newpll);

	if (achain->newpll >= achain->finalpll)
	{
		cmd_set_pll3(ctx, ADDR_BROADCAST, pll3);
		cmd_set_pll2(ctx, ADDR_BROADCAST, pll2);
		cmd_set_pll1(ctx, ADDR_BROADCAST, pll1);
		cmd_set_pll0(ctx, ADDR_BROADCAST, pll0);
		achain->update_pll_finish = true;
		achain->need_wtlevel = true;

		#ifdef USE_H8
		//启动快速流水
		applog(LOG_ERR, "upgrade pll finish, start quick speed");
		write_register_2_quick_spd(ctx, ADDR_BROADCAST, 1, 1, 0);
		#endif
	}
}

void inline cal_chippll(struct stu_chain *achain, uint8_t *buff, uint8_t chip_id, uint8_t len)
{
	uint8_t H_bit;
	uint8_t L_bit;
	uint16_t tmp;

	achain->chips[chip_id-1].pllCnt[0] = 0;
	achain->chips[chip_id-1].pllCnt[1] = 0;
	achain->chips[chip_id-1].pllCnt[2] = 0;
	achain->chips[chip_id-1].pllCnt[3] = 0;

	tmp = *((uint16_t *)(buff+10));
	achain->chips[chip_id-1].pllCnt[0] = bswap_16(tmp)&0x0fff;

	tmp = *((uint16_t *)(buff+9));
	achain->chips[chip_id-1].pllCnt[1] = (bswap_16(tmp)&0xfff0)>>4;

	tmp = *((uint16_t *)(buff+7));
	achain->chips[chip_id-1].pllCnt[2] = bswap_16(tmp)&0x0fff;

	tmp = *((uint16_t *)(buff+6));
	achain->chips[chip_id-1].pllCnt[3] =  (bswap_16(tmp)&0xfff0)>>4;

	applog(LOG_ERR, "chip_%d pll3~pll0 cores:%d %d %d %d",chip_id, 
		achain->chips[chip_id-1].pllCnt[3],
		achain->chips[chip_id-1].pllCnt[2],
		achain->chips[chip_id-1].pllCnt[1],
		achain->chips[chip_id-1].pllCnt[0]);
}

void inline auto_adjust_pll3(struct stu_chain *achain)
{
	#define READ_INTERVAL	5 
	#define PLLthrehold_LOW 128*0.7 // 70%
	#define PLLthrehold_HIGH 128*0.9 // 90%

	bool need_update = false;
	uint8_t chip_id;
	uint8_t reg_buff[12];
	struct spi_ctx *ctx = achain->spi_ctx;
	struct timeval t_now;
	cgtime(&t_now);
	uint32_t t_diff = tdiff(&t_now, &achain->last_autopll_t);
	struct cgpu_info *cgpu = achain->cgpu;

	if (t_diff > READ_INTERVAL)
	{
		chip_id = achain->read_chipid;
		cmd_read_register(ctx,  chip_id, 9, 12, reg_buff);

		#if 1
		for (int i=0; i<12; i++)
			printf("%02x ", reg_buff[i]);
		printf("\n");
		#endif

		cal_chippll(achain,reg_buff, chip_id, 12);
		
		if (achain->chips[chip_id-1].pllCnt[3]<PLLthrehold_LOW)
		{
			if (achain->chips[chip_id-1].pll[3] > pll2+20)
			{
				applog(LOG_ERR, "%s chip_%d pll3 increase 5M",  basename(achain->devname), chip_id);
				achain->chips[chip_id-1].pll[3] -= 5;
				need_update = true;
			}
		}
		else if (achain->chips[chip_id-1].pllCnt[3]>PLLthrehold_HIGH)
		{
			if(achain->chips[chip_id-1].pll[3]< pll3+20)
			{
				applog(LOG_ERR, "%s chip_%d pll3 decrease 5M",  basename(achain->devname), chip_id);
				achain->chips[chip_id-1].pll[3] += 5;
				need_update = true;
			}
		}

		if(need_update)
		{
			cmd_set_pll3(ctx,  chip_id, achain->chips[chip_id-1].pll[3]);
		}

		achain->read_chipid++;
		if (achain->read_chipid > ASIC_CHIP_NUM)
		{
			achain->read_chipid = 1;
			//只需要一个线程做
			if (0 == cgpu->device_id)
			{
				achain->enable_auto_volt= true;
			}
		}

		cgtime(&achain->last_autopll_t);
	}
}

//计算各算力板的平均PLL并放回最大和最小值
void get_chain_av_pll(uint32_t *maxPll, uint32_t *minPll)
{
	struct stu_chain *chain;
	struct stu_chip *chip;
	uint32_t core_total_pll;
	uint32_t tmp_avpll[4];
	int i,j;
	
	for ( i=0; i<total_devices; i++)
	{
		chain = devices[i]->device_data;
		core_total_pll = 0;

		for ( j=0; j<chain->num_active_chips; j++)
		{
			chip = &chain->chips[j];

			core_total_pll += chip->pllCnt[0]*chip->pll[0]+ chip->pllCnt[1]*chip->pll[1] 
				+ chip->pllCnt[2]*chip->pll[2] +chip->pllCnt[3]*chip->pll[3];
		}
		chain->avpll = core_total_pll/(chain->num_active_chips*TOTAL_CORE);
		tmp_avpll[i] = chain->avpll;
	}

	*maxPll = tmp_avpll[0];
	*minPll = tmp_avpll[0];

	for ( i=1; i<total_devices; i++)
	{
		if (tmp_avpll[i] > (*maxPll))
		{
			*maxPll = tmp_avpll[i];
		}

		if (tmp_avpll[i] < (*minPll))
		{
			*minPll = tmp_avpll[i];
		}
	}
}
