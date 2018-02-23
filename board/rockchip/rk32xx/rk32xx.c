/*
 * (C) Copyright 2008-2015 Fuzhou Rockchip Electronics Co., Ltd
 * Peter, Software Engineering, <superpeter.cai@gmail.com>.
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */
#include <common.h>
#include <version.h>
#include <errno.h>
#include <fastboot.h>
#include <fdtdec.h>
#include <fdt_support.h>
#include <power/pmic.h>

#include <asm/io.h>
#include <asm/arch/rkplat.h>

#include "../common/config.h"

enum project_id {
	TinkerBoardS = 0,
	TinkerBoard  = 7,
};

enum pcb_id {
	SR,
	ER,
	PR,
};

extern bool force_ums;

DECLARE_GLOBAL_DATA_PTR;
static ulong get_sp(void)
{
	ulong ret;

	asm("mov %0, sp" : "=r"(ret) : );
	return ret;
}

void board_lmb_reserve(struct lmb *lmb) {
	ulong sp;
	sp = get_sp();
	debug("## Current stack ends at 0x%08lx ", sp);

	/* adjust sp by 64K to be safe */
	sp -= 64<<10;
	lmb_reserve(lmb, sp,
			gd->bd->bi_dram[0].start + gd->bd->bi_dram[0].size - sp);

	//reserve 48M for kernel & 8M for nand api.
	lmb_reserve(lmb, gd->bd->bi_dram[0].start, CONFIG_LMB_RESERVE_SIZE);
}

int board_storage_init(void)
{
	int ret = 0;

	if (StorageInit() == 0) {
		printf("storage init OK!\n");
		ret = 0;
	} else {
		printf("storage init fail!\n");
		ret = -1;
	}

	return ret;
}


/*****************************************
 * Routine: board_init
 * Description: Early hardware init.
 *****************************************/
int board_init(void)
{
	/* Set Initial global variables */

	gd->bd->bi_arch_number = MACH_TYPE_RK30XX;
	gd->bd->bi_boot_params = PHYS_SDRAM + 0x88000;

	return 0;
}


#ifdef CONFIG_DISPLAY_BOARDINFO
/**
 * Print board information
 */
int checkboard(void)
{
	puts("Board:\tRockchip platform Board\n");
#ifdef CONFIG_SECOND_LEVEL_BOOTLOADER
	printf("Uboot as second level loader\n");
#endif
	return 0;
}
#endif


#ifdef CONFIG_ARCH_EARLY_INIT_R
int arch_early_init_r(void)
{
	debug("arch_early_init_r\n");

	 /* set up exceptions */
	interrupt_init();
	/* enable exceptions */
	enable_interrupts();

	/* rk pl330 dmac init */
#ifdef CONFIG_RK_PL330_DMAC
	rk_pl330_dmac_init_all();
#endif /* CONFIG_RK_PL330_DMAC */

#ifdef CONFIG_RK_PWM_REMOTE
	RemotectlInit();
#endif

	return 0;
}
#endif


#define RAMDISK_ZERO_COPY_SETTING	"0xffffffff=n\0"
static void board_init_adjust_env(void)
{
	bool change = false;

	char *s = getenv("bootdelay");
	if (s != NULL) {
		unsigned long bootdelay = 0;

		bootdelay = simple_strtoul(s, NULL, 16);
		debug("getenv: bootdelay = %lu\n", bootdelay);
#if (CONFIG_BOOTDELAY <= 0)
		if (bootdelay > 0) {
			setenv("bootdelay", simple_itoa(0));
			change = true;
			debug("setenv: bootdelay = 0\n");
		}
#else
		if (bootdelay != CONFIG_BOOTDELAY) {
			setenv("bootdelay", simple_itoa(CONFIG_BOOTDELAY));
			change = true;
			debug("setenv: bootdelay = %d\n", CONFIG_BOOTDELAY);
		}
#endif
	}

	s = getenv("bootcmd");
	if (s != NULL) {
		debug("getenv: bootcmd = %s\n", s);
		if (strcmp(s, CONFIG_BOOTCOMMAND) != 0) {
			setenv("bootcmd", CONFIG_BOOTCOMMAND);
			change = true;
			debug("setenv: bootcmd = %s\n", CONFIG_BOOTCOMMAND);
		}
	}

	s = getenv("initrd_high");
	if (s != NULL) {
		debug("getenv: initrd_high = %s\n", s);
		if (strcmp(s, RAMDISK_ZERO_COPY_SETTING) != 0) {
			setenv("initrd_high", RAMDISK_ZERO_COPY_SETTING);
			change = true;
			debug("setenv: initrd_high = %s\n", RAMDISK_ZERO_COPY_SETTING);
		}
	}

	if (change) {
#ifdef CONFIG_CMD_SAVEENV
		debug("board init saveenv.\n");
		saveenv();
#endif
	}
}

void usb_current_limit_ctrl(bool unlock_current)
{
	printf("%s: unlock_current = %d\n", __func__, unlock_current);

	if(unlock_current == true)
		gpio_direction_output(GPIO_BANK6|GPIO_A6, 1);
	else
		gpio_direction_output(GPIO_BANK6|GPIO_A6, 0);
}

/*
*
* eMMC maskrom mode : GPIO6_A7 (H:disable maskrom, L:enable maskrom)
*
*/
void rk3288_maskrom_ctrl(bool enable_emmc)
{
	printf("%s: enable_emmc = %d\n", __func__, enable_emmc);

	if(enable_emmc == true)
		gpio_direction_output(GPIO_BANK6|GPIO_A7, 1);
	else
		gpio_direction_output(GPIO_BANK6|GPIO_A7, 0);

	mdelay(10);
}

void check_force_enter_ums_mode(void)
{
	enum pcb_id pcbid;
	enum project_id projectid;

	// enable projectid pull down and set it to input
	gpio_pull_updown(GPIO_BANK2|GPIO_A1, GPIOPullUp);
	gpio_pull_updown(GPIO_BANK2|GPIO_A2, GPIOPullUp);
	gpio_pull_updown(GPIO_BANK2|GPIO_A3, GPIOPullUp);
	gpio_direction_input(GPIO_BANK2|GPIO_A1);
	gpio_direction_input(GPIO_BANK2|GPIO_A2);
	gpio_direction_input(GPIO_BANK2|GPIO_A3);

	// set pcbid to input
	gpio_direction_input(GPIO_BANK2|GPIO_B0);

	// disable SDP pull up/down and set it to input
	gpio_pull_updown(GPIO_BANK6|GPIO_A5, PullDisable);
	gpio_direction_input(GPIO_BANK6|GPIO_A5);

	mdelay(10);

	// read project id
	projectid = gpio_get_value(GPIO_BANK2|GPIO_A1) | gpio_get_value(GPIO_BANK2|GPIO_A2)<<1 | gpio_get_value(GPIO_BANK2|GPIO_A3)<<2;

	// read pcbid
	pcbid = gpio_get_value(GPIO_BANK2|GPIO_B0) | gpio_get_value(GPIO_BANK2|GPIO_B1)<<1 | gpio_get_value(GPIO_BANK2|GPIO_B2)<<2;

	// only Tinker Board S and the PR stage PCB has this function
	if(projectid!=TinkerBoard && pcbid >= ER){
		printf("PC event = 0x%x\n", gpio_get_value(GPIO_BANK6|GPIO_A5));
		if(gpio_get_value(GPIO_BANK6|GPIO_A5) == 1){
			// SDP detected, enable EMMC and unlock usb current limit
			printf("usb connected to SDP, force enter ums mode\n");
			force_ums = true;
			// unlock usb current limit and re-enable EMMC
			usb_current_limit_ctrl(true);
			rk3288_maskrom_ctrl(true);
			mdelay(10);
		}
	}
}

#ifdef CONFIG_BOARD_LATE_INIT
extern char bootloader_ver[24];
int board_late_init(void)
{
	debug("board_late_init\n");

	board_init_adjust_env();

	load_disk_partitions();

	debug("rkimage_prepare_fdt\n");
	rkimage_prepare_fdt();

#ifdef CONFIG_RK_KEY
	debug("key_init\n");
	key_init();
#endif

#ifdef CONFIG_RK_POWER
	debug("pmic_init\n");
	pmic_init(0);
#if defined(CONFIG_POWER_PWM_REGULATOR)
	debug("pwm_regulator_init\n");
	pwm_regulator_init();
#endif
	debug("fg_init\n");
	fg_init(0); /*fuel gauge init*/
#endif /* CONFIG_RK_POWER */

#if defined(CONFIG_RK_DCF)
	dram_freq_init();
#endif

	debug("idb init\n");
	//TODO:set those buffers in a better way, and use malloc?
	rkidb_setup_space(gd->arch.rk_global_buf_addr);

	/* after setup space, get id block data first */
	rkidb_get_idblk_data();

	/* Secure boot check after idb data get */
	SecureBootCheck();

	if (rkidb_get_bootloader_ver() == 0) {
		printf("\n#Boot ver: %s\n", bootloader_ver);
	}

	char tmp_buf[32];
	/* rk sn size 30bytes, zero buff */
	memset(tmp_buf, 0, 32);
	if (rkidb_get_sn(tmp_buf)) {
		setenv("fbt_sn#", tmp_buf);
	}

	debug("fbt preboot\n");
	board_fbt_preboot();

	return 0;
}
#endif

