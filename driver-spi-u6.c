#include "miner.h"
#include "stu-device.h"

extern float g_average_temp;
static uint32_t set_work_interval = 15;
struct spi_config cfg[ASIC_CHAIN_NUM];
struct spi_ctx *spi[ASIC_CHAIN_NUM];
struct stu_chain *chain[ASIC_CHAIN_NUM];
uint8_t waittemp = 1;

static bool wq_enqueue(struct work_queue *wq, struct work *work)
{
	if (work == NULL)
		return false;
	struct work_ent *we = malloc(sizeof(*we));
	assert(we != NULL);

	we->work = work;
	INIT_LIST_HEAD(&we->head);
	list_add_tail(&we->head, &wq->head);
	wq->num_elems++;
	return true;
}

static struct work *wq_dequeue(struct work_queue *wq)
{
	if (wq == NULL)
		return NULL;
	if (wq->num_elems == 0)
		return NULL;
	struct work_ent *we;
	we = list_entry(wq->head.next, struct work_ent, head);
	struct work *work = we->work;

	list_del(&we->head);
	free(we);
	wq->num_elems--;
	return work;
}


struct stu_chain *init_u6_chain(struct spi_ctx *ctx, int chain_id)
{
  uint8_t devname[64]={0};
  char *p;
  int n;
  int loop;
  bool ret = false;
  struct stu_chain *achain =  calloc(1, sizeof(*achain));

  sprintf(devname, SPI_DEVICE_TEMPLATE, ctx->config.bus, ctx->config.cs_line);
  achain->chain_id = ctx->config.bus;
  achain->spi_ctx = ctx;
  achain->devname = strdup(devname);
  
  achain->num_chips = ASIC_CHIP_NUM;
  achain->num_active_chips = ASIC_CHIP_NUM;
  achain->chips = calloc(achain->num_active_chips, sizeof(struct stu_chip));
  achain->read_chipid = 1;

#if 0
  for(loop = 0; loop < 3; loop++)
  {
    applog(LOG_ERR, "start cmd_auto_address... loop: %d", loop);

    ret = cmd_auto_address(ctx, ADDR_BROADCAST);

    if(true == ret)
    {
      break;
    }
    else
    {
      cgsleep_ms(100);
    }
  }

  if (!ret)
  {
    if ( !cmd_detect_chipx(ctx, ASIC_CHIP_NUM/2))
    {
      applog(LOG_ERR, "%s cmd_auto_address fail ! loop: %d", devname, loop);
      goto FAIL;
    }
  }
  applog(LOG_ERR, "cmd_auto_address finished... loop%d", loop);
#endif  

  for (int i=0; i<ASIC_CHIP_NUM; i++)
  {
    achain->chips[i].if_chip_ok = cmd_read_register(ctx, i+1, 10, 4 , NULL);
  }
    
  mutex_init(&achain->lock);
  INIT_LIST_HEAD(&achain->active_wq.head);

  return achain;

FAIL:  
  free(achain->chips);
  free(achain->devname);
  free(achain);
  return NULL;

}


void u6_detect_chain(bool hotplug)
{
  int i, j;
  bool ret = false;
  int valid_board_cnt=0;

  #define VALID_BOARD_NUM 2
  
  for (i=0; i<ASIC_CHAIN_NUM; i++)
  {
    cfg[i].bus     = i;
    cfg[i].cs_line = 0;
    cfg[i].mode    = SPI_MODE_0;
    cfg[i].speed   = DEFAULT_SPI_SPEED;
    cfg[i].bits    = 16;
    cfg[i].delay   = DEFAULT_SPI_DELAY_USECS;

    spi[i] = spi_init(&cfg[i]);
    if(spi[i] == NULL)
    {
      applog(LOG_ERR, "spi init fail");
      continue;
    }

    applog(LOG_WARNING, "spi_init, default SPI[%d] speed: %d", i , spi[i]->config.speed);
    
  }


  for (j=0; j<3;j++)
  {
    if (spi[j] == NULL)
      continue;

    
    power_on_off(0);
    sleep(1);
    power_on_off(1);
    sleep(1);
  
    valid_board_cnt=0;
   
    
    for (i=0; i<ASIC_CHAIN_NUM; i++)
    {
      if (spi[i] == NULL)
        continue;
	  
	stu_dev_init(i);
      spi[i]->valid_board = false;
      stu_board_hw_reset(spi[i]->config.bus);
      ret = cmd_auto_address(spi[i], ADDR_BROADCAST);
      if (!ret)
      {
        cgsleep_ms(2);
        ret = cmd_detect_chipx(spi[i], 2);
      }
      if (ret)
      {
        valid_board_cnt++;
        spi[i]->valid_board =true;
      }
    }

    if (valid_board_cnt >=VALID_BOARD_NUM)
      break;    

  }


  for (i=0; i<ASIC_CHAIN_NUM; i++)
  {
  	Set_Led_OnOf(spi[i]->config.bus, LED_DISABLE);
    if((spi[i] == NULL) ||(!spi[i]->valid_board))
      continue;
    
    chain[i] = init_u6_chain(spi[i], i);
    
    config_hash_board(chain[i]);

    Set_Led_OnOf(spi[i]->config.bus, LED_ENABLE);
    struct cgpu_info *cgpu = malloc(sizeof(*cgpu));

    memset(cgpu, 0, sizeof(*cgpu));
    cgpu->drv = &u6_drv;
    cgpu->name = "u6.SingleChain";
    cgpu->threads = 1;

    cgpu->device_data = chain[i];
    chain[i]->cgpu = cgpu;
    add_cgpu(cgpu);
  }

  sprintf(g_miner_version, "%s", "19.11.19.16");
  sprintf(g_miner_compiletime, "%s", "Tue Nov 19 15:59:48 CST 2019");
  sprintf(g_miner_type, "%s", "StuMiner U6"); 
}


void be32enc(void *pp, uint32_t x)
{
	uint8_t *p = (uint8_t *)pp;
	p[3] = x & 0xff;
	p[2] = (x >> 8) & 0xff;
	p[1] = (x >> 16) & 0xff;
	p[0] = (x >> 24) & 0xff;
}

static uint8_t  * create_job( struct work *work, uint8_t chip_id, uint8_t job_id)
{
	uint32_t *wdata = (uint32_t *)(work->data + 32*4);
	//uint32_t *pmidstate = (uint32_t *)(work->midstate);
	uint32_t *ptarget = (uint32_t *)(work->target);
	int i, bpos, k, x;
	uint32_t *pdata = (uint32_t *)(work->data);

	static uint8_t job[JOB_LENGTH] ;
	uint32_t midstate2[8];
	uint32_t data2[16];
	uint32_t target2[8];
	uint32_t endiance[20] = {0};

	memset(job, 0, sizeof(job));

	job[0] = CMD_WRITE_JOB;
	job[1] = chip_id;
	job[2] = 0x00;
	job[3] = 0x00;
	
	#if 0 //enable test work
	memcpy(work->data, jobx, 20*4);
	#endif

	for (int k=0;k<20; k++)
		endiance[k] = swab32(pdata[k]);
	memcpy(job+4, endiance, 80);

	/*Luck fire new version fpga*/
	//job[82] = 0x00; job[83] = 0x00; job[84] = 0x00; job[85] = 0x00;

	/* crc */
	uint8_t tmp_buf[84];
	uint16_t crc;

	memset(tmp_buf, 0, sizeof(tmp_buf));
	for(i = 0; i < 42; i++)
	{
		tmp_buf[(2 * i) + 1] = job[(2 * i) + 0];
		tmp_buf[(2 * i) + 0] = job[(2 * i) + 1];
	}
	crc = CRC16(tmp_buf, 84);
	job[84] = (uint8_t)((crc >> 8) & 0xff);
	job[85] = (uint8_t)((crc >> 0) & 0xff);


	#if 0
		applog(LOG_ERR, "job data:");
		
		printf("%02x %02x %02x %02x ", job[0], job[1], job[2], job[3]);

		pdata = (uint32_t *)(&job[4]);
		for (int i=0; i<20; i++)
		{
			printf("%08x ", pdata[i]);
		}
		printf("%04x", crc);
		printf("\n");
	#endif

	swap_data(job, JOB_LENGTH);

	return job;
}

 static bool get_nonce(struct stu_chain *achain, uint8_t *nonce, uint8_t *chip_id , uint8_t *ntime)
{
	uint8_t buffer[24]={0};
	if (cmd_read_result(achain, ADDR_BROADCAST,buffer))
	{
		uint16_t rx_crc = CRC16(buffer, 8);
		uint16_t *pcrc = &buffer[8];

		//applog(LOG_ERR, "cal_crc=%4x  rx_crc=%04x", rx_crc, *pcrc);
		
		if (rx_crc != *pcrc)
		{
			applog(LOG_ERR, "read nonce crc error!");
			//return false;
		}
		
		*chip_id = buffer[0];
		*ntime = buffer[2];

		//nonce[0] = buffer[6];
		//nonce[1] = buffer[7];
		//nonce[2] = buffer[4];
		//nonce[3] = buffer[5];
		memcpy(nonce, buffer+4, 4);
		
		//applog(LOG_ERR, "Got nonce for chip_%d   NONCE=0x%08x, ntime=%02x",
		//	   *chip_id,  (*(uint32_t *)nonce), *ntime);
		
		return true;
	}
	return false;
}

 void inline u6_change_target(struct stu_chain *achain, uint32_t new_diff)
 {
	if (achain->current_HTarget6 != new_diff)
	{
		achain->current_HTarget6 = new_diff;
		uint32_t setTarget = (new_diff>>20)&0x0000ffff;
		cmd_write_register_4(achain->spi_ctx, ADDR_BROADCAST, setTarget);
	}
}

static bool set_work(struct stu_chain *achain,uint8_t chip_id, struct work *work)
{
	bool ret=false;
	struct spi_ctx *ctx = achain->spi_ctx;
	struct stu_chip *chip = &achain->chips[chip_id -1];

	#if 0 //debug
	if (1 == chip_id)
	{
		uint32_t diff6 = *((uint32_t *)(work->target+24));
		u6_change_target(achain, diff6);
	}
	#endif
	

	if (chip->work[0] != NULL)
	{
		if (chip->work[1] != NULL)
		{
			work_completed(achain->cgpu, chip->work[1]);
		}
		chip->work[1] = chip->work[0];
		chip->work[0] = NULL;
	}
	uint8_t *jobdata = create_job(work, chip_id, 0);
	if (!cmd_write_job(achain, chip_id, jobdata))
	{
		work_completed(achain->cgpu, work);
		chip->work[0] = NULL;
		ret = false;
	}
	else
	{
		chip->work[0] = work;
		ret = true;
	}


	//applog(LOG_ERR, "1--chip_%d, chipAddr=%08x, work0-1:%08x %08x", 
	//			chip_id, chip, chip->work[0], chip->work[1]);

	return ret;
}

static int64_t u6_scanwork(struct thr_info *thr)
{	
	struct cgpu_info *cgpu = thr->cgpu;
	struct stu_chain *achain = cgpu->device_data;
	struct timeval time_now;
	uint32_t nonce;
	uint16_t extra_nonce;
	char str_ntime[12]={0};
	uint8_t chip_ntime;
	uint32_t new_ntime;
	uint8_t chip_id;
	uint8_t job_id;
	int i, j;
	bool work_updated = false;
	uint64_t accept = 0;
	uint32_t t_need_reset_low_hash_sec;
	double dev_runtime;
	uint8_t whichpll=0;
	uint8_t upDown=0;
	uint32_t chain_average_pll[3] = {0};
	uint8_t regBuff[12];
	uint8_t core_id;
	uint32_t temp_nonce;
	struct stu_chip *chip;
	bool if_has_nonce = false;
	uint32_t t_ms;
	uint32_t t_sec;
	bool ret;
//	float temp_max;
	if(!if_hashboard_cooldown){
		return 0;
	}

	if (waittemp){
		printf("%f: %f, %f, %f\n", g_board_maxtemp, g_board_temp[0], g_board_temp[1], g_board_temp[2]);
		if((60 < g_board_maxtemp) || ((g_board_temp[0] + g_board_temp[1] + g_board_temp[2]) > 70))
		{
			printf("waittemp-------------------------------------- = %d\n", waittemp);
			waittemp = 0;
		}
		cgsleep_ms(3000);
		return 0;
	}
	
	
	mutex_lock(&achain->lock);
	
#if 1 //get nonce
	for (j=0; j<2; j++)
	{

		if (!get_nonce(achain, (uint8_t*)&nonce, &chip_id,  &chip_ntime))
			break;

		//applog(LOG_ERR, "chip_%d, job_id_%d,get nonce  = 0x%08x, extra nonce = 0x%04x", chip_id, job_id,swab32(nonce), extra_nonce);

		if (chip_id < 1 ||chip_id > achain->num_active_chips)
		{
			applog(LOG_ERR, "chain_%d, wrong chip_id %d", achain->chain_id, chip_id);
			continue;
		}
		
		work_updated = true;
		if_has_nonce = true;

		chip = &achain->chips[chip_id-1];
		struct work *work = chip->work[0];

		temp_nonce = nonce;

		core_id = (temp_nonce&0x0000f000)>>13;		
		if (core_id>=6)
		{
			applog(LOG_ERR, "wrong core id !");
			//core_id = 6;
			continue;
		}

		if (work == NULL) 
		{
			/* already been flushed => stale */
			applog(LOG_WARNING, "chip stale nonce ");
			chip->stales++;
			continue;
		}

		#if 1
		uint32_t *pnt = (uint32_t *)(work->data+68);
		free(work->ntime);
		new_ntime = swab32(swab32(work->save_ntime)+chip_ntime);
		snprintf(str_ntime, 9, "%08x", swab32(new_ntime));

		//applog(LOG_ERR, "o_time=%08x, chip_ntime=%02x", swab32(work->save_ntime), chip_ntime);
		//applog(LOG_ERR, "new ntime=%08x", new_ntime);

		work->ntime = strdup(str_ntime);
		*pnt = new_ntime;
		#endif

		work->chipid  = chip_id;
		uint32_t  *pdata = (uint32_t  *)(work->data);
		pdata[19] = ((nonce&0x0000ffff)<<16)|((nonce&0xffff0000)>>16);

		
		if (!submit_nonce(thr, work, swab32(nonce))) 
		{
			struct work *work = chip->work[1];

			if (work == NULL)
			{
				continue;
			}

			uint32_t *pnt = (uint32_t *)(work->data+68);
			free(work->ntime);
			new_ntime = swab32(swab32(work->save_ntime)+chip_ntime);
			snprintf(str_ntime, 9, "%08x", swab32(new_ntime));

			work->ntime = strdup(str_ntime);
			*pnt = new_ntime;
			work->chipid  = chip_id;
			uint32_t  *pdata = (uint32_t  *)(work->data);
			pdata[19] = ((nonce&0x0000ffff)<<16)|((nonce&0xffff0000)>>16);

			if (!submit_nonce(thr, work, swab32(nonce)))
			{
				chip->hw_errors++;
				chip->core_hwe[core_id]++;
				achain->wrong_nonce++;
				chip->hwe_5min++;
				if (achain->update_pll_finish)
					chip->hwe_10min++;
				continue;
			}
			applog(LOG_ERR, "recheck pass !\n");
			hw_errors--;
			cgpu->hw_errors--;
		}

		accept +=8.0;
		//accept += (int64_t)work->sdiff;
		chip->chip_nonces_found++;
		chip->core_acc[core_id]++;
		chip->acc_5min++;
		achain->right_nonce++;
		if (achain->update_pll_finish)
			chip->acc_10min++;
		cgtime(&achain->last_get_nonce_t);
	}
#endif

	//reconfig_hash_board(achain);
	if (unlikely(if_has_nonce&&enlog))
	{
		print_core_nonce_info(achain, chip, chip_id);
	}
	
	cgtime(&time_now);
	 t_ms = ms_tdiff(&time_now, &achain->last_set_work_t);
	#if 1 //定时更新任务
	if (t_ms>20*1000 || achain->need_flush_job)
	{
			struct work *work;
			work_updated = true;
			applog(LOG_ERR, "%s update work t=%d", basename(achain->devname), t_ms/1000);

			for (i=0; i<achain->num_active_chips; i++)
			{
				work = wq_dequeue(&achain->active_wq);
				if (work != NULL) 
				{
			    		achain->need_flush_job = false;
					if (set_work(achain, i+1, work)) 
					{
					}
				}
			}
			achain->set_job_cnt++;
			copy_time(&achain->last_set_work_t, &time_now);

	}
	#endif

	#if 1 // 分步启动cores,广播
	cgtime(&time_now);
	t_sec = time_now.tv_sec - achain->last_update_core_t.tv_sec;
	
	if ((achain->last_update_core_num < 7) && (t_sec>2))
	{

		applog(LOG_ERR, "core_num = %d", achain->last_update_core_num);

		cmd_write_register_7(achain->spi_ctx, ADDR_BROADCAST, achain->last_update_core_num);
		
		cmd_write_register_6(achain->spi_ctx, ADDR_BROADCAST, 0,1,
									1, 5,  100);
		
		cgtime(&achain->last_update_core_t);

		if (achain->last_update_core_num  == 6)
		{
			achain->core_all_boot = true;
			achain->last_reboot_core_num = 1;
			achain->start_update_pll = true;
			cgtime(&achain->last_reboot_core_t);
			cgtime(&achain->last_update_pll_t);
			cgtime(&achain->last_ft_t);
			applog(LOG_ERR, "start update pll, newpll = %d finalpll=%d", achain->newpll, achain->finalpll);
		}
		achain->last_update_core_num++;
	}
	#endif

	#if 1 //切换PLL(pll0<->pll1)  升频
	cgtime(&time_now);
	t_ms = (uint32_t)ms_tdiff(&time_now, &achain->last_switchPll_t);

	if ((!achain->update_pll_finish)&&(achain->start_update_pll)&&(t_ms>500)&&(achain->newpll <= achain->finalpll)/*&&(g_board_temp[achain->chain_id]>40)*/)
	{
		applog(LOG_ERR, "%s temperature=%0.1f", achain->devname, g_board_temp[achain->chain_id]);
		applog(LOG_ERR, "switch core = %d, pll=%d", achain->last_switchCore_num, 
															(achain->newpll));

		reconfig_hash_board(achain);
		
		if (0 == achain->switch_direct)
			cmd_switch_pll0_to_pll1(achain->spi_ctx,  ADDR_BROADCAST,  achain->last_switchCore_num);
		else
			cmd_switch_pll1_to_pll0(achain->spi_ctx, ADDR_BROADCAST,  achain->last_switchCore_num);	

		cmd_write_register_6(achain->spi_ctx, ADDR_BROADCAST, 0,1,
									1, 5,  100);
		
		achain->last_switchCore_num++;
		cgtime(&achain->last_switchPll_t);
		
		if (achain->last_switchCore_num>6)
		{
			achain->last_switchCore_num = 1;

			if (achain->newpll >=achain->finalpll)
			{
				if (!achain->switch_direct)
				{
					//如果
					//升频完成后所有core工作在PLL1,现切换到PLL0

					applog(LOG_ERR, "swich all core from pll1 to pll0!!!!!!!!!");
					cmd_update_pll0(achain->spi_ctx, ADDR_BROADCAST, achain->newpll);
					for (int i=1; i<7; i++)
					{
						cmd_switch_pll1_to_pll0(achain->spi_ctx, ADDR_BROADCAST,  i);
						cmd_write_register_6(achain->spi_ctx, ADDR_BROADCAST, 0,1,
									1, 5,  100);
						cgsleep_ms(500);
					}
				}
				for (int i=0; i<ASIC_CHIP_NUM; i++)
				{
					achain->chips[i].chipNewpll = achain->newpll;
				}
				
				//disable_core(achain->spi_ctx, ADDR_BROADCAST, 1);  //关闭core3
				//cmd_write_register_6(achain->spi_ctx, ADDR_BROADCAST, 0,1,
				//					1, 5,  100);
				
				cmd_update_pll1(achain->spi_ctx, ADDR_BROADCAST, pll1);
				applog(LOG_ERR, "upgrade pll done!all core work at %dM", achain->newpll);
				cgtime(&achain->last_ft_t);
				achain->last_ft_chipnum = 1;
				achain->update_pll_finish = true;
				cgtime(&achain->last_update_pll_t);
				cgtime(&achain->update_pll_finish_t);
				cgtime(&achain->last_checkCrc_t);
				cmd_auto_address(achain->spi_ctx, ADDR_BROADCAST);
			}
			else
			{	
				achain->newpll +=STEP;
				//下面的连个范围内的pll有电流跳变问题，暂时跳过
				#if 0
				if ((achain->newpll>=250)&&(achain->newpll<=320))
					achain->newpll = 330;
				if ((achain->newpll>=390)&&(achain->newpll<=420))
					achain->newpll = 430;
				#endif
				
				if (0 == achain->switch_direct)
				{
					applog(LOG_ERR, "start switch direction 1->0, set pll0=%d", achain->newpll);
					achain->switch_direct = 1;
					cmd_update_pll0(achain->spi_ctx, ADDR_BROADCAST, achain->newpll);
				}
				else
				{
					applog(LOG_ERR, "start switch direction 0->1, set pll1=%d", achain->newpll);
					achain->switch_direct = 0;
					cmd_update_pll1(achain->spi_ctx, ADDR_BROADCAST, achain->newpll);
				}

				cmd_write_register_6(achain->spi_ctx, ADDR_BROADCAST, 0,1,
										1, 5,  100);
				
			}
			
		}
	}

	#endif

	#if 1 //跟据错误率调频

		cgtime(&time_now);
		t_sec = time_now.tv_sec - achain->last_update_pll_t.tv_sec;
		if ((!achain->need_autopll) && achain->update_pll_finish && (t_sec>300))
		{
			achain->need_autopll=true;
		}
		if ((t_sec>8) && achain->need_autopll)
		{
			chip = &achain->chips[achain->last_autopll_chip-1];
			
			float WrongPerc;
			float right = chip->acc_10min;
			float wrong = chip->hwe_10min;
			if (0.0 == (right+wrong))
				WrongPerc=1; 
			else
				WrongPerc = wrong/(right+wrong);

			applog(LOG_ERR, "%s chip_%d acc_10min=%d hwe_10min=%d", 
					achain->devname, achain->last_autopll_chip,chip->acc_10min, chip->hwe_10min);

			if (chip->chipNewpll >= pll0)
			{
				if (WrongPerc < gt_uppercent) // 1% error
				{
					if (chip->chipNewpll < upperpll)
					{
						 chip->chipNewpll +=gt_upstep;
						applog(LOG_ERR, "%s, chip_%d errorPercent(%0.1f%%) +%dM newpll=%d", achain->devname,achain->last_autopll_chip , 
									WrongPerc*100, gt_upstep,chip->chipNewpll);
						cmd_update_pll0(achain->spi_ctx,  achain->last_autopll_chip,  chip->chipNewpll);
					}
				}

				if (WrongPerc > gt_lowpercent)  // 92% error
				{
					if (chip->chipNewpll > lowerpll)
					{
						chip->chipNewpll -=gt_downstep;
						applog(LOG_ERR, "%s, chip_%d errorPercent(%0.1f%%) -%dM newpll=%d", achain->devname,achain->last_autopll_chip , 
														WrongPerc*100,gt_downstep, chip->chipNewpll);
						cmd_update_pll0(achain->spi_ctx,  achain->last_autopll_chip,  chip->chipNewpll);
					}
				}
			}	
			else
			{
				if (WrongPerc < lt_uppercent) // 1% error
				{
					if (chip->chipNewpll < upperpll)
					{
						 chip->chipNewpll +=lt_upstep;
						applog(LOG_ERR, "%s, chip_%d error(%0.1f%%) +%dM newpll=%d", achain->devname,achain->last_autopll_chip , 
									WrongPerc*100, lt_upstep,chip->chipNewpll);
						cmd_update_pll0(achain->spi_ctx,  achain->last_autopll_chip,  chip->chipNewpll);
					}
				}

				if (WrongPerc > lt_lowpercent)  // 92% error
				{
					if (chip->chipNewpll > lowerpll)
					{
						chip->chipNewpll -=lt_downstep;
						if(chip->chipNewpll < lowerpll)
							chip->chipNewpll = lowerpll;
						applog(LOG_ERR, "%s, chip_%d error(%0.1f%%) -%dM newpll=%d", achain->devname,achain->last_autopll_chip , 
														WrongPerc*100,lt_downstep, chip->chipNewpll);
						cmd_update_pll0(achain->spi_ctx,  achain->last_autopll_chip,  chip->chipNewpll);
					}
				}
			}
			
			
			chip->acc_10min=0;
			chip->hwe_10min=0;
			achain->last_autopll_chip++;
			cgtime(&achain->last_update_pll_t);
			if (achain->last_autopll_chip>ASIC_CHIP_NUM)
			{
				achain->last_autopll_chip = 1;
			}
				
		}

	#endif

#if 0 //开通speed功能并定时ft
		cgtime(&time_now);
		t_sec = time_now.tv_sec - achain->update_pll_finish_t.tv_sec;
		if ((!achain->start_speed) &&(t_sec>60)&&achain->update_pll_finish)
		{
			achain->start_speed = true;
			cmd_write_register_2(achain->spi_ctx, ADDR_BROADCAST, 1, 1, 0);
			cmd_write_register_6(achain->spi_ctx, ADDR_BROADCAST, 0,1,1, 5, 100);
		}
		
		t_sec = time_now.tv_sec - achain->last_ft_t.tv_sec;
		if ((t_sec>10) && achain->start_speed)
		{
			cgtime(&achain->last_ft_t);
			cmd_enable_ft(achain->spi_ctx,  ADDR_BROADCAST, 4, 1, 1500);
			cmd_write_register_3(achain->spi_ctx, ADDR_BROADCAST, 0, 1, 10000000);
		}	
#endif

#if 0
	cgtime(&time_now);
	t_sec = time_now.tv_sec - achain->last_vol.tv_sec;
	if (t_sec > 60)
	{
		cgtime(&achain->last_vol);
		temp_max  = get_temp_val();
		if (54 > temp_max)
		{
			if(1300 > stu_board_get_voltage())
			{
				changevol += 0.05;
				stu_board_set_voltage(changevol);
			}
		}
		if (62 < temp_max)
		{
			if(1250 < stu_board_get_voltage())
			{
				changevol -= 0.05;
				stu_board_set_voltage(changevol);
			}
		}
	}

#endif


#if 1 //定时ft
		cgtime(&time_now);
		t_sec = time_now.tv_sec - achain->last_ft_t.tv_sec;
		if ((t_sec>10) && achain->update_pll_finish&&achain->core_all_boot)
		{
			cgtime(&achain->last_ft_t);
			cmd_enable_ft(achain->spi_ctx,  achain->last_ft_chipnum, 4, 1, 1500);
			ret = cmd_read_register(achain->spi_ctx, achain->last_ft_chipnum, 9, 12, regBuff);
			cmd_write_register_3(achain->spi_ctx, achain->last_ft_chipnum, 0, 1, 10000000);
			if (ret)
			{
				uint8_t passCores = (regBuff[4]>>4)&0x0f;
				achain->total_ft_pass_core -= achain->chips[achain->last_ft_chipnum-1].ft_pass_cnt;
				achain->total_ft_pass_core += passCores;
				achain->chips[achain->last_ft_chipnum-1].ft_pass_cnt = passCores;
				applog(LOG_ERR, "%s  ft passCore=%d chip_%d, total_ft_pass=%d ",achain->devname,  passCores, achain->last_ft_chipnum, achain->total_ft_pass_core);
			}
			
#if 0	    //跟据ft的结果调频		
			applog(LOG_ERR, "%s, chip_%d, pll=%d", achain->devname, achain->last_ft_chipnum, achain->chips[achain->last_ft_chipnum-1].chipNewpll);
			if (ret)
			{

				//if (regBuff[2]&0x0f)//ft pass
				if (passCores>=4)
				{
					if (achain->chips[achain->last_ft_chipnum-1].chipNewpll<700)
					{
						if (4 == passCores)
						{
							applog(LOG_ERR, "%s, chip_%d, +5M, pll=%d", achain->devname, achain->last_ft_chipnum, achain->chips[achain->last_ft_chipnum-1].chipNewpll);
							achain->chips[achain->last_ft_chipnum-1].chipNewpll+=5;
						}
						else if (passCores>=5)
						{
							applog(LOG_ERR, "%s, chip_%d, +10M, pll=%d", achain->devname, achain->last_ft_chipnum, achain->chips[achain->last_ft_chipnum-1].chipNewpll);
							achain->chips[achain->last_ft_chipnum-1].chipNewpll+=10;
						}
						cmd_update_pll0(achain->spi_ctx,  achain->last_ft_chipnum,  achain->chips[achain->last_ft_chipnum-1].chipNewpll);
					}
				}
				else
				{
					if (achain->chips[achain->last_ft_chipnum-1].chipNewpll>500)
					{
						if (3 == passCores)
						{
							applog(LOG_ERR, "%s, chip_%d,  -5M, pll=%d", achain->devname, achain->last_ft_chipnum, achain->chips[achain->last_ft_chipnum-1].chipNewpll);
							achain->chips[achain->last_ft_chipnum-1].chipNewpll -= 5;
						}
						else if (passCores<=2)
						{
							applog(LOG_ERR, "%s, chip_%d,  -20M, pll=%d", achain->devname, achain->last_ft_chipnum, achain->chips[achain->last_ft_chipnum-1].chipNewpll);
							achain->chips[achain->last_ft_chipnum-1].chipNewpll -= 20;
						}
						cmd_update_pll0(achain->spi_ctx,  achain->last_ft_chipnum,  achain->chips[achain->last_ft_chipnum-1].chipNewpll);
					}
				}
			}
#endif			

			achain->last_ft_chipnum++;
			if (achain->last_ft_chipnum>ASIC_CHIP_NUM)
			{
				achain->last_ft_chipnum = 1;
			}
		}
#endif


#if 1 //打印每颗芯片的错误率
	if (unlikely(enlog))
	{
		cgtime(&time_now);
		t_sec = time_now.tv_sec - achain->last_log_t.tv_sec;
		if (t_sec >300)
		{
			print_chip_nonceInfo(achain);
			cgtime(&achain->last_log_t);
		}
	}
			
#endif

#if 0 //依次重启core
	cgtime(&time_now);
	t_sec = time_now.tv_sec - achain->last_reboot_core_t.tv_sec;

	if ((achain->core_all_boot)&&(achain->last_reboot_core_num < 7) && (t_sec>300))
	{
		applog(LOG_ERR, "reboot core = %d", achain->last_reboot_core_num);
		cmd_core_reboot(achain->spi_ctx, ADDR_BROADCAST, achain->last_reboot_core_num);
		cmd_write_register_6(achain->spi_ctx, ADDR_BROADCAST, 0,1,
									1, 5,  100);

		achain->last_reboot_core_num++;
		cgtime(&achain->last_reboot_core_t);
		if (achain->last_reboot_core_num >= 6)
			achain->last_reboot_core_num = 1;
	}
#endif	
	
	//低算力判断
#if 1
	cgtime(&time_now);
	t_sec = time_now.tv_sec - achain->last_get_nonce_t.tv_sec;
	
	#define NO_NONCE_RESET_T	300 
	if ((t_sec > NO_NONCE_RESET_T) && achain->update_pll_finish)
	{
		printf("%s no nonce cause reset hashboard!\n", basename(achain->devname));
		reset_hashboard_log("5_min_no_nonce_reset", achain->devname);
		reset_hash_board(achain);
	}

	//cgpu->rolling15 cgpu->rolling5的单位是Mhash/s, 升频完成后1800s
	cgtime(&time_now);
	t_sec = time_now.tv_sec - achain->update_pll_finish_t.tv_sec;
	if ((t_sec>1800) && (cgpu->rolling15 < 50000.0)&&(cgpu->rolling5 < 50000.0))
	{
		printf("%s low hashrate reset hashboard!\n", basename(achain->devname));
		reset_hashboard_log("low_hash_reset", achain->devname);
		reset_hash_board(achain);
	}
#endif	

	//判断crc出错率重启
#if 1
		cgtime(&time_now);
		t_sec = time_now.tv_sec - achain->last_checkCrc_t.tv_sec;
		if (achain->update_pll_finish && t_sec>300)//5分钟检查一次
		{
			cgtime(&achain->last_checkCrc_t);			
			if (!ifcheck_crc_pass(achain))
			{
				applog(LOG_ERR, "too many crc error cause reset hashboard!");
				reset_hashboard_log("crcError_hash_reset", achain->devname);
				reset_hash_board(achain);
			}
			achain->set_job_cnt = 0;
		}
#endif

	//定时刷新寄存器
#if 1
	cgtime(&time_now);
	t_sec = time_now.tv_sec - achain->last_confReg_t.tv_sec;
	if (t_sec>120)
	{
		cgtime(&achain->last_confReg_t);
		reconfig_hash_board(achain);
	}

#endif
	
	/* in case of no progress, prevent busy looping */
	if (!work_updated)
	{
		cgsleep_ms(10);
	}
	mutex_unlock(&achain->lock);

	
	return ((int64_t) accept<< 32);
	
}

static bool u6_queue_full(struct cgpu_info *cgpu)
{
	struct stu_chain *achain = cgpu->device_data;
	int queue_full = false;

	mutex_lock(&achain->lock);

	if (achain->active_wq.num_elems >=  achain->num_active_chips)
		queue_full = true;
	else
		wq_enqueue(&achain->active_wq, get_queued(cgpu));

	mutex_unlock(&achain->lock);

	return queue_full;
}

static void u6_flush_work(struct cgpu_info *cgpu)
{
	struct stu_chain *achain = cgpu->device_data;

	mutex_lock(&achain->lock);

	while (achain->active_wq.num_elems > 0) 
	{
		struct work *work = wq_dequeue(&achain->active_wq);
		assert(work != NULL);
		work_completed(cgpu, work);
	}

	mutex_unlock(&achain->lock);

	achain->need_flush_job = true;
}


struct device_drv u6_drv = {	
	.drv_id = DRIVER_u6,
	.dname = "u6",
	.name = "u6",
	.drv_detect = u6_detect_chain,
	.hash_work = hash_queued_work, 
	.scanwork = u6_scanwork,
	.queue_full = u6_queue_full,
	.update_work = u6_flush_work,
};

