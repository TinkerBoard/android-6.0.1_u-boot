/********************************************************************************
		COPYRIGHT (c)   2013 BY ROCK-CHIP FUZHOU
			--  ALL RIGHTS RESERVED  --
File Name:	
Author:         
Created:        
Modified:
Revision:       1.00
********************************************************************************/
#include <config.h>
#include <common.h>
#include <command.h>
#include <mmc.h>
#include <part.h>
#include <malloc.h>
#include <linux/list.h>
#include <div64.h>
#include "rkmmc.h"
//#include <typedef.h>
/* Set block count limit because of 16 bit register limit on some hardware*/
#ifndef CONFIG_SYS_MMC_MAX_BLK_COUNT
#define CONFIG_SYS_MMC_MAX_BLK_COUNT 65535
#endif
static int mmcwaitbusy(void)
{
  int count;
  /* Wait max 100 ms */
  count = MAX_RETRY_COUNT;
  /* before reset ciu, it should check DATA0. if when DATA0 is low and
     it resets ciu, it might make a problem */
  while ((Readl ((gMmcBaseAddr + MMC_STATUS)) & MMC_BUSY)){
    if(count == 0){
      return -1;
    }
    count--;
    udelay(1);
  }
  return 0;
}
static int  mci_send_cmd(u32 cmd, u32 arg)
{

	unsigned int cmd_status = 0;
	volatile unsigned int RetryCount = 0;
	RetryCount = 1000;
	Writel(gMmcBaseAddr + MMC_CMD, cmd);
	while ((Readl(gMmcBaseAddr + MMC_CMD) & MMC_CMD_START) && (RetryCount > 0)){
		udelay(1);
		RetryCount--;
	}
	 if(RetryCount == 0)
		return -1;
}
static void emmcpoweren(char En)
{
	if(En){
		Writel(gMmcBaseAddr + MMC_PWREN, 1);
		Writel(gMmcBaseAddr + MMC_RST_N, 1);
	}
	else{
		Writel(gMmcBaseAddr + MMC_PWREN, 0);
		Writel(gMmcBaseAddr + MMC_RST_N, 0);
	}
}


static void emmcreset()
{
   
  	 int data;
	  data = ((1<<16)|(1))<<3;
	  Writel(gCruBaseAddr + 0x1d8, data);
	  DRVDelayUs(100);
	  data = ((1<<16)|(0))<<3;
	  Writel(gCruBaseAddr + 0x1d8, data);
	  DRVDelayUs(200);
	  emmcpoweren(1);
   
}

static void emmc_dev_reset(void)
{
	emmcpoweren(0);
	DRVDelayMs(5);	
	emmcpoweren(1);
	DRVDelayMs(1);
}
static void emmc_gpio_init()
{
	Writel(gGrfBaseAddr + 0x20,0xffffaaaa);
	Writel(gGrfBaseAddr + 0x24,0x000c0008);
	Writel(gGrfBaseAddr + 0x28,0x003f002a);
}

static int rk_emmc_init(struct mmc *mmc)
{
	int timeOut;
	emmc_dev_reset();
	emmcreset();
	emmc_gpio_init();
	Writel(gMmcBaseAddr + MMC_CTRL, MMC_CTRL_RESET | MMC_CTRL_FIFO_RESET);
	Writel(gMmcBaseAddr + MMC_PWREN,1);
	while ((Readl(gMmcBaseAddr + MMC_CTRL) & (MMC_CTRL_FIFO_RESET | MMC_CTRL_RESET)) && (timeOut > 0))
	{
		DRVDelayUs(1);
		timeOut--;
	}
	if(timeOut == 0)
		return -1;
	Writel(gMmcBaseAddr + MMC_RINTSTS, 0xFFFFFFFF);/* Clear the interrupts for the host controller */
	Writel(gMmcBaseAddr + MMC_INTMASK, 0); /* disable all mmc interrupt first */
	Writel(gMmcBaseAddr + MMC_TMOUT, 0xFFFFFFFF);/* Put in max timeout */
	Writel(gMmcBaseAddr + MMC_FIFOTH, (0x3 << 28) |((FIFO_DETH/2 - 1) << 16) | ((FIFO_DETH/2) << 0));
	Writel(gMmcBaseAddr + MMC_CLKSRC, 0);
	return 0;
}
static u32 rk_mmc_prepare_command(struct mmc *mmc, struct mmc_cmd *cmd,struct mmc_data *data)
{
	
	u32 cmdr;
	cmdr = cmd->cmdidx;
	
	cmdr |= MMC_CMD_PRV_DAT_WAIT;

	if (cmd->resp_type & MMC_RSP_PRESENT) {
		/* We expect a response, so set this bit */
		cmdr |= MMC_CMD_RESP_EXP;
		if (cmd->resp_type & MMC_RSP_136)
			cmdr |= MMC_CMD_RESP_LONG;
	}
	if (cmd->resp_type & MMC_RSP_CRC)
		cmdr |= MMC_CMD_RESP_CRC;
	if (data) {
		cmdr |= MMC_CMD_DAT_EXP;
		if (data->flags & MMC_DATA_WRITE)
			cmdr |= MMC_CMD_DAT_WR;
	}
	return cmdr;
}
static int rk_mmc_start_command(struct mmc *mmc,
				  struct mmc_cmd *cmd, u32 cmd_flags)
{
	unsigned int RetryCount = 0;
	unsigned int time_out = 10000;
	Writel(gMmcBaseAddr + MMC_CMDARG, cmd->cmdarg);
	Writel(gMmcBaseAddr + MMC_CMD, cmd_flags | MMC_CMD_START | MMC_USE_HOLD_REG);
	udelay(1);
	for (RetryCount; RetryCount<MAX_RETRY_COUNT; RetryCount++) {
		if(Readl(gMmcBaseAddr + MMC_RINTSTS) & MMC_INT_CMD_DONE){
			Writel(gMmcBaseAddr + MMC_RINTSTS, MMC_INT_CMD_DONE);
			break;
		}
		udelay(1);
	}
	if (RetryCount == MAX_RETRY_COUNT) {
		printf("Emmc::EmmcSendCmd failed, Cmd: 0x%08x, Arg: 0x%08x\n", cmd_flags, cmd->cmdarg);
		return COMM_ERR;
	}
	if(Readl(gMmcBaseAddr + MMC_RINTSTS) & MMC_CMD_RES_TIME_OUT){
		//printf("Emmc::EmmcSendCmd  Time out error, Cmd: 0x%08x, Arg: 0x%08x, RINTSTS: 0x%08x\n",
		//	cmd_flags,  cmd->cmdarg, (Readl(gMmcBaseAddr + MMC_RINTSTS)));
		Writel(gMmcBaseAddr + MMC_RINTSTS, MMC_CMD_RES_TIME_OUT);
		return TIMEOUT;
	}
	if(Readl(gMmcBaseAddr + MMC_RINTSTS) & MMC_CMD_ERROR_FLAGS) {
		printf("Emmc::EmmcSendCmd error, Cmd: 0x%08x, Arg: 0x%08x, RINTSTS: 0x%08x\n",
			cmd_flags,  cmd->cmdarg, (Readl(gMmcBaseAddr + MMC_RINTSTS)));
		Writel(gMmcBaseAddr + MMC_RINTSTS, MMC_CMD_ERROR_FLAGS);
		return COMM_ERR;
	}
	#if 1
	if (cmd->resp_type & MMC_RSP_PRESENT) {
		if (cmd->resp_type & MMC_RSP_136) {
			cmd->response[3] = Readl(gMmcBaseAddr+MMC_RESP0);
			cmd->response[2] = Readl(gMmcBaseAddr+MMC_RESP1);
			cmd->response[1] = Readl(gMmcBaseAddr+MMC_RESP2);
			cmd->response[0] = Readl(gMmcBaseAddr+MMC_RESP3);
		} else {
			cmd->response[0] = Readl(gMmcBaseAddr+MMC_RESP0);
			cmd->response[1] = 0;
			cmd->response[2] = 0;
			cmd->response[3] = 0;
		}
	}
	#endif
	return 0;
	
}
static int EmmcWriteData (void *Buffer, unsigned int Blocks)
{
	int Status;
	unsigned int *DataBuffer = Buffer;
	unsigned int FifoCount=0;
	unsigned int Count=0;
	int data_over_flag = 0;
	unsigned int Size32 = Blocks * BLKSZ / 4;
	while(Size32){
	FifoCount = FIFO_DETH/4 - MMC_GET_FCNT(Readl(gMmcBaseAddr + MMC_STATUS));
	for (Count = 0; Count < FifoCount; Count++)
		Writel((gMmcBaseAddr + MMC_DATA), *DataBuffer++);
	Size32 -= FifoCount;
	if(Readl(gMmcBaseAddr + MMC_RINTSTS) & MMC_DATA_ERROR_FLAGS) {
		printf("Emmc::ReadSingleBlock data error, RINTSTS: 0x%08x\n",(Readl(gMmcBaseAddr + MMC_RINTSTS)));
		Writel(gMmcBaseAddr + MMC_RINTSTS, MMC_DATA_ERROR_FLAGS);
		return -1;
	}
	if(Readl(gMmcBaseAddr + MMC_RINTSTS) & MMC_INT_TXDR) {
		Writel(gMmcBaseAddr + MMC_RINTSTS, MMC_INT_TXDR);
		continue;
	}
	if(Readl(gMmcBaseAddr + MMC_RINTSTS) & MMC_INT_DATA_OVER) {
		Writel(gMmcBaseAddr + MMC_RINTSTS, MMC_INT_DATA_OVER);
		Size32 = 0;
		data_over_flag = 1;
		break;
	}
	}
	if(data_over_flag == 0){
		Count = MAX_RETRY_COUNT;
		while ((!(Readl ((gMmcBaseAddr + MMC_RINTSTS)) & MMC_INT_DATA_OVER))&&Count){
			Count--;
	   		udelay(1);
		}
		if(Count == 0){
			printf("write wait DTO timeout\n");
			return -1;
		}
		else{
			Writel(gMmcBaseAddr + MMC_RINTSTS, MMC_INT_DATA_OVER);
		}
		
	}
	if(mmcwaitbusy()){
		printf("in write wait busy time out\n");
		return -1;
	}
	if(Size32 )
		return -1;
         else
		return 0;
}

static int EmmcReadData (void *Buffer, unsigned int Blocks)
{
	int Status;
	unsigned int *DataBuffer = Buffer;
	unsigned int FifoCount=0;
	unsigned int Count=0;
	int data_over_flag = 0;
	unsigned int Size32 = Blocks * BLKSZ / 4;
	while(Size32){
	if(Readl(gMmcBaseAddr + MMC_RINTSTS) & MMC_DATA_ERROR_FLAGS) {
		printf("Emmc::ReadSingleBlock data error, RINTSTS: 0x%08x\n",(Readl(gMmcBaseAddr + MMC_RINTSTS)));
		Writel(gMmcBaseAddr + MMC_RINTSTS, MMC_DATA_ERROR_FLAGS);
		return -1;
	}
	if(Readl(gMmcBaseAddr + MMC_RINTSTS) & MMC_INT_RXDR) {
		FifoCount = MMC_GET_FCNT(Readl(gMmcBaseAddr + MMC_STATUS));
		for (Count = 0; Count < FifoCount; Count++)
			*DataBuffer++ = Readl(gMmcBaseAddr + MMC_DATA);
		Writel(gMmcBaseAddr + MMC_RINTSTS, MMC_INT_RXDR);
		Size32 -= FifoCount;
	}
	if(Readl(gMmcBaseAddr + MMC_RINTSTS) & MMC_INT_DATA_OVER) {
		for (Count = 0; Count < Size32; Count++){
			*DataBuffer++ = Readl(gMmcBaseAddr + MMC_DATA);
		}
		Writel(gMmcBaseAddr + MMC_RINTSTS, MMC_INT_DATA_OVER);
		Size32 = 0;
		data_over_flag = 1;
		break;
	}
	}
	if(data_over_flag == 0){
		Count = MAX_RETRY_COUNT;
		while ((!(Readl ((gMmcBaseAddr + MMC_RINTSTS)) & MMC_INT_DATA_OVER))&&Count){
			Count--;
	   		udelay(1);
		}
		if(Count == 0){
			printf("read wait DTO timeout\n");
			return -1;
		}
		else{
			Writel(gMmcBaseAddr + MMC_RINTSTS, MMC_INT_DATA_OVER);
		}
		
	}
	if(mmcwaitbusy()){
		printf("in read wait busy time out\n");
		return -1;
	}
	if(Size32 == 0)
	Status = 0;
	else
	Status = -1;

	return Status;
}

static int rk_emmc_request(struct mmc *mmc, struct mmc_cmd *cmd,
		struct mmc_data *data)
{
	u32 cmdflags;
	int ret;
	int Status;
	int value;
	int timeout;
	if (data) {
		Writel(gMmcBaseAddr +MMC_BYTCNT, data->blocksize*data->blocks);
		Writel(gMmcBaseAddr +MMC_BLKSIZ, data->blocksize);
		Writel(gMmcBaseAddr + MMC_CTRL, Readl(gMmcBaseAddr + MMC_CTRL) | MMC_CTRL_FIFO_RESET);
		Writel((gMmcBaseAddr + MMC_INTMASK), 
		  MMC_INT_TXDR | MMC_INT_RXDR | MMC_INT_CMD_DONE | MMC_INT_DATA_OVER | MMC_ERROR_FLAGS);
		 //Wait contrloler ready
		Status = mmcwaitbusy();
		if (Status < 0) {
		      printf("Emmc::EmmcPreTransfer failed, data busy\n");
		      return Status;
  		}
	}
	if(cmd->cmdidx == MMC_CMD_STOP_TRANSMISSION)
	{
		value = Readl(gMmcBaseAddr + MMC_STATUS);
		if (!(value & MMC_FIFO_EMPTY))
		{
			value = Readl(gMmcBaseAddr + MMC_CTRL);
			value |= MMC_CTRL_FIFO_RESET;
			Writel(gMmcBaseAddr + MMC_CTRL,value), 
			Status = mmcwaitbusy();
			if (Status < 0) {
			      printf("Emmc::EmmcPreTransfer failed, data busy\n");
			      return Status;
	  		}
		}
	}
	cmdflags = rk_mmc_prepare_command(mmc, cmd,data);
	if(cmd->cmdidx == 0)
		cmdflags |= MMC_CMD_INIT;
	ret = rk_mmc_start_command(mmc, cmd, cmdflags);
	if(ret)
		return ret;
	if(data){
		if(data->flags == MMC_DATA_READ){
			ret = EmmcReadData(data->dest, data->blocks);
		}
		else if(data->flags == MMC_DATA_WRITE){
			ret = EmmcWriteData(data->src, data->blocks);
		}
	}
	return ret;
}
static void rk_set_ios(struct mmc *mmc)
{
	int cfg = 0;
	int suit_clk_div;
	int src_clk;
	int src_clk_div;
	int second_freq;
	int value;
	switch (mmc->bus_width) {
	case 4:
		cfg = MMC_CTYPE_4BIT;
		break;
	case 8:
		cfg = MMC_CTYPE_8BIT;
		break;
	default:
		cfg = MMC_CTYPE_1BIT;
	}
	if(mmc->clock){
		/* disable clock */
		Writel(gMmcBaseAddr + MMC_CLKENA, 0);
		/* inform CIU */
		mci_send_cmd(MMC_CMD_START |MMC_CMD_UPD_CLK | MMC_CMD_PRV_DAT_WAIT, 0);
		if(mmc->clock > mmc->f_max )
			mmc->clock = mmc->f_max;
		if(mmc->clock < mmc->f_min)
			mmc->clock = mmc->f_min;
		src_clk = rk_get_general_pll()/2; //rk32 emmc src generall pll,emmc automic divide setting freq to 1/2,for get the right freq ,we divide this freq to 1/2
		src_clk_div = src_clk/mmc->clock;
		if(src_clk_div > 0x3e)
			src_clk_div = 0x3e;
		second_freq = src_clk/src_clk_div;
		suit_clk_div = (second_freq/mmc->clock);
	 	if (((suit_clk_div & 0x1) == 1) && (suit_clk_div != 1))
	        		suit_clk_div++;  //make sure this div is even number
	        	if(suit_clk_div == 1)
			value =0;
		else
			 value = (suit_clk_div >> 1);
		/* set clock to desired speed */
		Writel(gMmcBaseAddr + MMC_CLKDIV, value);
		/* inform CIU */
		mci_send_cmd( MMC_CMD_START |MMC_CMD_UPD_CLK | MMC_CMD_PRV_DAT_WAIT, 0);

		rkclk_emmc_set_clk(src_clk_div);
		/* enable clock */
		Writel(gMmcBaseAddr + MMC_CLKENA, MMC_CLKEN_ENABLE);
		/* inform CIU */
		mci_send_cmd(MMC_CMD_START |MMC_CMD_UPD_CLK | MMC_CMD_PRV_DAT_WAIT, 0);
		
	}
	Writel(gMmcBaseAddr + MMC_CTYPE, cfg);
}

int rk_mmc_init()
{
	struct mmc *mmc = NULL;
	mmc = malloc(sizeof(struct mmc));
	if (!mmc)
		return -1;
	sprintf(mmc->name, "rk emmc");
	mmc->send_cmd = rk_emmc_request;
	mmc->set_ios = rk_set_ios;
	mmc->init = rk_emmc_init;;
	mmc->getcd = NULL;
	mmc->getwp = NULL;
	mmc->host_caps = MMC_MODE_8BIT | MMC_MODE_4BIT |MMC_MODE_HS |MMC_MODE_HS_52MHz;

	mmc->voltages = 0x00ff8080;
	mmc->f_max = MMC_BUS_CLOCK/2;
	mmc->f_min = (MMC_BUS_CLOCK+510 -1)/510;

	mmc->b_max = 255;
	mmc->rca = 3;
	mmc_register(mmc);

	return 0;
}
