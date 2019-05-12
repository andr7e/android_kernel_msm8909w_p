/* Copyright (c) 2015-2016 The Linux Foundation. All rights reserved.
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
#define pr_fmt(fmt) "SMB:%s: " fmt, __func__

#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/debugfs.h>
#include <linux/pm_wakeup.h>
#include <linux/spinlock.h>
#include <linux/wakelock.h>
#include <linux/qpnp/qpnp-adc.h>
#include <soc/qcom/socinfo.h>
#include <linux/alarmtimer.h>

static bool is_fake_battery = false;
module_param(is_fake_battery, bool, 0644);

struct smb23x_wakeup_source {
	struct wakeup_source source;
	unsigned long enabled_bitmap;
	spinlock_t ws_lock;
};

enum wakeup_src {
	WAKEUP_SRC_IRQ_POLLING = 0,
	WAKEUP_SRC_MAX,
};
#define WAKEUP_SRC_MASK (~(~0 << WAKEUP_SRC_MAX))

struct smb23x_chip {
	struct i2c_client		*client;
	struct device			*dev;

	/* charger configurations  -- via devicetree */
	bool				cfg_charging_disabled;
	bool				cfg_recharge_disabled;
	bool				cfg_chg_inhibit_disabled;
	bool				cfg_iterm_disabled;
	bool				cfg_aicl_disabled;
	bool				cfg_apsd_disabled;
	bool				cfg_bms_controlled_charging;

	int				cfg_vfloat_mv;
	int				cfg_resume_delta_mv;
	int				cfg_chg_inhibit_delta_mv;
	int				cfg_iterm_ma;
	int				cfg_fastchg_ma;
	int				cfg_cold_bat_decidegc;
	int				cfg_cool_bat_decidegc;
	int				cfg_warm_bat_decidegc;
	int				cfg_hot_bat_decidegc;
	int				cfg_temp_comp_mv;
	int				cfg_temp_comp_ma;
	int				cfg_safety_time;

	int				*cfg_thermal_mitigation;
	int				hot_batt_p;
	int				cold_batt_p;
	int				low_temp_threshold;
	int				high_temp_threshold;
	int				cfg_warm_comp_mv;
	int				cfg_cool_comp_mv;
	int				cfg_warm_comp_ma;
	int				cfg_cool_comp_ma;
	int				tm_state;
	int				board_ver_id;
	struct wake_lock	charger_wake_lock;
	struct wake_lock	charger_valid_lock;

	/* status */
	bool				batt_present;
	bool				batt_full;
	bool				batt_warm_full;
	bool				batt_real_full;
	bool				batt_hot;
	bool				batt_cold;
	bool				batt_warm;
	bool				batt_cool;
	bool				usb_present;
	bool				apsd_enabled;

	int				chg_disabled_status;
	int				usb_suspended_status;
	int				usb_psy_ma;

	/* others */
	bool				irq_waiting;
	bool				resume_completed;
	u8				irq_cfg_mask;
	u32				peek_poke_address;
	int				fake_battery_soc;
	int				therm_lvl_sel;
	int				thermal_levels;
	u32				workaround_flags;
	const char			*bms_psy_name;
	struct power_supply		*usb_psy;
	struct power_supply		*bms_psy;
	struct power_supply		batt_psy;
	struct mutex			read_write_lock;
	struct mutex			irq_complete;
	struct mutex			chg_disable_lock;
	struct mutex			usb_suspend_lock;
	struct mutex			icl_set_lock;
	struct mutex			jeita_configure_lock;
	struct dentry			*debug_root;
	struct delayed_work		irq_polling_work;
	struct smb23x_wakeup_source	smb23x_ws;
	struct delayed_work		temp_control_work;
	struct qpnp_vadc_chip	*vadc_dev;
	struct alarm			update_temp_alarm;
};

static int usbin_current_ma_table[] = {
	100,
	300,
	500,
	650,
	900,
	1000,
	1500,
};

static int fastchg_current_ma_table[] = {
	100,
	250,
	300,
	370,
	500,
	600,
	700,
	1000,
};

static int iterm_ma_table[] = {
	20,
	30,
	50,
	75,
};

static int recharge_mv_table[] = {
	150,
	80,
};

static int safety_time_min_table[] = {
	180,
	270,
	360,
	0,
};

static int vfloat_compensation_mv_table[] = {
	80,
	120,
	160,
	200,
};

static int inhibit_mv_table[] = {
	0,
	265,
	385,
	512,
};

static int cold_bat_decidegc_table[] = {
	100,
	50,
	0,
	-50,
};

static int cool_bat_decidegc_table[] = {
	150,
	100,
	50,
	0,
};

static int warm_bat_decidegc_table[] = {
	400,
	450,
	500,
	550,
};

static int hot_bat_decidegc_table[] = {
	500,
	550,
	600,
	650,
};


#define MIN_FLOAT_MV	3480
#define MAX_FLOAT_MV	4400
#define FLOAT_STEP_MV	40

#define _SMB_MASK(BITS, POS) \
	((unsigned char)(((1 << (BITS)) - 1) << (POS)))
#define SMB_MASK(LEFT_BIT_POS, RIGHT_BIT_POS) \
	_SMB_MASK((LEFT_BIT_POS) - (RIGHT_BIT_POS) + 1, \
				(RIGHT_BIT_POS))

/* register mapping definitions */
#define CFG_REG_0		0x00
#define USBIN_ICL_MASK		SMB_MASK(4, 2)
#define USBIN_ICL_OFFSET	2
#define ITERM_MASK		SMB_MASK(1, 0)

#define CFG_REG_2		0x02
#define RECHARGE_DIS_BIT	BIT(7)
#define RECHARGE_THRESH_MASK	BIT(6)
#define RECHARGE_THRESH_OFFSET	6
#define ITERM_DIS_BIT		BIT(5)
#define FASTCHG_CURR_MASK	SMB_MASK(2, 0)

#define CFG_REG_3		0x03
#define FASTCHG_CURR_SOFT_COMP	SMB_MASK(7, 5)
#define FASTCHG_CURR_SOFT_COMP_OFFSET	5
#define FLOAT_VOLTAGE_MASK	SMB_MASK(4, 0)

#define CFG_REG_4		0x04
#define CHG_EN_ACTIVE_LOW_BIT	BIT(5)
#define SAFETY_TIMER_MASK	SMB_MASK(4, 3)
#define SAFETY_TIMER_OFFSET	3
#define SAFETY_TIMER_DISABLE	SMB_MASK(4, 3)
#define SYSTEM_VOLTAGE_MASK	SMB_MASK(1, 0)

#define CFG_REG_5		0x05
#define BAT_THERM_DIS_BIT	BIT(7)
#define HARD_THERM_NOT_SUSPEND	BIT(6)
#define SOFT_THERM_BEHAVIOR_MASK	SMB_MASK(5, 4)
#define SOFT_THERM_VFLT_CHG_COMP	SMB_MASK(5, 4)
#define VFLOAT_COMP_MASK	SMB_MASK(3, 2)
#define VFLOAT_COMP_OFFSET		2
#define APSD_EN_BIT		BIT(1)
#define AICL_EN_BIT		BIT(0)

#define CFG_REG_6		0x06
#define CHG_INHIBIT_THRESH_MASK	SMB_MASK(7, 6)
#define INHIBIT_THRESH_OFFSET	6
#define CHG_SYSTEM_VOLTAGE_THRESHOLD_MASK	SMB_MASK(5, 4)
#define CHG_SYSTEM_VOLTAGE_THRESHOLD__OFFSET 4

#define BMD_ALGO_MASK		SMB_MASK(1, 0)
#define BMD_ALGO_THERM_IO	SMB_MASK(1, 0)

#define CFG_REG_7		0x07
#define USB1_5_PIN_CNTRL_BIT	BIT(7)
#define USB_AC_PIN_CNTRL_BIT	BIT(6)
#define CHG_EN_PIN_CNTRL_BIT	BIT(5)
#define SUSPEND_SW_CNTRL_BIT	BIT(3)

#define CFG_REG_8		0x08
#define HARD_COLD_TEMP_MASK	SMB_MASK(7, 6)
#define HARD_COLD_TEMP_OFFSET	6
#define HARD_HOT_TEMP_MASK	SMB_MASK(5, 4)
#define HARD_HOT_TEMP_OFFSET	4
#define SOFT_COLD_TEMP_MASK	SMB_MASK(3, 2)
#define SOFT_COLD_TEMP_OFFSET	2
#define SOFT_HOT_TEMP_MASK	SMB_MASK(1, 0)
#define SOFT_HOT_TEMP_OFFSET	0

#define IRQ_CFG_REG_9		0x09
#define SAFETY_TIMER_IRQ_EN_BIT	BIT(7)
#define BATT_OV_IRQ_EN_BIT	BIT(6)
#define BATT_MISSING_IRQ_EN_BIT	BIT(5)
#define ITERM_IRQ_EN_BIT	BIT(4)
#define HARD_TEMP_IRQ_EN_BIT	BIT(3)
#define SOFT_TEMP_IRQ_EN_BIT	BIT(2)
#define AICL_DONE_IRQ_EN_BIT	BIT(1)
#define INOK_IRQ_EN_BIT		BIT(0)

#define I2C_COMM_CFG_REG	0x0a
#define VOLATILE_WRITE_EN_BIT	BIT(0)

#define NV_CFG_REG		0x14
#define UNPLUG_RELOAD_DIS_BIT	BIT(2)

#define CMD_REG_0		0x30
#define VOLATILE_WRITE_ALLOW	BIT(7)
#define USB_SUSPEND_BIT		BIT(2)
#define CHARGE_EN_BIT		BIT(1)
#define STATE_PIN_OUT_DIS_BIT	BIT(0)

#define CMD_REG_1		0x31
#define USB500_MODE_BIT		BIT(1)
#define USBAC_MODE_BIT		BIT(0)

#define IRQ_A_STATUS_REG	0x38
#define HOT_HARD_BIT		BIT(6)
#define COLD_HARD_BIT		BIT(4)
#define HOT_SOFT_BIT		BIT(2)
#define COLD_SOFT_BIT		BIT(0)

#define IRQ_B_STATUS_REG	0x39
#define BATT_OV_BIT		BIT(6)
#define BATT_ABSENT_BIT		BIT(4)
#define BATT_UV_BIT		BIT(2)
#define BATT_P2F_BIT		BIT(0)

#define IRQ_C_STATUS_REG	0x3A
#define CHG_ERROR_BIT		BIT(6)
#define RECHG_THRESH_BIT	BIT(4)
#define TAPER_CHG_BIT		BIT(2)
#define ITERM_BIT		BIT(0)

#define IRQ_D_STATUS_REG	0x3B
#define APSD_DONE_BIT		BIT(6)
#define AICL_DONE_BIT		BIT(4)
#define SAFETYTIMER_EXPIRED_BIT	BIT(2)

#define CHG_STATUS_A_REG	0x3C
#define USBIN_OV_BIT		BIT(6)
#define USBIN_UV_BIT		BIT(4)
#define POWER_OK_BIT		BIT(2)
#define DIE_TEMP_LIMIT_BIT	BIT(0)

#define CHG_STATUS_B_REG	0x3D
#define CHARGE_TYPE_MASK	SMB_MASK(2, 1)
#define CHARGE_TYPE_OFFSET	1
#define NO_CHARGE_VAL		0x00
#define PRE_CHARGE_VAL		BIT(1)
#define FAST_CHARGE_VAL		BIT(2)
#define TAPER_CHARGE_VAL	SMB_MASK(2, 1)
#define CHARGE_EN_STS_BIT	BIT(0)

#define CHG_STATUS_C_REG	0x3E
#define APSD_STATUS_BIT		BIT(4)
#define APSD_RESULT_MASK	SMB_MASK(3, 0)
#define CDP_TYPE_VAL		BIT(3)
#define DCP_TYPE_VAL		BIT(2)
#define SDP_TYPE_VAL		BIT(0)
#define OTHERS_TYPE_VAL		BIT(1)

#define AICL_STATUS_REG		0x3F
#define AICL_DONE_STS_BIT	BIT(6)
#define AICL_RESULT_MASK	SMB_MASK(5, 0)
#define AICL_75_MA		BIT(3)
#define AICL_100_MA		BIT(4)
#define AICL_300_MA		BIT(0)
#define AICL_500_MA		BIT(1)
#define AICL_650_MA		SMB_MASK(1, 0)
#define AICL_900_MA		BIT(2)
#define AICL_1000_MA		(BIT(2) | BIT(0))
#define AICL_1500_MA		(BIT(2) | BIT(1))
#define USB5_LIMIT		BIT(4)
#define USB1_LIMIT		BIT(5)

#define BMS_NORMAL_UPDATE_TEMP_INTERVAL_NS	20000000000 /*20s*/
#define BMS_FAST_UPDATE_TEMP_INTERVAL_NS	1000000000 /*1s*/
#define BMS_SLOW_UPDATE_TEMP_INTERVAL_NS	300000000000 /*300s*/

struct smb_irq_info {
	const char	*name;
	int		(*smb_irq)(struct smb23x_chip *chip, u8 rt_stat);
	int		high;
	int		low;
};

struct irq_handler_info {
	u8			stat_reg;
	u8			val;
	u8			prev_val;
	struct smb_irq_info	irq_info[4];
};

enum {
	USER = BIT(0),
	CURRENT = BIT(1),
	BMS = BIT(2),
	THERMAL = BIT(3),
};

enum {
	WRKRND_IRQ_POLLING = BIT(0),
	WRKRND_USB_SUSPEND = BIT(1),
};

enum {
	SMB_TM_HIGHER_STATE,
	SMB_TM_LOWER_STATE,
	SMB_TM_NORMAL_STATE,
};

enum {
	SMB_TEMP_COLD_STATE,
	SMB_TEMP_COOL_STATE,
	SMB_TEMP_NORMAL_STATE,
	SMB_TEMP_WARM_STATE,
	SMB_TEMP_HOT_STATE,
};

static irqreturn_t smb23x_stat_handler(int irq, void *dev_id);

#define MAX_RW_RETRIES		3
static int __smb23x_read(struct smb23x_chip *chip, u8 reg, u8 *val)
{
	int rc, i;

	for (i = 0; i < MAX_RW_RETRIES; i++) {
		rc = i2c_smbus_read_byte_data(chip->client, reg);
		if (rc >= 0)
			break;
		/* delay between i2c retries */
		msleep(20);
	}
	if (rc < 0) {
		pr_err("Reading 0x%02x failed, rc = %d\n", reg, rc);
		return rc;
	}

	*val = rc;
	pr_debug("Reading 0x%02x = 0x%02x\n", reg, *val);

	return 0;
}

static int __smb23x_write(struct smb23x_chip *chip, u8 reg, u8 val)
{
	int rc, i;

	for (i = 0; i < MAX_RW_RETRIES; i++) {
		rc = i2c_smbus_write_byte_data(chip->client, reg, val);
		if (!rc)
			break;
		/* delay between i2c retries */
		msleep(20);
	}
	if (rc < 0) {
		pr_err("Writing val 0x%02x to reg 0x%02x failed, rc = %d\n",
			val, reg, rc);
		return rc;
	}

	pr_debug("Writing 0x%02x = 0x%02x\n", reg, val);

	return 0;
}

static int smb23x_read(struct smb23x_chip *chip, u8 reg, u8 *val)
{
	int rc;

	mutex_lock(&chip->read_write_lock);
	rc = __smb23x_read(chip, reg, val);
	mutex_unlock(&chip->read_write_lock);

	return rc;
}

static int smb23x_write(struct smb23x_chip *chip, int reg, int val)
{
	int rc;

	mutex_lock(&chip->read_write_lock);
	rc = __smb23x_write(chip, reg, val);
	mutex_unlock(&chip->read_write_lock);
	return rc;
}

static int smb23x_masked_write(struct smb23x_chip *chip,
				u8 reg, u8 mask, u8 val)
{
	int rc;
	u8 tmp;

	mutex_lock(&chip->read_write_lock);
	rc = __smb23x_read(chip, reg, &tmp);
	if (rc < 0) {
		pr_err("Read failed\n");
		goto i2c_error;
	}
	tmp &= ~mask;
	tmp |= val & mask;
	rc = __smb23x_write(chip, reg, tmp);
	if (rc < 0)
		pr_err("Write failed\n");
i2c_error:
	mutex_unlock(&chip->read_write_lock);
	return rc;
}

static void smb23x_wakeup_src_init(struct smb23x_chip *chip)
{
	spin_lock_init(&chip->smb23x_ws.ws_lock);
	wakeup_source_init(&chip->smb23x_ws.source, "smb23x");
}

static void smb23x_stay_awake(struct smb23x_wakeup_source *source,
			enum wakeup_src wk_src)
{
	unsigned long flags;

	spin_lock_irqsave(&source->ws_lock, flags);

	if (!__test_and_set_bit(wk_src, &source->enabled_bitmap)) {
		__pm_stay_awake(&source->source);
		pr_debug("enabled source %s, wakeup_src %d\n",
			source->source.name, wk_src);
	}
	spin_unlock_irqrestore(&source->ws_lock, flags);
}

static void smb23x_relax(struct smb23x_wakeup_source *source,
	enum wakeup_src wk_src)
{
	unsigned long flags;

	spin_lock_irqsave(&source->ws_lock, flags);
	if (__test_and_clear_bit(wk_src, &source->enabled_bitmap) &&
		!(source->enabled_bitmap & WAKEUP_SRC_MASK)) {
		__pm_relax(&source->source);
		pr_debug("disabled source %s\n", source->source.name);
	}
	spin_unlock_irqrestore(&source->ws_lock, flags);

	pr_debug("relax source %s, wakeup_src %d\n",
		source->source.name, wk_src);
}

static int smb23x_parse_dt(struct smb23x_chip *chip)
{
	int rc = 0;
	struct device_node *node = chip->dev->of_node;

	if (!node) {
		pr_err("device node invalid\n");
		return (-EINVAL);
	}

	chip->cfg_charging_disabled =
		of_property_read_bool(node, "qcom,charging-disabled");
	chip->cfg_recharge_disabled =
		of_property_read_bool(node, "qcom,recharge-disabled");
	chip->cfg_chg_inhibit_disabled =
		of_property_read_bool(node, "qcom,chg-inhibit-disabled");
	chip->cfg_iterm_disabled =
		of_property_read_bool(node, "qcom,iterm-disabled");
	chip->cfg_aicl_disabled =
		of_property_read_bool(node, "qcom,aicl-disabled");
	chip->cfg_apsd_disabled =
		of_property_read_bool(node, "qcom,apsd-disabled");
	chip->cfg_bms_controlled_charging =
		of_property_read_bool(node, "qcom,bms-controlled-charging");

	rc = of_property_read_string(node, "qcom,bms-psy-name",
						&chip->bms_psy_name);
	if (rc) {
		chip->bms_psy_name = NULL;
		chip->cfg_bms_controlled_charging = false;
	}

	/*
	 * If bms-controlled-charging is defined, then the charging
	 * termination and recharge behavior will be controlled by
	 * BMS power supply instead of the SMB chip itself, so we
	 * need to keep iterm and recharge on the chip disabled.
	 */
	if ((chip->cfg_bms_controlled_charging) || (chip->board_ver_id == 0)) {
		pr_info("board_id:%d\n", chip->board_ver_id);
		chip->cfg_iterm_disabled = true;
		chip->cfg_recharge_disabled = true;
	}
	rc = of_property_read_u32(node, "qcom,float-voltage-mv",
					&chip->cfg_vfloat_mv);
	if (rc < 0) {
		chip->cfg_vfloat_mv = -EINVAL;
	} else {
		if (chip->cfg_vfloat_mv > MAX_FLOAT_MV
			|| chip->cfg_vfloat_mv < MIN_FLOAT_MV) {
			pr_err("Float voltage out of range\n");
			return (-EINVAL);
		}
	}

	rc = of_property_read_u32(node, "qcom,recharge-thresh-mv",
					&chip->cfg_resume_delta_mv);
	if (rc < 0)
		chip->cfg_resume_delta_mv = -EINVAL;

	rc = of_property_read_u32(node, "qcom,chg-inhibit-thresh-mv",
					&chip->cfg_chg_inhibit_delta_mv);
	if (rc < 0)
		chip->cfg_chg_inhibit_delta_mv = -EINVAL;

	rc = of_property_read_u32(node, "qcom,iterm-ma",
					&chip->cfg_iterm_ma);
	if (rc < 0)
		chip->cfg_iterm_ma = -EINVAL;

	rc = of_property_read_u32(node, "qcom,fastchg-ma",
					&chip->cfg_fastchg_ma);
	if (rc < 0)
		chip->cfg_fastchg_ma = -EINVAL;

	rc = of_property_read_u32(node, "qcom,cold-bat-decidegc",
					&chip->cfg_cold_bat_decidegc);
	if (rc < 0)
		chip->cfg_cold_bat_decidegc = -EINVAL;

	rc = of_property_read_u32(node, "qcom,cool-bat-decidegc",
					&chip->cfg_cool_bat_decidegc);
	if (rc < 0)
		chip->cfg_cool_bat_decidegc = -EINVAL;

	rc = of_property_read_u32(node, "qcom,warm-bat-decidegc",
					&chip->cfg_warm_bat_decidegc);
	if (rc < 0)
		chip->cfg_warm_bat_decidegc = -EINVAL;

	rc = of_property_read_u32(node, "qcom,hot-bat-decidegc",
					&chip->cfg_hot_bat_decidegc);
	if (rc < 0)
		chip->cfg_hot_bat_decidegc = -EINVAL;

	rc = of_property_read_u32(node,"zte,batt-hot-percentage",
		&chip->hot_batt_p);
	if (rc < 0)
			chip->hot_batt_p = -EINVAL;

	rc = of_property_read_u32(node,"zte,batt-cold-percentage",
		&chip->cold_batt_p);
	if (rc < 0)
			chip->cold_batt_p = -EINVAL;

	rc = of_property_read_u32(node,"zte,soft-warm-current-comp-ma",
		&chip->cfg_warm_comp_ma);
	if (rc < 0)
			chip->cfg_warm_comp_ma = -EINVAL;

	rc = of_property_read_u32(node,"zte,soft-cool-current-comp-ma",
		&chip->cfg_cool_comp_ma);
	if (rc < 0)
			chip->cfg_cool_comp_ma = -EINVAL;

	rc = of_property_read_u32(node,"zte,soft-warm-vfloat-comp-mv",
		&chip->cfg_warm_comp_mv);
	if (rc < 0)
			chip->cfg_warm_comp_mv = -EINVAL;

	rc = of_property_read_u32(node,"zte,soft-cool-vfloat-comp-mv",
		&chip->cfg_cool_comp_mv);
	if (rc < 0)
			chip->cfg_cool_comp_mv = -EINVAL;

	rc = of_property_read_u32(node, "qcom,soft-temp-vfloat-comp-mv",
					&chip->cfg_temp_comp_mv);
	if (rc < 0)
		chip->cfg_temp_comp_mv = -EINVAL;

	rc = of_property_read_u32(node, "qcom,soft-temp-current-comp-ma",
					&chip->cfg_temp_comp_ma);
	if (rc < 0)
		chip->cfg_temp_comp_ma = -EINVAL;

	rc = of_property_read_u32(node, "qcom,charging-timeout",
					&chip->cfg_safety_time);
	if (rc < 0)
		chip->cfg_safety_time = -EINVAL;

	if (of_find_property(node, "qcom,thermal-mitigation",
					&chip->thermal_levels)) {
		chip->cfg_thermal_mitigation = devm_kzalloc(chip->dev,
						chip->thermal_levels,
						GFP_KERNEL);

		if (chip->cfg_thermal_mitigation == NULL) {
			pr_err("thermal mitigation kzalloc() failed.\n");
			return (-ENOMEM);
		}

		chip->thermal_levels /= sizeof(int);
		rc = of_property_read_u32_array(node,
				"qcom,thermal-mitigation",
				chip->cfg_thermal_mitigation,
				chip->thermal_levels);
		if (rc) {
			pr_err("Read therm limits failed, rc=%d\n", rc);
			return rc;
		}
	}

	return 0;
}

static int smb23x_enable_volatile_writes(struct smb23x_chip *chip)
{
	int rc;
	u8 reg;

	rc = smb23x_read(chip, I2C_COMM_CFG_REG, &reg);
	if (rc < 0) {
		pr_err("Read I2C_COMM_CFG_REG, rc=%d\n", rc);
		return rc;
	}
	if (!(reg & VOLATILE_WRITE_EN_BIT)) {
		pr_err("Volatile write is not allowed!");
		return (-EACCES);
	}

	rc = smb23x_masked_write(chip, CMD_REG_0,
			VOLATILE_WRITE_ALLOW, VOLATILE_WRITE_ALLOW);
	if (rc < 0) {
		pr_err("Set VOLATILE_WRITE_ALLOW bit failed\n");
		return rc;
	}

	return 0;
}

static inline int find_closest_in_ascendant_list(
		int val, int lst[], int length)
{
	int i;

	for (i = 0; i < length; i++) {
		if (val <= lst[i])
			break;
	}
	if (i == length)
		i--;

	return i;
}

static inline int find_closest_in_descendant_list(
		int val, int lst[], int length)
{
	int i;

	for (i = 0; i < length; i++) {
		if (val >= lst[i])
			break;
	}
	if (i == length)
		i--;

	return i;
}

static int __smb23x_charging_disable(struct smb23x_chip *chip, bool disable)
{
	int rc;

	rc = smb23x_masked_write(chip, CMD_REG_0,
			CHARGE_EN_BIT, disable ? 0 : CHARGE_EN_BIT);
	if (rc < 0)
		pr_err("%s charging failed, rc=%d\n",
				disable ? "Disable" : "Enable", rc);
	return rc;
}

static int smb23x_charging_disable(struct smb23x_chip *chip,
			int reason, int disable)
{
	int rc = 0;
	int disabled;

	mutex_lock(&chip->chg_disable_lock);
	disabled = chip->chg_disabled_status;
	if (disable)
		disabled |= reason;
	else
		disabled &= ~reason;

	rc = __smb23x_charging_disable(chip, disabled ? true : false);
	if (rc < 0)
		pr_err("%s charging for reason 0x%x failed\n",
				disable ? "Disable" : "Enable", reason);
	else
		chip->chg_disabled_status = disabled;
	mutex_unlock(&chip->chg_disable_lock);
	return rc;
}

static int smb23x_suspend_usb(struct smb23x_chip *chip,
			int reason, bool suspend)
{
	int rc = 0;
	int suspended;

	mutex_lock(&chip->usb_suspend_lock);
	suspended = chip->usb_suspended_status;
	if (suspend)
		suspended |= reason;
	else
		suspended &= ~reason;

	rc = smb23x_masked_write(chip, CMD_REG_0, USB_SUSPEND_BIT,
			suspended ? USB_SUSPEND_BIT : 0);
	if (rc < 0) {
		pr_err("Write USB_SUSPEND failed, rc=%d\n", rc);
	} else {
		chip->usb_suspended_status = suspended;
		pr_debug("%suspend USB!\n", suspend ? "S" : "Un-s");
	}
	mutex_unlock(&chip->usb_suspend_lock);

	return rc;
}

#define CURRENT_SUSPEND	2
#define CURRENT_100_MA	100
#define CURRENT_500_MA	500
static int smb23x_set_appropriate_usb_current(struct smb23x_chip *chip)
{
	int rc = 0, therm_ma, current_ma;
	int usb_current = chip->usb_psy_ma;
	u8 tmp;

	if (chip->therm_lvl_sel > 0
			&& chip->therm_lvl_sel < (chip->thermal_levels - 1))
		/*
		 * consider thermal limit only when it is active and not at
		 * the highest level
		 */
		therm_ma = chip->cfg_thermal_mitigation[chip->therm_lvl_sel];
	else
		therm_ma = usb_current;

	current_ma = min(therm_ma, usb_current);

	if (current_ma <= CURRENT_SUSPEND) {
		if (chip->workaround_flags & WRKRND_USB_SUSPEND) {
			current_ma = CURRENT_100_MA;
		} else {
			/* suspend USB input */
			rc = smb23x_suspend_usb(chip, CURRENT, true);
			if (rc)
				pr_err("Suspend USB failed, rc=%d\n", rc);
			return rc;
		}
	}

	if (current_ma <= CURRENT_100_MA) {
		/* USB 100 */
		rc = smb23x_masked_write(chip, CMD_REG_1,
				USB500_MODE_BIT | USBAC_MODE_BIT, 0);
		if (rc)
			pr_err("Set USB100 failed, rc=%d\n", rc);
		pr_debug("Setting USB 100\n");
	} else if (current_ma <= CURRENT_500_MA) {
		/* USB 500 */
		rc = smb23x_masked_write(chip, CMD_REG_1,
				USB500_MODE_BIT | USBAC_MODE_BIT,
				USB500_MODE_BIT);
		if (rc)
			pr_err("Set USB500 failed, rc=%d\n", rc);
		pr_debug("Setting USB 500\n");
	} else {
		/* USB AC */
		rc = smb23x_masked_write(chip, CMD_REG_1,
				USBAC_MODE_BIT, USBAC_MODE_BIT);
		if (rc)
			pr_err("Set USBAC failed, rc=%d\n", rc);
		pr_debug("Setting USB AC\n");
	}

	/* set ICL */
	tmp = find_closest_in_ascendant_list(current_ma, usbin_current_ma_table,
					ARRAY_SIZE(usbin_current_ma_table));
	tmp = tmp << USBIN_ICL_OFFSET;
	rc = smb23x_masked_write(chip, CFG_REG_0, USBIN_ICL_MASK, tmp);
	if (rc < 0) {
		pr_err("Set ICL failed\n, rc=%d\n", rc);
		return rc;
	}
	pr_debug("ICL set to = %d\n",
			usbin_current_ma_table[tmp >> USBIN_ICL_OFFSET]);

	if (!(chip->workaround_flags & WRKRND_USB_SUSPEND)) {
		/* un-suspend USB input */
		rc = smb23x_suspend_usb(chip, CURRENT, false);
		if (rc < 0)
			pr_err("Un-suspend USB failed, rc=%d\n", rc);
	}

	return rc;
}

extern void set_batt_hot_cold_threshold(unsigned int vbatt_hot_threshold,
	unsigned int vbatt_cold_threshold);

#define DEFAULT_BATT_VOLTAGE	4000
static int smb23x_get_prop_batt_voltage(struct smb23x_chip *chip)
{
	union power_supply_propval ret = {0, };

	if (chip->bms_psy) {
		chip->bms_psy->get_property(chip->bms_psy,
				POWER_SUPPLY_PROP_VOLTAGE_NOW, &ret);
		return ret.intval/1000;
	}

	return DEFAULT_BATT_VOLTAGE;
}

static int smb23x_get_prop_batt_voltage_avg(struct smb23x_chip *chip)
{
	union power_supply_propval ret = {0, };

	if (chip->bms_psy) {
		chip->bms_psy->get_property(chip->bms_psy,
				POWER_SUPPLY_PROP_VOLTAGE_AVG, &ret);
		return ret.intval/1000;
	}

	return DEFAULT_BATT_VOLTAGE;
}

#define DEFAULT_BATT_CAPACITY	60
static int smb23x_get_prop_batt_capacity(struct smb23x_chip *chip)
{
	union power_supply_propval ret = {0, };

	if (is_fake_battery)
		return DEFAULT_BATT_CAPACITY;

	if (chip->fake_battery_soc != -EINVAL)
		return chip->fake_battery_soc;

	if (chip->batt_full == true)
		return 100;

	if (chip->bms_psy) {
		chip->bms_psy->get_property(chip->bms_psy,
				POWER_SUPPLY_PROP_CAPACITY, &ret);
		return ret.intval;
	}

	return DEFAULT_BATT_CAPACITY;
}

static int smb23x_get_prop_batt_real_capacity(struct smb23x_chip *chip)
{
	union power_supply_propval ret = {0, };

	if (is_fake_battery)
		return DEFAULT_BATT_CAPACITY;

	if (chip->fake_battery_soc != -EINVAL)
		return chip->fake_battery_soc;

	if (chip->bms_psy) {
		chip->bms_psy->get_property(chip->bms_psy,
				POWER_SUPPLY_PROP_CAPACITY, &ret);
		return ret.intval;
	}

	return DEFAULT_BATT_CAPACITY;
}

#define SYSTEM_VOLTAGE_RESET_GATE	4050
static void smb23x_system_voltage_set(struct smb23x_chip *chip)
{
	int rc;
	u8 tmp;

	rc = smb23x_masked_write(chip, CFG_REG_4,
		SYSTEM_VOLTAGE_MASK,
		2);
	if (rc < 0) {
		pr_err("Set System Voltage failed, rc=%d\n", rc);
		return;
	}

	rc = smb23x_masked_write(chip, CFG_REG_6,
		CHG_SYSTEM_VOLTAGE_THRESHOLD_MASK,
		2 << CHG_SYSTEM_VOLTAGE_THRESHOLD__OFFSET);
	if (rc < 0) {
		pr_err("Set System Voltage threshold failed, rc=%d\n", rc);
		return;
	}

	rc = smb23x_read(chip, CFG_REG_4, &tmp);
	if (rc < 0) {
		pr_err("read CFG_REG_5 failed, rc=%d\n", rc);
		return;
	}
	pr_info("CFG_REG_4:%d\n", tmp);

	rc = smb23x_read(chip, CFG_REG_6, &tmp);
	if (rc < 0) {
		pr_err("read CFG_REG_5 failed, rc=%d\n", rc);
		return;
	}
	pr_info("CFG_REG_6:%d\n", tmp);
}

#define DEFAULT_BATT_CURRENT	500
static int smb23x_get_current_now(struct smb23x_chip *chip)
{
	union power_supply_propval ret = {0, };

	if (chip->bms_psy) {
		chip->bms_psy->get_property(chip->bms_psy,
				POWER_SUPPLY_PROP_CURRENT_NOW, &ret);
		return ret.intval/1000;
	}

	return DEFAULT_BATT_CURRENT;
}
static int smb23x_get_current_avg(struct smb23x_chip *chip)
{
	union power_supply_propval ret = {0, };

	if (chip->bms_psy) {
		chip->bms_psy->get_property(chip->bms_psy,
				POWER_SUPPLY_PROP_CURRENT_AVG, &ret);
		return ret.intval/1000;
	}

	return DEFAULT_BATT_CURRENT;
}


static void smb23x_get_charge_term_sts(struct smb23x_chip *chip, bool *value)
{
	u8 tmp;
	int rc;

	rc = smb23x_read(chip, IRQ_C_STATUS_REG, &tmp);
	if (rc < 0) {
		pr_err("read CFG_REG_5 failed, rc=%d\n", rc);
		return;
	}

	if (tmp & ITERM_BIT)
		*value = true;
	else
		*value = false;
}

static void smb23x_get_recharge_sts(struct smb23x_chip *chip, bool *value)
{
	u8 tmp;
	int rc;

	rc = smb23x_read(chip, IRQ_C_STATUS_REG, &tmp);
	if (rc < 0) {
		pr_err("read CFG_REG_5 failed, rc=%d\n", rc);
		return;
	}

	if (tmp & RECHG_THRESH_BIT)
		*value = true;
	else
		*value = false;
}

static void smb23x_set_charger_state(struct smb23x_chip *chip, bool disable)
{
	pr_info("disable=%d chip->batt_full=%d\n", disable, chip->batt_full);
	if (disable) {
		smb23x_charging_disable(chip, CURRENT, true);
		power_supply_changed(&chip->batt_psy);
		pr_info("charger_disable1=%d chip->batt_real_full1=%d\n", disable, chip->batt_real_full);
	} else {
		smb23x_charging_disable(chip, CURRENT, false);
		power_supply_changed(&chip->batt_psy);
		pr_info("charger_disable2=%d chip->batt_real_full2=%d\n", disable, chip->batt_real_full);
	}
}

#define DEFAULT_FULLCAP	550
static int smb23x_get_fullcap(struct smb23x_chip *chip)
{
	union power_supply_propval ret = {0, };

	if (chip->bms_psy) {
		chip->bms_psy->get_property(chip->bms_psy,
				POWER_SUPPLY_PROP_CHARGE_FULL, &ret);
		return ret.intval;
	}

	return DEFAULT_FULLCAP;
}

#define DEFAULT_REPCAP	250
static int smb23x_get_repcap(struct smb23x_chip *chip)
{
	union power_supply_propval ret = {0, };

	if (chip->bms_psy) {
		chip->bms_psy->get_property(chip->bms_psy,
				POWER_SUPPLY_PROP_CHARGE_NOW, &ret);
		return ret.intval;
	}

	return DEFAULT_REPCAP;
}

#define FULL_CAPACITY 100
#define RECHARGE_CAPACITY 99

static bool smb23x_recharge_now(struct smb23x_chip *chip)
{
	int  capacity, fullcap, repcap;

	fullcap = smb23x_get_fullcap(chip);
	repcap = smb23x_get_repcap(chip);
	capacity = smb23x_get_prop_batt_real_capacity(chip);
	pr_info("fullcap:%d, repcap:%d, capacity:%d\n", fullcap, repcap, capacity);
	if ((capacity < RECHARGE_CAPACITY) || ((fullcap - repcap) * 1000 > (fullcap * 15))) {
		smb23x_set_charger_state(chip, false);
		chip->batt_real_full = false;
		return true;
	} else
		return false;
}

static bool smb23x_check_charge_term(struct smb23x_chip *chip, int current_now)
{
	int  capacity, fullcap, repcap;

	fullcap = smb23x_get_fullcap(chip);
	repcap = smb23x_get_repcap(chip);
	capacity = smb23x_get_prop_batt_real_capacity(chip);
	pr_info("fullcap:%d, repcap:%d, capacity:%d\n", fullcap, repcap, capacity);
	if ((current_now <= 0) && (abs(current_now) < (chip->cfg_iterm_ma + 10)))
		if ((capacity >= RECHARGE_CAPACITY) && (fullcap == repcap))
			return true;
	return false;
}

#define CONSECUTIVE_COUNT	5

static void smb23x_check_fullcharged_state(struct smb23x_chip *chip)
{
	int  current_now, capacity;
	bool chg_term;
	static int count = 0;

	capacity = smb23x_get_prop_batt_real_capacity(chip);
	smb23x_get_charge_term_sts(chip, &chg_term);

	if ((chg_term) && (!chip->batt_real_full)) {
			__smb23x_charging_disable(chip, true);
			__smb23x_charging_disable(chip, false);
			pr_info("[CHG] start re-charging when charger reported DONE and batt_real_full =%d\n",
					chip->batt_real_full);
	}
	current_now = smb23x_get_current_now(chip);

	pr_debug("soc =%d,chg_term:%d,batt_full:%d,current_now:%d,batt_real_full:%d\n",
		capacity, chg_term, chip->batt_full, current_now, chip->batt_real_full);
	if (chip->batt_real_full) {
		if (!smb23x_recharge_now(chip)) {
			pr_info("do not need charge now\n");
			return;
		}
		pr_info("recharge now\n");
	} else if (current_now > 0) {
		pr_info("Charging but system demand increased\n");
		count = 0;
	} else if (!smb23x_check_charge_term(chip, current_now)) {
		pr_info("Not at EOC, current_now=%d,capacity:%d\n", current_now, capacity);
		count = 0;
	} else {
		if (count == CONSECUTIVE_COUNT) {
			chip->batt_full = true;
			chip->batt_real_full = true;
			smb23x_set_charger_state(chip, true);
		} else {
			count += 1;
			pr_info("EOC count = %d\n", count);
		}
	}
}

static bool smb23x_warm_cool_recharge_now(struct smb23x_chip *chip)
{
	int voltage_avg;
	bool recharge_sts;

	voltage_avg = smb23x_get_prop_batt_voltage_avg(chip);
	smb23x_get_recharge_sts(chip, &recharge_sts);
	if (!chip->batt_warm_full)
		return false;

	if (recharge_sts)
		return true;

	if (chip->tm_state == SMB_TEMP_WARM_STATE) {
		if (voltage_avg < (chip->cfg_warm_comp_mv - chip->cfg_resume_delta_mv))
			return true;
		else
			return false;
	}

	if (chip->tm_state == SMB_TEMP_WARM_STATE) {
		if (voltage_avg < (chip->cfg_cool_comp_mv - chip->cfg_resume_delta_mv))
			return true;
		else
			return false;
	}

	return false;

}

static void smb23x_warm_cool_check_fullcharged_state(struct smb23x_chip *chip)
{
	int  current_now;
	bool chg_term, recharge_now;
	static int count = 0;

	current_now = smb23x_get_current_now(chip);
	recharge_now = smb23x_warm_cool_recharge_now(chip);
	smb23x_get_charge_term_sts(chip, &chg_term);

	if (chg_term) {
		chip->batt_warm_full = true;
		smb23x_set_charger_state(chip, true);
	}

	pr_debug("chg_term:%d,chip->batt_warm_full:%d,current_avg:%d,recharge_now:%d\n",
		 chg_term, chip->batt_warm_full, current_now, recharge_now);
	if (chip->batt_warm_full) {
		if (recharge_now) {
			smb23x_set_charger_state(chip, false);
			chip->batt_warm_full = false;
		} else {
			pr_info("do not need charge now\n");
			return;
		}
	} else if (current_now > 0) {
		pr_info("Charging but system demand increased\n");
		count = 0;
	} else if ((current_now * -1) > (chip->cfg_iterm_ma + 10)) {
		pr_info("Not at EOC, ibat_ma=%d\n", current_now);
		count = 0;
	} else {
		if (count == CONSECUTIVE_COUNT) {
			chip->batt_warm_full = true;
			smb23x_set_charger_state(chip, true);
		} else {
			count += 1;
			pr_info("EOC count = %d\n", count);
		}
	}
}

static void smb23x_charger_eoc(struct smb23x_chip *chip)
{
	if (!chip->resume_completed) {
		pr_info("charger_eoc launched before device-resume, schedule to 2s later\n");
		return;
	}

	if (chip->usb_present == false) {
		pr_info("no chg connected, go through directly\n");
		if (chip->batt_warm_full || chip->batt_full) {
			smb23x_set_charger_state(chip, false);
			chip->batt_warm_full = false;
			chip->batt_full = false;
		}
		return;
	}

	if (chip->batt_present == false) {
		pr_info("no battery, go through directly\n");
		return;
	}

	if (chip->tm_state == SMB_TEMP_NORMAL_STATE) {
		pr_debug("normal temp\n");
		if (chip->batt_warm_full) {
			smb23x_set_charger_state(chip, false);
			chip->batt_warm_full = false;
		}
		smb23x_check_fullcharged_state(chip);
	}

	if (chip->tm_state == SMB_TEMP_COOL_STATE || chip->tm_state == SMB_TEMP_WARM_STATE) {
		pr_debug("warm or cool temp\n");
		smb23x_warm_cool_check_fullcharged_state(chip);
	}
}

static int smb23x_hw_init(struct smb23x_chip *chip)
{
	int rc, i = 0;
	u8 tmp;
	set_batt_hot_cold_threshold(chip->hot_batt_p, chip->cold_batt_p);

	rc = smb23x_enable_volatile_writes(chip);
	if (rc < 0) {
		pr_err("Enable volatile writes failed, rc=%d\n", rc);
		return rc;
	}

	/* iterm setting */
	if (chip->cfg_iterm_disabled) {
		rc = smb23x_masked_write(chip, CFG_REG_2,
				ITERM_DIS_BIT, ITERM_DIS_BIT);
		if (rc < 0) {
			pr_err("Disable ITERM failed, rc=%d\n", rc);
			return rc;
		}
	} else if (chip->cfg_iterm_ma != -EINVAL) {
		i = find_closest_in_ascendant_list(chip->cfg_iterm_ma,
				iterm_ma_table, ARRAY_SIZE(iterm_ma_table));
		tmp = i;
		rc = smb23x_masked_write(chip, CFG_REG_0, ITERM_MASK, tmp);
		if (rc < 0) {
			pr_err("Set ITERM failed, rc=%d\n", rc);
			return rc;
		}
	}

	/* recharging setting */
	if (chip->cfg_recharge_disabled) {
		rc = smb23x_masked_write(chip, CFG_REG_2,
			RECHARGE_DIS_BIT, RECHARGE_DIS_BIT);
		if (rc < 0) {
			pr_err("Disable recharging failed, rc=%d\n", rc);
			return rc;
		}
	} else if (chip->cfg_resume_delta_mv != -EINVAL) {
		i = find_closest_in_descendant_list(
			chip->cfg_resume_delta_mv, recharge_mv_table,
			ARRAY_SIZE(recharge_mv_table));
		tmp = i << RECHARGE_THRESH_OFFSET;
		rc = smb23x_masked_write(chip, CFG_REG_2,
				RECHARGE_THRESH_MASK, tmp);
		if (rc < 0) {
			pr_err("Set recharge thresh failed, rc=%d\n", rc);
			return rc;
		}
	}

	/* charging inhibit setting */
	if (chip->cfg_chg_inhibit_disabled) {
		rc = smb23x_masked_write(chip, CFG_REG_6,
				CHG_INHIBIT_THRESH_MASK, 0);
		if (rc < 0) {
			pr_err("Disable charge inhibit failed, rc=%d\n", rc);
			return rc;
		}
	} else if (chip->cfg_chg_inhibit_delta_mv != -EINVAL) {
		i = find_closest_in_ascendant_list(
			chip->cfg_chg_inhibit_delta_mv, inhibit_mv_table,
			ARRAY_SIZE(inhibit_mv_table));
		tmp = i << INHIBIT_THRESH_OFFSET;
		rc = smb23x_masked_write(chip, CFG_REG_6,
				CHG_INHIBIT_THRESH_MASK, tmp);
		if (rc < 0) {
			pr_err("Set inhibit threshold failed, rc=%d\n", rc);
			return rc;
		}
	}

	/* disable AICL */
	if (chip->cfg_aicl_disabled) {
		rc = smb23x_masked_write(chip, CFG_REG_5, AICL_EN_BIT, 0);
		if (rc < 0) {
			pr_err("Disable AICL failed, rc=%d\n", rc);
			return rc;
		}
	}

	/* disable APSD */
	if (chip->cfg_apsd_disabled) {
		rc = smb23x_masked_write(chip, CFG_REG_5, APSD_EN_BIT, 0);
		if (rc < 0) {
			pr_err("Disable APSD failed, rc=%d\n", rc);
			return rc;
		}
		chip->apsd_enabled = false;
	} else {
		rc = smb23x_read(chip, CFG_REG_5, &tmp);
		if (rc < 0) {
			pr_err("read CFG_REG_5 failed, rc=%d\n", rc);
			return rc;
		}
		chip->apsd_enabled = !!(tmp & APSD_EN_BIT);
	}

	/* float voltage setting */
	if (chip->cfg_vfloat_mv != -EINVAL) {
		tmp = (chip->cfg_vfloat_mv - MIN_FLOAT_MV) / FLOAT_STEP_MV;
		rc = smb23x_masked_write(chip, CFG_REG_3,
				FLOAT_VOLTAGE_MASK, tmp);
		if (rc < 0) {
			pr_err("Set float voltage failed, rc=%d\n", rc);
			return rc;
		}
	}

	/* fastchg current setting */
	if (chip->cfg_fastchg_ma != -EINVAL) {
		i = find_closest_in_ascendant_list(
			chip->cfg_fastchg_ma, fastchg_current_ma_table,
			ARRAY_SIZE(fastchg_current_ma_table));
		tmp = i;
		rc = smb23x_masked_write(chip, CFG_REG_2,
				FASTCHG_CURR_MASK, tmp);
		if (rc < 0) {
			pr_err("Set fastchg current failed, rc=%d\n", rc);
			return rc;
		}
	}

	/* hard JEITA settings */
	if (chip->cfg_cold_bat_decidegc != -EINVAL ||
		chip->cfg_hot_bat_decidegc != -EINVAL) {
		u8 mask = 0;

		rc = smb23x_masked_write(chip, CFG_REG_5,
				BAT_THERM_DIS_BIT | HARD_THERM_NOT_SUSPEND, 0);
		if (rc < 0) {
			pr_err("Enable thermal monitor failed, rc=%d\n", rc);
			return rc;
		}
		if (chip->cfg_cold_bat_decidegc != -EINVAL) {
			i = find_closest_in_descendant_list(
				chip->cfg_cold_bat_decidegc,
				cold_bat_decidegc_table,
				ARRAY_SIZE(cold_bat_decidegc_table));
			mask |= HARD_COLD_TEMP_MASK;
			tmp = i << HARD_COLD_TEMP_OFFSET;
		}

		if (chip->cfg_hot_bat_decidegc != -EINVAL) {
			i = find_closest_in_ascendant_list(
				chip->cfg_hot_bat_decidegc,
				hot_bat_decidegc_table,
				ARRAY_SIZE(hot_bat_decidegc_table));
			mask |= HARD_HOT_TEMP_MASK;
			tmp |= i << HARD_HOT_TEMP_OFFSET;
		}
		rc = smb23x_masked_write(chip, CFG_REG_8, mask, tmp);
		if (rc < 0) {
			pr_err("Set hard cold/hot temperature failed, rc=%d\n",
				rc);
			return rc;
		}
	}

	/* soft JEITA settings */
	if (chip->cfg_cool_bat_decidegc != -EINVAL ||
		chip->cfg_warm_bat_decidegc != -EINVAL) {
		u8 mask = 0;

		rc = smb23x_masked_write(chip, CFG_REG_5,
			SOFT_THERM_BEHAVIOR_MASK, SOFT_THERM_VFLT_CHG_COMP);
		if (rc < 0) {
			pr_err("Set soft JEITA behavior failed, rc=%d\n", rc);
			return rc;
		}
		if (chip->cfg_cool_bat_decidegc != -EINVAL) {
			i = find_closest_in_descendant_list(
				chip->cfg_cool_bat_decidegc,
				cool_bat_decidegc_table,
				ARRAY_SIZE(cool_bat_decidegc_table));
			mask |= SOFT_COLD_TEMP_MASK;
			tmp = i << SOFT_COLD_TEMP_OFFSET;
		}

		if (chip->cfg_warm_bat_decidegc != -EINVAL) {
			i = find_closest_in_ascendant_list(
				chip->cfg_warm_bat_decidegc,
				warm_bat_decidegc_table,
				ARRAY_SIZE(warm_bat_decidegc_table));
			mask |= SOFT_HOT_TEMP_MASK;
			tmp |= i << SOFT_HOT_TEMP_OFFSET;
		}

		rc = smb23x_masked_write(chip, CFG_REG_8, mask, tmp);
		if (rc < 0) {
			pr_err("Set soft cold/hot temperature failed, rc=%d\n",
				rc);
			return rc;
		}
	}

	/* float voltage and fastchg current compensation for soft JEITA */
	if (chip->cfg_temp_comp_mv != -EINVAL) {
		i = find_closest_in_ascendant_list(
			chip->cfg_temp_comp_mv, vfloat_compensation_mv_table,
			ARRAY_SIZE(vfloat_compensation_mv_table));
		tmp = i << VFLOAT_COMP_OFFSET;
		rc = smb23x_masked_write(chip, CFG_REG_5,
				VFLOAT_COMP_MASK, tmp);
		if (rc < 0) {
			pr_err("Set VFLOAT_COMP failed, rc=%d\n", rc);
			return rc;
		}
	}
	if (chip->cfg_temp_comp_ma != -EINVAL) {
		int compensated_ma;

		compensated_ma = chip->cfg_fastchg_ma - chip->cfg_temp_comp_ma;
		i = find_closest_in_ascendant_list(
			compensated_ma, fastchg_current_ma_table,
			ARRAY_SIZE(fastchg_current_ma_table));
		tmp = i << FASTCHG_CURR_SOFT_COMP_OFFSET;
		rc = smb23x_masked_write(chip, CFG_REG_3,
				FASTCHG_CURR_SOFT_COMP, tmp);
		if (rc < 0) {
			pr_err("Set FASTCHG_COMP failed, rc=%d\n", rc);
			return rc;
		}
	}

	/* safety timer setting */
	if (chip->cfg_safety_time != -EINVAL) {
		i = find_closest_in_ascendant_list(
			chip->cfg_safety_time, safety_time_min_table,
			ARRAY_SIZE(safety_time_min_table));
		tmp = i << SAFETY_TIMER_OFFSET;
		/* disable safety timer if the value equals 0 */
		if (chip->cfg_safety_time == 0)
			tmp = SAFETY_TIMER_DISABLE;
		rc = smb23x_masked_write(chip, CFG_REG_4,
				SAFETY_TIMER_MASK, tmp);
		if (rc < 0) {
			pr_err("Set safety timer failed, rc=%d\n", rc);
			return rc;
		}
	}

	/*System voltage config*/
	smb23x_system_voltage_set(chip);

	/*
	 * Disable the STAT pin output, to make the pin keep at open drain
	 * state and detect the IRQ on the falling edge
	 */
	rc = smb23x_masked_write(chip, CMD_REG_0,
			STATE_PIN_OUT_DIS_BIT,
			STATE_PIN_OUT_DIS_BIT);
	if (rc < 0) {
		pr_err("Disable state pin output failed, rc=%d\n", rc);
		return rc;
	}

	rc = smb23x_masked_write(chip, CFG_REG_4,
			CHG_EN_ACTIVE_LOW_BIT, 0); /* EN active high */
	rc |= smb23x_masked_write(chip, CFG_REG_7,
				USB1_5_PIN_CNTRL_BIT |
				USB_AC_PIN_CNTRL_BIT |
				CHG_EN_PIN_CNTRL_BIT |
				SUSPEND_SW_CNTRL_BIT,
				SUSPEND_SW_CNTRL_BIT); /* suspend ctrl by sw */
	if (rc < 0) {
		pr_err("Configure board setting failed, rc=%d\n", rc);
		return rc;
	}

	/* Enable necessary IRQs */
	rc = smb23x_masked_write(chip, IRQ_CFG_REG_9, 0xff,
			SAFETY_TIMER_IRQ_EN_BIT |
			BATT_MISSING_IRQ_EN_BIT |
			ITERM_IRQ_EN_BIT |
			HARD_TEMP_IRQ_EN_BIT |
			SOFT_TEMP_IRQ_EN_BIT |
			AICL_DONE_IRQ_EN_BIT |
			INOK_IRQ_EN_BIT);
	if (rc < 0) {
		pr_err("Configure IRQ failed, rc=%d\n", rc);
		return rc;
	}

	return rc;
}


static int hot_hard_irq_handler(struct smb23x_chip *chip, u8 rt_sts)
{
	pr_warn("rt_sts = 0x02%x\n", rt_sts);
	chip->batt_hot = !!rt_sts;
	return 0;
}

static int cold_hard_irq_handler(struct smb23x_chip *chip, u8 rt_sts)
{
	pr_debug("rt_sts = 0x02%x\n", rt_sts);
	chip->batt_cold = !!rt_sts;
	return 0;
}

static int hot_soft_irq_handler(struct smb23x_chip *chip, u8 rt_sts)
{
	pr_debug("rt_sts = 0x02%x\n", rt_sts);
	chip->batt_warm = !!rt_sts;
	return 0;
}

static int cold_soft_irq_handler(struct smb23x_chip *chip, u8 rt_sts)
{
	pr_debug("rt_sts = 0x02%x\n", rt_sts);
	chip->batt_cool = !!rt_sts;
	return 0;
}

static int batt_ov_irq_handler(struct smb23x_chip *chip, u8 rt_sts)
{
	pr_debug("rt_sts = 0x02%x\n", rt_sts);
	return 0;
}

static int batt_missing_irq_handler(struct smb23x_chip *chip, u8 rt_sts)
{
	pr_debug("rt_sts = 0x02%x\n", rt_sts);
	chip->batt_present = !rt_sts;
	return 0;
}

static int batt_low_irq_handler(struct smb23x_chip *chip, u8 rt_sts)
{
	pr_warn("rt_sts = 0x02%x\n", rt_sts);
	return 0;
}

static int pre_to_fast_irq_handler(struct smb23x_chip *chip, u8 rt_sts)
{
	pr_debug("rt_sts = 0x02%x\n", rt_sts);
	/*chip->batt_full = false;*/

	return 0;
}

static int chg_error_irq_handler(struct smb23x_chip *chip, u8 rt_sts)
{
	pr_warn("rt_sts = 0x02%x\n", rt_sts);
	return 0;
}

static int recharge_irq_handler(struct smb23x_chip *chip, u8 rt_sts)
{
	pr_debug("rt_sts = 0x02%x\n", rt_sts);
	/*chip->batt_full = !rt_sts;*/
	return 0;
}

static int taper_irq_handler(struct smb23x_chip *chip, u8 rt_sts)
{
	pr_debug("rt_sts = 0x02%x\n", rt_sts);
	return 0;
}

static int iterm_irq_handler(struct smb23x_chip *chip, u8 rt_sts)
{
	pr_debug("rt_sts = 0x02%x\n", rt_sts);
	/*chip->batt_full = !!rt_sts;*/

	return 0;
}


static int handle_usb_insertion(struct smb23x_chip *chip)
{
	power_supply_set_present(chip->usb_psy, chip->usb_present);
	power_supply_set_online(chip->usb_psy, true);

	return 0;
}

static int handle_usb_removal(struct smb23x_chip *chip)
{
	power_supply_set_supply_type(chip->usb_psy,
			POWER_SUPPLY_TYPE_UNKNOWN);
	power_supply_set_present(chip->usb_psy, chip->usb_present);
	power_supply_set_online(chip->usb_psy, false);

	return 0;
}

static int src_detect_irq_handler(struct smb23x_chip *chip, u8 rt_sts)
{
	bool usb_present = !!rt_sts;

	if (!chip->apsd_enabled)
		return 0;

	pr_info("chip->usb_present = %d, usb_present = %d\n",
					chip->usb_present, usb_present);
	if (chip->usb_present ^ usb_present) {
		wake_lock_timeout(&chip->charger_wake_lock, 5 * HZ);
	}

	if (usb_present && !chip->usb_present) {
		pr_info("Charger insert!\n");
		/*wake_lock(&chip->charger_valid_lock);*/
		chip->usb_present = usb_present;
		handle_usb_insertion(chip);
	} else if (!usb_present && chip->usb_present) {
		pr_info("Charger removed!\n");
		/*wake_unlock(&chip->charger_valid_lock);*/
		chip->usb_present = usb_present;
		smb23x_charging_disable(chip, CURRENT, false);
		chip->batt_full = false;
		chip->batt_warm_full = false;
		chip->batt_real_full = false;
		handle_usb_removal(chip);
	}

	return 0;
}

static int aicl_done_irq_handler(struct smb23x_chip *chip, u8 rt_sts)
{
	pr_debug("rt_sts = 0x02%x\n", rt_sts);
	return 0;
}

static int chg_timeout_irq_handler(struct smb23x_chip *chip, u8 rt_sts)
{
	pr_debug("rt_sts = 0x02%x\n", rt_sts);
	return 0;
}

static int pre_chg_timeout_irq_handler(struct smb23x_chip *chip, u8 rt_sts)
{
	pr_debug("rt_sts = 0x02%x\n", rt_sts);
	return 0;
}

static int usbin_ov_irq_handler(struct smb23x_chip *chip, u8 rt_sts)
{
	bool usb_present = !rt_sts;
	int health = !!rt_sts ? POWER_SUPPLY_HEALTH_OVERVOLTAGE :
				POWER_SUPPLY_HEALTH_GOOD;

	pr_debug("chip->usb_present = %d, usb_present = %d\n",
					chip->usb_present,  usb_present);
	if (chip->usb_present != usb_present) {
		chip->usb_present = usb_present;
		power_supply_set_present(chip->usb_psy, usb_present);
		power_supply_set_health_state(chip->usb_psy, health);
	}

	return 0;
}

#define IRQ_POLLING_MS	500
static void smb23x_irq_polling_work_fn(struct work_struct *work)
{
	struct smb23x_chip *chip = container_of(work, struct smb23x_chip,
					irq_polling_work.work);

	smb23x_stat_handler(chip->client->irq, (void *)chip);

	if (chip->usb_present)
		schedule_delayed_work(&chip->irq_polling_work,
			msecs_to_jiffies(IRQ_POLLING_MS));
}

/*
 * On some of the parts, the Non-volatile register values will
 * be reloaded upon unplug event. Even the unplug event won't be
 * detected because the IRQ isn't enabled by default in chip
 * internally.
 * Use polling to detect the unplug event, and after that, redo
 * hw_init() to repropgram the software configurations.
 */
static void reconfig_upon_unplug(struct smb23x_chip *chip)
{
	int rc;
	int reason;

	if (chip->workaround_flags & WRKRND_IRQ_POLLING) {
		if (chip->usb_present) {
			smb23x_stay_awake(&chip->smb23x_ws,
					WAKEUP_SRC_IRQ_POLLING);
			schedule_delayed_work(&chip->irq_polling_work,
					msecs_to_jiffies(IRQ_POLLING_MS));
		} else {
			pr_debug("restore software settings after unplug\n");
			smb23x_relax(&chip->smb23x_ws, WAKEUP_SRC_IRQ_POLLING);
			rc = smb23x_hw_init(chip);
			if (rc)
				pr_err("smb23x init upon unplug failed, rc=%d\n",
							rc);
			/*
			 * Retore the CHARGE_EN && USB_SUSPEND bit
			 * according to the status maintained in sw.
			 */
			mutex_lock(&chip->chg_disable_lock);
			reason = chip->chg_disabled_status;
			mutex_unlock(&chip->chg_disable_lock);
			rc = smb23x_charging_disable(chip, reason,
					!!reason ? true : false);
			if (rc < 0)
				pr_err("%s charging failed\n",
					!!reason ? "Disable" : "Enable");

			mutex_lock(&chip->usb_suspend_lock);
			reason = chip->usb_suspended_status;
			mutex_unlock(&chip->usb_suspend_lock);
			if (!(chip->workaround_flags & WRKRND_USB_SUSPEND)) {
				rc = smb23x_suspend_usb(chip, reason,
						!!reason ? true : false);
				if (rc < 0)
					pr_err("%suspend USB failed\n",
						!!reason ? "S" : "Un-s");
			}
		}
	}
}

static int usbin_uv_irq_handler(struct smb23x_chip *chip, u8 rt_sts)
{
	bool usb_present = !rt_sts;

	pr_debug("chip->usb_present = %d, usb_present = %d\n",
					chip->usb_present,  usb_present);
	if (chip->usb_present == usb_present)
		return 0;

	wake_lock_timeout(&chip->charger_wake_lock, 5 * HZ);
	if (!usb_present && chip->usb_present) {
		pr_info("Charger removed!\n");
		smb23x_charging_disable(chip, CURRENT, false);
		chip->batt_full = false;
		chip->batt_warm_full = false;
		chip->batt_real_full = false;
		/*wake_unlock(&chip->charger_valid_lock);*/
	} else if (usb_present && !chip->usb_present) {
		pr_info("Charger insert!\n");
		/*wake_lock(&chip->charger_valid_lock);*/
	}
	chip->usb_present = usb_present;
	reconfig_upon_unplug(chip);
	power_supply_set_present(chip->usb_psy, usb_present);

	return 0;
}

static int power_ok_irq_handler(struct smb23x_chip *chip, u8 rt_sts)
{
	pr_debug("rt_sts = 0x02%x\n", rt_sts);
	return 0;
}

static int die_temp_irq_handler(struct smb23x_chip *chip, u8 rt_sts)
{
	pr_warn("rt_sts = 0x02%x\n", rt_sts);
	return 0;
}

static struct irq_handler_info handlers[] = {
	{ IRQ_A_STATUS_REG, 0, 0,
		{
			{
				.name = "cold_soft",
				.smb_irq = cold_soft_irq_handler,
			},
			{
				.name = "hot_soft",
				.smb_irq = hot_soft_irq_handler,
			},
			{
				.name = "cold_hard",
				.smb_irq = cold_hard_irq_handler,
			},
			{
				.name = "hot_hard",
				.smb_irq = hot_hard_irq_handler,
			},
		},
	},
	{ IRQ_B_STATUS_REG, 0, 0,
		{
			{
				.name = "p2f",
				.smb_irq = pre_to_fast_irq_handler,
			},
			{
				.name = "batt_low",
				.smb_irq = batt_low_irq_handler,
			},
			{
				.name = "batt_missing",
				.smb_irq = batt_missing_irq_handler,
			},
			{
				.name = "batt_ov",
				.smb_irq = batt_ov_irq_handler,
			},
		},
	},
	{ IRQ_C_STATUS_REG, 0, 0,
		{
			{
				.name = "iterm",
				.smb_irq = iterm_irq_handler,
			},
			{
				.name = "taper",
				.smb_irq = taper_irq_handler,
			},
			{
				.name = "recharge",
				.smb_irq = recharge_irq_handler,
			},
			{
				.name = "chg_error",
				.smb_irq = chg_error_irq_handler,
			},
		},
	},
	{ IRQ_D_STATUS_REG, 0, 0,
		{
			{
				.name = "pre_chg_timeout",
				.smb_irq = pre_chg_timeout_irq_handler,
			},
			{
				.name = "chg_timeout",
				.smb_irq = chg_timeout_irq_handler,
			},
			{
				.name = "aicl_done",
				.smb_irq = aicl_done_irq_handler,
			},
			{
				.name = "src_detect",
				.smb_irq = src_detect_irq_handler,
			},
		},
	},
	{ CHG_STATUS_A_REG, 0, 0,
		{
			{
				.name = "die_temp",
				.smb_irq = die_temp_irq_handler,
			},
			{
				.name = "power_ok",
				.smb_irq = power_ok_irq_handler,
			},
			{
				.name = "usbin_uv",
				.smb_irq = usbin_uv_irq_handler,
			},
			{
				.name = "usbin_ov",
				.smb_irq = usbin_ov_irq_handler,
			},
		},
	},
};

#define UPDATE_IRQ_STAT(irq_reg, value) \
	handlers[irq_reg - IRQ_A_STATUS_REG].prev_val = value
static int smb23x_determine_initial_status(struct smb23x_chip *chip)
{
	int rc = 0;
	u8 reg;

	rc = smb23x_read(chip, IRQ_A_STATUS_REG, &reg);
	if (rc < 0) {
		pr_err("Read IRQ_A failed, rc=%d\n", rc);
		return rc;
	}
	UPDATE_IRQ_STAT(IRQ_A_STATUS_REG, reg);
	if (reg & HOT_HARD_BIT)
		chip->batt_hot = true;
	else if (reg & COLD_HARD_BIT)
		chip->batt_cold = true;
	else if (reg & HOT_SOFT_BIT)
		chip->batt_warm = true;
	else if (reg & COLD_SOFT_BIT)
		chip->batt_cool = true;

	chip->batt_present = true;
	rc = smb23x_read(chip, IRQ_B_STATUS_REG, &reg);
	if (rc < 0) {
		pr_err("Read IRQ_B failed, rc=%d\n", rc);
		return rc;
	}
	UPDATE_IRQ_STAT(IRQ_B_STATUS_REG, reg);
	if (reg & BATT_ABSENT_BIT)
		chip->batt_present = false;

	rc = smb23x_read(chip, IRQ_C_STATUS_REG, &reg);
	if (rc < 0) {
		pr_err("Read IRQ_C failed, rc=%d\n", rc);
		return rc;
	}
	UPDATE_IRQ_STAT(IRQ_C_STATUS_REG, reg);
	if (reg & ITERM_BIT)
		smb23x_charger_eoc(chip);
		/*chip->batt_full = true;*/

	rc = smb23x_read(chip, IRQ_D_STATUS_REG, &reg);
	if (rc < 0) {
		pr_err("Read IRQ_D failed, rc=%d\n", rc);
		return rc;
	}
	UPDATE_IRQ_STAT(IRQ_D_STATUS_REG, reg);
	if (chip->apsd_enabled && (reg & APSD_DONE_BIT))
		chip->usb_present = true;

	rc = smb23x_read(chip, CHG_STATUS_A_REG, &reg);
	if (rc < 0) {
		pr_err("Read CHG_STATUS_A failed, rc=%d\n", rc);
		return rc;
	}
	UPDATE_IRQ_STAT(CHG_STATUS_A_REG, reg);
	if (!(reg & USBIN_OV_BIT) && !(reg & USBIN_UV_BIT))
		chip->usb_present = true;
	else if (reg & USBIN_OV_BIT)
		power_supply_set_health_state(chip->usb_psy,
				POWER_SUPPLY_HEALTH_OVERVOLTAGE);

	if (chip->usb_present && chip->apsd_enabled) {
		handle_usb_insertion(chip);
	} else if (chip->usb_present) {
		power_supply_set_present(chip->usb_psy, chip->usb_present);
		reconfig_upon_unplug(chip);
	}
	chip->high_temp_threshold = chip->cfg_warm_bat_decidegc;
	chip->low_temp_threshold = chip->cfg_cool_bat_decidegc;
	chip->tm_state = SMB_TEMP_NORMAL_STATE;

	return rc;
}

static int smb23x_irq_read(struct smb23x_chip *chip)
{
	int rc, i;

	for (i = 0; i < ARRAY_SIZE(handlers); i++) {
		rc = smb23x_read(chip, handlers[i].stat_reg,
						&handlers[i].val);
		if (rc < 0) {
			pr_err("Couldn't read %d rc = %d\n",
					handlers[i].stat_reg, rc);
			handlers[i].val = 0;
			continue;
		}
	}

	return rc;
}

#define IRQ_LATCHED_MASK	0x02
#define IRQ_STATUS_MASK		0x01
#define BITS_PER_IRQ		2
static irqreturn_t smb23x_stat_handler(int irq, void *dev_id)
{
	struct smb23x_chip *chip = dev_id;
	int i, j;
	u8 triggered;
	u8 changed;
	u8 rt_stat, prev_rt_stat;
	int rc;
	int handler_count = 0;

	pr_debug("entering\n");
	mutex_lock(&chip->irq_complete);
	chip->irq_waiting = true;
	if (!chip->resume_completed) {
		pr_debug("IRQ triggered before device-resume\n");
		disable_irq_nosync(irq);
		mutex_unlock(&chip->irq_complete);
		return IRQ_HANDLED;
	}
	chip->irq_waiting = false;

	smb23x_irq_read(chip);
	for (i = 0; i < ARRAY_SIZE(handlers); i++) {
		for (j = 0; j < ARRAY_SIZE(handlers[i].irq_info); j++) {
			triggered = handlers[i].val
			       & (IRQ_LATCHED_MASK << (j * BITS_PER_IRQ));
			rt_stat = handlers[i].val
				& (IRQ_STATUS_MASK << (j * BITS_PER_IRQ));
			prev_rt_stat = handlers[i].prev_val
				& (IRQ_STATUS_MASK << (j * BITS_PER_IRQ));
			changed = prev_rt_stat ^ rt_stat;

			if (triggered || changed)
				rt_stat ? handlers[i].irq_info[j].high++ :
						handlers[i].irq_info[j].low++;

			if ((triggered || changed)
				&& handlers[i].irq_info[j].smb_irq != NULL) {
				handler_count++;
				rc = handlers[i].irq_info[j].smb_irq(chip,
								rt_stat);
				if (rc < 0)
					pr_err("Couldn't handle %d irq for reg 0x%02x rc = %d\n",
						j, handlers[i].stat_reg, rc);
			}
		}
		handlers[i].prev_val = handlers[i].val;
	}

	pr_debug("handler count = %d\n", handler_count);
	if (handler_count) {
		pr_debug("batt psy changed\n");
		power_supply_changed(&chip->batt_psy);
		if (chip->usb_psy) {
			pr_debug("usb psy changed\n");
			power_supply_changed(chip->usb_psy);
		}
	}

	mutex_unlock(&chip->irq_complete);

	return IRQ_HANDLED;
}

static enum power_supply_property smb23x_battery_properties[] = {
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
};

static int smb23x_get_prop_batt_health(struct smb23x_chip *chip)
{
	int health;

	if (chip->batt_hot)
		health = POWER_SUPPLY_HEALTH_OVERHEAT;
	else if (chip->batt_cold)
		health = POWER_SUPPLY_HEALTH_COLD;
	else if (chip->batt_warm)
		health = POWER_SUPPLY_HEALTH_WARM;
	else if (chip->batt_cool)
		health = POWER_SUPPLY_HEALTH_COOL;
	else
		health = POWER_SUPPLY_HEALTH_GOOD;

	return health;
}

static int smb23x_get_prop_batt_status(struct smb23x_chip *chip)
{
	int rc, status;
	u8 tmp;

	if (chip->batt_full)
		return POWER_SUPPLY_STATUS_FULL;

	rc = smb23x_read(chip, CHG_STATUS_B_REG, &tmp);
	if (rc < 0) {
		pr_err("Read STATUS_B failed, rc=%d\n", rc);
		return POWER_SUPPLY_STATUS_UNKNOWN;
	}

	status = tmp & CHARGE_TYPE_MASK;
	return (status == NO_CHARGE_VAL) ? POWER_SUPPLY_STATUS_DISCHARGING :
						POWER_SUPPLY_STATUS_CHARGING;
}

static int smb23x_get_prop_battery_charging_enabled(struct smb23x_chip *chip)
{
	int rc, status;
	u8 tmp;

	rc = smb23x_read(chip, CHG_STATUS_B_REG, &tmp);
	if (rc < 0) {
		pr_err("Read STATUS_B failed, rc=%d\n", rc);
		return !chip->chg_disabled_status;
	}

	status = tmp & CHARGE_EN_STS_BIT;
	if (status && !chip->chg_disabled_status)
		return 1;
	else
		return 0;
}

static int smb23x_get_prop_charging_enabled(struct smb23x_chip *chip)
{
	return !chip->usb_suspended_status;
}

static int smb23x_get_prop_batt_present(struct smb23x_chip *chip)
{
	return chip->batt_present ? 1 : 0;
}

static int smb23x_get_prop_charge_type(struct smb23x_chip *chip)
{
	int rc, status;
	u8 tmp;

	rc = smb23x_read(chip, CHG_STATUS_B_REG, &tmp);
	if (rc < 0) {
		pr_err("Read STATUS_B failed, rc=%d\n", rc);
		goto exit;
	}

	status = tmp & CHARGE_TYPE_MASK;
	if (status == NO_CHARGE_VAL)
		return POWER_SUPPLY_CHARGE_TYPE_NONE;
	else if (status == PRE_CHARGE_VAL)
		return POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
	else if (status == FAST_CHARGE_VAL)
		return POWER_SUPPLY_CHARGE_TYPE_FAST;
	else if (status == TAPER_CHARGE_VAL)
		return POWER_SUPPLY_CHARGE_TYPE_TAPER;
exit:
	return POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
}

//extern int set_cblpwr_pon_disable(void);
static int  set_cblpwr_pon = 1;
static int smb23x_cblpwr_pon_disable(const char *val, struct kernel_param *kp)
{
	int ret;

	ret = param_set_int(val, kp);
	if (ret) {
		pr_err("error setting value %d\n", ret);
		return ret;
	}
	if (set_cblpwr_pon == 0) {
		//set_cblpwr_pon_disable();
		pr_info("set_cblpwr_pon_disable\n");
	}

	return ret;
}
module_param_call(set_cblpwr_pon, smb23x_cblpwr_pon_disable, param_get_uint,
					&set_cblpwr_pon, 0644);

#define DEFAULT_BATT_TEMP	250
#define HOT_TEMP 600

static int smb23x_get_prop_batt_temp(struct smb23x_chip *chip, int *batt_temp)
{
	int rc;
	struct qpnp_vadc_result result;

	if (is_fake_battery) {
		*batt_temp = DEFAULT_BATT_TEMP;
		return 0;
	}

	rc = qpnp_vadc_read(chip->vadc_dev, LR_MUX1_BATT_THERM, &result);
	if (rc) {
		pr_err("error reading adc channel = %d, rc = %d\n",
					LR_MUX1_BATT_THERM, rc);
		return rc;
	}

	pr_debug("batt_temp phy = %lld meas = 0x%llx\n",
			result.physical, result.measurement);

	*batt_temp = (int)result.physical;

	//if (*batt_temp > HOT_TEMP)
	//	set_cblpwr_pon_disable();

	return 0;
}

static int smb23x_system_temp_level_set(struct smb23x_chip *chip, int lvl_sel)
{
	int rc = 0;

	if (!chip->cfg_thermal_mitigation) {
		pr_err("Thermal mitigation not supported\n");
		return (-EINVAL);
	}

	if (lvl_sel < 0) {
		pr_err("Unsupported level selected %d\n", lvl_sel);
		return (-EINVAL);
	}

	if (lvl_sel >= chip->thermal_levels) {
		pr_err("Unsupported level selected %d forcing %d\n", lvl_sel,
				chip->thermal_levels - 1);
		lvl_sel = chip->thermal_levels - 1;
	}

	if (lvl_sel == chip->therm_lvl_sel)
		return 0;

	mutex_lock(&chip->icl_set_lock);
	chip->therm_lvl_sel = lvl_sel;
	pr_info("chip->therm_lvl_sel:%d\n", chip->therm_lvl_sel);
	rc = smb23x_set_appropriate_usb_current(chip);
	if (rc)
		pr_err("Couldn't set USB current rc = %d\n", rc);

	mutex_unlock(&chip->icl_set_lock);
	return rc;
}

static int smb23x_battery_get_property(struct power_supply *psy,
				enum power_supply_property prop,
				union power_supply_propval *val)
{
	struct smb23x_chip *chip = container_of(psy,
			struct smb23x_chip, batt_psy);
	int value = 0, rc;

	switch (prop) {
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = smb23x_get_prop_batt_health(chip);
		break;
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = smb23x_get_prop_batt_status(chip);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = smb23x_get_prop_batt_present(chip);
		break;
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		val->intval = smb23x_get_prop_charging_enabled(chip);
		break;
	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
		val->intval = smb23x_get_prop_battery_charging_enabled(chip);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = smb23x_get_prop_charge_type(chip);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = smb23x_get_prop_batt_capacity(chip);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		 rc = smb23x_get_prop_batt_temp(chip, &value);
		 if (rc < 0)
			value = DEFAULT_BATT_TEMP;
		 val->intval = value;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = smb23x_get_prop_batt_voltage(chip) * 1000;
		break;
	case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
		val->intval = chip->therm_lvl_sel;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = smb23x_get_current_now(chip);
		break;
	default:
		return (-EINVAL);
	}
	pr_debug("get_property: prop(%d) = %d\n", (int)prop, (int)val->intval);

	return 0;
}

static int smb23x_battery_set_property(struct power_supply *psy,
				enum power_supply_property prop,
				const union power_supply_propval *val)
{
	struct smb23x_chip *chip = container_of(psy,
			struct smb23x_chip, batt_psy);
	int rc;

	pr_debug("set_property: prop(%d) = %d\n", (int)prop, (int)val->intval);

	switch (prop) {
	case POWER_SUPPLY_PROP_STATUS:
		if (!chip->cfg_bms_controlled_charging)
			return (-EINVAL);

		switch (val->intval) {
		case POWER_SUPPLY_STATUS_FULL:
			pr_debug("BMS notify: battery FULL!\n");
			chip->batt_full = true;
			rc = smb23x_charging_disable(chip, BMS, true);
			if (rc < 0) {
				pr_err("Disable charging for BMS failed, rc=%d\n",
						rc);
				return rc;
			}
			break;
		case POWER_SUPPLY_STATUS_CHARGING:
			pr_debug("BMS notify: battery CHARGING!\n");
			chip->batt_full = false;
			rc = smb23x_charging_disable(chip, BMS, false);
			if (rc < 0) {
				pr_err("Enable charging for BMS failed, rc=%d\n",
						rc);
				return rc;
			}
			break;
		case POWER_SUPPLY_STATUS_DISCHARGING:
			pr_debug("BMS notify: battery DISCHARGING!\n");
			chip->batt_full = false;
			break;
		default:
			break;
		}
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		smb23x_suspend_usb(chip, USER, !val->intval);
		power_supply_changed(&chip->batt_psy);
		power_supply_changed(chip->usb_psy);
		break;
	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
		smb23x_charging_disable(chip, USER, !val->intval);
		power_supply_changed(&chip->batt_psy);
		break;
	case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
		smb23x_system_temp_level_set(chip, val->intval);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		chip->fake_battery_soc = val->intval;
		power_supply_changed(&chip->batt_psy);
		break;
	default:
		return (-EINVAL);
	}

	return 0;
}

static int smb23x_battery_is_writeable(struct power_supply *psy,
					enum power_supply_property prop)
{
	int rc;

	switch (prop) {
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
	case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
	case POWER_SUPPLY_PROP_CAPACITY:
		rc = 1;
		break;
	default:
		rc = 0;
		break;
	}
	return rc;
}

static void smb23x_get_bms_psy(struct smb23x_chip *chip)
{
	if (chip->bms_psy_name && chip->bms_psy == NULL) {
		if (chip->board_ver_id == 1) {
			chip->bms_psy = power_supply_get_by_name((char *)chip->bms_psy_name);
		} else {
			chip->bms_psy = power_supply_get_by_name("bms");
		}
	}
}
static void smb23x_external_power_changed(struct power_supply *psy)
{
	struct smb23x_chip *chip = container_of(psy,
			struct smb23x_chip, batt_psy);
	union power_supply_propval prop = {0,};
	int icl = 0, rc;
	bool online;

	smb23x_get_bms_psy(chip);

	rc = chip->usb_psy->get_property(chip->usb_psy,
			POWER_SUPPLY_PROP_CURRENT_MAX, &prop);
	if (rc < 0)
		pr_err("Get CURRENT_MAX from usb failed, rc=%d\n", rc);
	else
		icl = prop.intval / 1000;
	pr_info("current_limit = %d\n", icl);

	if (chip->usb_psy_ma != icl) {
		mutex_lock(&chip->icl_set_lock);
		chip->usb_psy_ma = icl;
		rc = smb23x_set_appropriate_usb_current(chip);
		mutex_unlock(&chip->icl_set_lock);
		if (rc < 0)
			pr_err("Set appropriate current failed, rc=%d\n", rc);
	}

	rc = chip->usb_psy->get_property(chip->usb_psy,
			POWER_SUPPLY_PROP_ONLINE, &prop);
	if (rc < 0)
		pr_err("Get ONLINE from usb failed, rc=%d\n", rc);
	else
		online = !!prop.intval;

	if (chip->usb_present && chip->usb_psy_ma != 0) {
		if (!online && !chip->apsd_enabled)
			power_supply_set_online(chip->usb_psy, true);
	} else if (online && !chip->apsd_enabled) {
		power_supply_set_online(chip->usb_psy, false);
	}
}

#define LAST_CNFG_REG	0x1F
static int show_cnfg_regs(struct seq_file *m, void *data)
{
	struct smb23x_chip *chip = m->private;
	int rc;
	u8 reg;
	u8 addr;

	for (addr = CFG_REG_0; addr <= I2C_COMM_CFG_REG; addr++) {
		rc = smb23x_read(chip, addr, &reg);
		if (!rc)
			seq_printf(m, "0x%02x = 0x%02x\n", addr, reg);
	}

	return 0;
}

static int cnfg_debugfs_open(struct inode *inode, struct file *file)
{
	struct smb23x_chip *chip = inode->i_private;

	return single_open(file, show_cnfg_regs, chip);
}

static const struct file_operations cnfg_debugfs_ops = {
	.owner		= THIS_MODULE,
	.open		= cnfg_debugfs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int show_cmd_regs(struct seq_file *m, void *data)
{
	struct smb23x_chip *chip = m->private;
	int rc;
	u8 reg;
	u8 addr;

	for (addr = CMD_REG_0; addr <= CMD_REG_1; addr++) {
		rc = smb23x_read(chip, addr, &reg);
		if (!rc)
			seq_printf(m, "0x%02x = 0x%02x\n", addr, reg);
	}

	return 0;
}

static int cmd_debugfs_open(struct inode *inode, struct file *file)
{
	struct smb23x_chip *chip = inode->i_private;

	return single_open(file, show_cmd_regs, chip);
}

static const struct file_operations cmd_debugfs_ops = {
	.owner		= THIS_MODULE,
	.open		= cmd_debugfs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int show_status_regs(struct seq_file *m, void *data)
{
	struct smb23x_chip *chip = m->private;
	int rc;
	u8 reg;
	u8 addr;

	for (addr = IRQ_A_STATUS_REG; addr <= AICL_STATUS_REG; addr++) {
		rc = smb23x_read(chip, addr, &reg);
		if (!rc)
			seq_printf(m, "0x%02x = 0x%02x\n", addr, reg);
	}

	return 0;
}

static int status_debugfs_open(struct inode *inode, struct file *file)
{
	struct smb23x_chip *chip = inode->i_private;

	return single_open(file, show_status_regs, chip);
}

static const struct file_operations status_debugfs_ops = {
	.owner		= THIS_MODULE,
	.open		= status_debugfs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int show_irq_count(struct seq_file *m, void *data)
{
	int i, j, total = 0;

	for (i = 0; i < ARRAY_SIZE(handlers); i++)
		for (j = 0; j < 4; j++) {
			seq_printf(m, "%s=%d\t(high=%d low=%d)\n",
						handlers[i].irq_info[j].name,
						handlers[i].irq_info[j].high
						+ handlers[i].irq_info[j].low,
						handlers[i].irq_info[j].high,
						handlers[i].irq_info[j].low);
			total += (handlers[i].irq_info[j].high
					+ handlers[i].irq_info[j].low);
		}

	seq_printf(m, "\n\tTotal = %d\n", total);

	return 0;
}

static int irq_count_debugfs_open(struct inode *inode, struct file *file)
{
	struct smb23x_chip *chip = inode->i_private;

	return single_open(file, show_irq_count, chip);
}

static const struct file_operations irq_count_debugfs_ops = {
	.owner		= THIS_MODULE,
	.open		= irq_count_debugfs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int get_reg(void *data, u64 *val)
{
	struct smb23x_chip *chip = data;
	int rc;
	u8 temp;

	rc = smb23x_read(chip, chip->peek_poke_address, &temp);
	if (rc < 0) {
		pr_err("Couldn't read reg %x rc = %d\n",
			chip->peek_poke_address, rc);
		return -EAGAIN;
	}
	*val = temp;
	return 0;
}

static int set_reg(void *data, u64 val)
{
	struct smb23x_chip *chip = data;
	int rc;
	u8 temp;

	temp = (u8) val;
	rc = smb23x_write(chip, chip->peek_poke_address, temp);
	if (rc < 0) {
		pr_err("Couldn't write 0x%02x to 0x%02x rc= %d\n",
			chip->peek_poke_address, temp, rc);
		return -EAGAIN;
	}
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(poke_poke_debug_ops, get_reg, set_reg, "0x%02llx\n");

static int force_irq_set(void *data, u64 val)
{
	struct smb23x_chip *chip = data;

	smb23x_stat_handler(chip->client->irq, data);
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(force_irq_ops, NULL, force_irq_set, "0x%02llx\n");

static int create_debugfs_entries(struct smb23x_chip *chip)
{
	chip->debug_root = debugfs_create_dir("smb23x", NULL);
	if (!chip->debug_root)
		pr_err("Couldn't create debug dir\n");

	if (chip->debug_root) {
		struct dentry *ent;

		ent = debugfs_create_file("config_registers", S_IFREG | S_IRUGO,
					  chip->debug_root, chip,
					  &cnfg_debugfs_ops);
		if (!ent)
			pr_err("Couldn't create cnfg debug file\n");

		ent = debugfs_create_file("status_registers", S_IFREG | S_IRUGO,
					  chip->debug_root, chip,
					  &status_debugfs_ops);
		if (!ent)
			pr_err("Couldn't create status debug file\n");

		ent = debugfs_create_file("cmd_registers", S_IFREG | S_IRUGO,
					  chip->debug_root, chip,
					  &cmd_debugfs_ops);
		if (!ent)
			pr_err("Couldn't create cmd debug file\n");

		ent = debugfs_create_x32("address", S_IFREG | S_IWUSR | S_IRUGO,
					  chip->debug_root,
					  &(chip->peek_poke_address));
		if (!ent)
			pr_err("Couldn't create address debug file\n");

		ent = debugfs_create_file("data", S_IFREG | S_IWUSR | S_IRUGO,
					  chip->debug_root, chip,
					  &poke_poke_debug_ops);
		if (!ent)
			pr_err("Couldn't create data debug file\n");

		ent = debugfs_create_file("force_irq",
					  S_IFREG | S_IWUSR | S_IRUGO,
					  chip->debug_root, chip,
					  &force_irq_ops);
		if (!ent)
			pr_err("Couldn't create force_irq debug file\n");

		ent = debugfs_create_file("irq_count", S_IFREG | S_IRUGO,
					  chip->debug_root, chip,
					  &irq_count_debugfs_ops);
		if (!ent)
			pr_err("Couldn't create irq_count debug file\n");
	}
	return 0;
}

static char *batt_supplied_to[] = {
	"bms",
};

static void smb23x_irq_polling_wa_check(struct smb23x_chip *chip)
{
	int rc;
	u8 reg;

	rc = smb23x_read(chip, NV_CFG_REG, &reg);
	if (rc) {
		pr_err("Read NV_CFG_REG failed, rc=%d\n", rc);
		return;
	}

	if (!(reg & UNPLUG_RELOAD_DIS_BIT))
		chip->workaround_flags |= WRKRND_IRQ_POLLING;

	pr_debug("use polling: %d\n", !(reg & UNPLUG_RELOAD_DIS_BIT));
}
#define MIN_IBAT_MA		100
#define MAX_IBAT_MA		500
int set_ibat(struct smb23x_chip *chip, int ma)
{
	int rc,i;

	if(ma < MIN_IBAT_MA){
		ma = MIN_IBAT_MA;
		pr_err("bad battery charge current ma =%d asked to set\n",ma);
	}else if(ma > MAX_IBAT_MA){
		ma = MAX_IBAT_MA;
		pr_err("bad battery charge current ma =%d asked to set\n",ma);
	}
	/* fastchg current setting */
	i = find_closest_in_ascendant_list(
		ma, fastchg_current_ma_table,
		ARRAY_SIZE(fastchg_current_ma_table));

	rc = smb23x_masked_write(chip, CFG_REG_2,
			FASTCHG_CURR_MASK, i);
	if (rc < 0) {
		pr_err("Set fastchg current failed, rc=%d\n", rc);
		return rc;
	}

	pr_debug("ibat current set to = %d\n", fastchg_current_ma_table[i]);

	return 0;
}

void smb23x_set_appropriate_ibat(struct smb23x_chip *chip)
{
	int chg_current = chip->cfg_fastchg_ma;
	pr_debug("chip->batt_cool:%d, chip->batt_warm:%d, chip->cfg_cool_comp_ma:%d\n",
			chip->batt_cool, chip->batt_warm, chip->cfg_cool_comp_ma);
	pr_debug("chip->cfg_warm_comp_ma:%d, chip->cfg_fastchg_ma:%d\n",
			chip->cfg_warm_comp_ma, chip->cfg_fastchg_ma);

	if (chip->batt_cool){
		chg_current = min(chg_current, chip->cfg_cool_comp_ma);
	}
	if (chip->batt_warm)
	{
		chg_current = min(chg_current, chip->cfg_warm_comp_ma);
	}
	pr_debug("setting %d mA\n", chg_current);

	set_ibat(chip, chg_current);
}

static int smb23x_float_voltage_set(struct smb23x_chip *chip, int vfloat_mv)
{
	u8 temp;
	int rc;
	pr_debug("set vfloat_mv:%d\n",vfloat_mv);
	if ((vfloat_mv < MIN_FLOAT_MV) || (vfloat_mv > MAX_FLOAT_MV)) {
		pr_err("bad float voltage mv =%d asked to set\n",
					vfloat_mv);
		return -EINVAL;
	}

	temp = (vfloat_mv - MIN_FLOAT_MV) / FLOAT_STEP_MV;
	rc = smb23x_masked_write(chip, CFG_REG_3,
			FLOAT_VOLTAGE_MASK, temp);
	if (rc < 0) {
		pr_err("Set float voltage failed, rc=%d\n", rc);
		return rc;
	}

	return rc;
}

static void smb23x_set_appropriate_float_voltage(struct smb23x_chip *chip)
{
	pr_debug("is_cold=%d is_cool=%d is_warm=%d cool_bat_mv=%d mv warm_bat_mv=%d mv\n",
			chip->batt_cold,
			chip->batt_cool,
			chip->batt_warm,
			chip->cfg_cool_comp_mv,
			chip->cfg_warm_comp_mv
			);
	pr_debug("vfloat_mv=%dmv warm_resume_delta_mv=%dmv\n",
			chip->cfg_vfloat_mv,
			chip->cfg_resume_delta_mv
			);
	if (chip->batt_cool)
	{
		smb23x_float_voltage_set(chip, chip->cfg_cool_comp_mv);
	}
	else if (chip->batt_warm)
	{
		smb23x_float_voltage_set(chip, chip->cfg_warm_comp_mv);
	}
	else
	{
		smb23x_float_voltage_set(chip, chip->cfg_vfloat_mv);
	}
}

#define SMB_HOT_TEMP_DEFAULT        500
#define SMB_COLD_TEMP_DEFAULT       0
#define SMB_WARM_TEMP_DEFAULT       450
#define SMB_COOL_TEMP_DEFAULT       100
#define HYSTERISIS_DECIDEGC         20
#define MAX_TEMP                    800
#define MIN_TEMP                   -300

/*
 * smb23x_charge_current_limit
 * set appropriate voltages and currents.
 *
 * (TODO)Note that when the battery is hot or cold, the charger
 * driver will not resume with SoC. Only vbatdet is used to
 * determine resume of charging.
 */

/* NOTE(by ZTE JZN):
*  if vbatt>warm_bat_mv and charging is enabled, battery will discharging and iusb=0 ;
*  if vbatt>warm_bat_mv and charging is disabled, iusb will be the currnt source and ibat=0 .
*  so, there is a bug in the below codes:if vbatt>warm_bat_mv,battery will discharging and iusb=0
*/
static void smb23x_charge_current_limit(struct smb23x_chip *chip)
{
	smb23x_set_appropriate_usb_current(chip);
	smb23x_set_appropriate_ibat(chip);
	smb23x_set_appropriate_float_voltage(chip);
	smb23x_charging_disable(chip, THERMAL, false);
	power_supply_changed(&chip->batt_psy);

}
static void smb23x_update_temp_state(struct smb23x_chip *chip)
{
	chip->batt_hot = (chip->tm_state == SMB_TEMP_HOT_STATE) ? true : false;
	chip->batt_warm = (chip->tm_state == SMB_TEMP_WARM_STATE) ? true : false;
	chip->batt_cool = (chip->tm_state == SMB_TEMP_COOL_STATE) ? true : false;
	chip->batt_cold = (chip->tm_state == SMB_TEMP_COLD_STATE) ? true : false;
}
static void smb23x_temp_state_changed(struct smb23x_chip *chip)
{
	smb23x_update_temp_state(chip);

	switch (chip->tm_state) {
	case(SMB_TEMP_COLD_STATE):
		chip->low_temp_threshold = MIN_TEMP;
		chip->high_temp_threshold = chip->cfg_cold_bat_decidegc + HYSTERISIS_DECIDEGC;
		smb23x_charging_disable(chip, THERMAL, true);
		power_supply_changed(&chip->batt_psy);
		break;
	case(SMB_TEMP_COOL_STATE):
		chip->low_temp_threshold = chip->cfg_cold_bat_decidegc;
		chip->high_temp_threshold = chip->cfg_cool_bat_decidegc + HYSTERISIS_DECIDEGC;
		smb23x_charge_current_limit(chip);
		break;
	case(SMB_TEMP_NORMAL_STATE):
		chip->low_temp_threshold = chip->cfg_cool_bat_decidegc;
		chip->high_temp_threshold = chip->cfg_warm_bat_decidegc;
		smb23x_charge_current_limit(chip);
		break;
	case(SMB_TEMP_WARM_STATE):
		chip->low_temp_threshold = chip->cfg_warm_bat_decidegc - HYSTERISIS_DECIDEGC;
		chip->high_temp_threshold = chip->cfg_hot_bat_decidegc;
		smb23x_charge_current_limit(chip);
		break;
	case(SMB_TEMP_HOT_STATE):
		chip->low_temp_threshold = chip->cfg_hot_bat_decidegc - HYSTERISIS_DECIDEGC;
		chip->high_temp_threshold = MAX_TEMP;
		smb23x_charging_disable(chip, THERMAL, true);
		power_supply_changed(&chip->batt_psy);
		break;
	default:
		break;
	}
}

static void smb23x_set_bms_temp(struct smb23x_chip *chip, int temp)
{
	union power_supply_propval ret = {0,};

	if (chip->bms_psy) {
		ret.intval = temp;
		chip->bms_psy->set_property(chip->bms_psy,
			POWER_SUPPLY_PROP_TEMP, &ret);
	}

}

#define NORMAL_HEARTBEAT_PRINT_COUNT 10
static void update_heartbeat(struct smb23x_chip *chip)
{
	int temp, voltage, cap, status, charge_type, present, chg_current;
	static int old_temp = 0;
	static int old_cap = 0;
	static int old_status = 0;
	static int old_present = 0;
	static int printk_counter = 0;

	if (chip == NULL) {
		pr_err("pmic fatal error:the_chip=null\n!!");
		return;
	}

	smb23x_get_prop_batt_temp(chip, &temp);
	voltage		=	smb23x_get_prop_batt_voltage(chip);
	cap			=	smb23x_get_prop_batt_capacity(chip);
	status		=	smb23x_get_prop_batt_status(chip);
	charge_type	=	smb23x_get_prop_charge_type(chip);
	present		=	smb23x_get_prop_batt_present(chip);
	chg_current	=	smb23x_get_current_now(chip);

	printk_counter++;

	if ((abs(temp-old_temp) >= 1) || (old_cap != cap) || (old_status != status)
	|| (old_present != present) || (printk_counter >= NORMAL_HEARTBEAT_PRINT_COUNT)) {
		pr_info("temp=%d, vol=%d, cap=%d, status=%d,chg_state=%d,current=%d\n",
				temp / 10, voltage, cap, status, charge_type, chg_current);
		pr_info("present=%d,usb_present=%d, batt_full=%d, tm_state=%d,batt_real_full:%d\n",
				present, chip->usb_present, chip->batt_full, chip->tm_state, chip->batt_real_full);
		if (chip->board_ver_id == 1)
			pr_info("***current_avg=%d, voltage_avg=%d, board_id=%d, batt_warm_full=%d, full_cap=%d, repcap=%d\n",
				smb23x_get_current_avg(chip), smb23x_get_prop_batt_voltage_avg(chip),
				chip->board_ver_id, chip->batt_warm_full,
				smb23x_get_fullcap(chip), smb23x_get_repcap(chip));

		old_temp = temp;
		old_cap = cap;
		old_status = status;
		old_present = present;
		printk_counter = 0;
		power_supply_changed(&chip->batt_psy);
	}
}

#define TEMP_DETECT_WORK_DELAY_2S 2000
#define TEMP_DETECT_WORK_DELAY_30S 30000
static void smb23x_temp_control_func(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct smb23x_chip *chip = container_of(dwork, struct smb23x_chip, temp_control_work);
	int state = SMB_TM_NORMAL_STATE;
	int temp = 0;
	int period = TEMP_DETECT_WORK_DELAY_2S;

	if (!chip->resume_completed) {
		pr_err("smb23x_temp_control_func launched before device-resume, schedule to 2s later\n");
		schedule_delayed_work(&chip->temp_control_work,round_jiffies_relative(msecs_to_jiffies(2000)));
		return;
	}

	update_heartbeat(chip);
	smb23x_get_prop_batt_temp(chip, &temp);
	if (chip->board_ver_id == 1) {
		smb23x_charger_eoc(chip);
		smb23x_set_bms_temp(chip, temp);
	}

	if (temp > MAX_TEMP)
		temp = MAX_TEMP;
	if (temp < MIN_TEMP)
		temp = MIN_TEMP;

	if (temp > chip->high_temp_threshold)
		state = SMB_TM_HIGHER_STATE;
	else if (temp < chip->low_temp_threshold)
		state = SMB_TM_LOWER_STATE;
	else
		state = SMB_TM_NORMAL_STATE;

	mutex_lock(&chip->jeita_configure_lock);

	if (state == SMB_TM_HIGHER_STATE) {
		switch (chip->tm_state) {
		case(SMB_TEMP_COLD_STATE):
			chip->tm_state = SMB_TEMP_COOL_STATE;
			break;
		case(SMB_TEMP_COOL_STATE):
			chip->tm_state = SMB_TEMP_NORMAL_STATE;
			break;
		case(SMB_TEMP_NORMAL_STATE):
			chip->tm_state = SMB_TEMP_WARM_STATE;
			break;
		case(SMB_TEMP_WARM_STATE):
			chip->tm_state = SMB_TEMP_HOT_STATE;
			break;
		}
		smb23x_temp_state_changed(chip);
	} else if (state == SMB_TM_LOWER_STATE) {
		switch (chip->tm_state) {
		case(SMB_TEMP_COOL_STATE):
			chip->tm_state = SMB_TEMP_COLD_STATE;
			break;
		case(SMB_TEMP_NORMAL_STATE):
			chip->tm_state = SMB_TEMP_COOL_STATE;
			break;
		case(SMB_TEMP_WARM_STATE):
			chip->tm_state = SMB_TEMP_NORMAL_STATE;
			break;
		case(SMB_TEMP_HOT_STATE):
			chip->tm_state = SMB_TEMP_WARM_STATE;
			break;
		}
		smb23x_temp_state_changed(chip);
	} else {
		pr_debug("SMB_TM_NORMAL_STATE");
		goto check_again;
	}
check_again:
	mutex_unlock(&chip->jeita_configure_lock);

	if ((state == SMB_TM_HIGHER_STATE) || (state == SMB_TM_LOWER_STATE)) {
		pr_info("temp=%d threshold=(%d-%d) health=(%d-%d-%d-%d)\n",
				temp, chip->low_temp_threshold, chip->high_temp_threshold,
				chip->batt_cold, chip->batt_cool,
				chip->batt_warm, chip->batt_hot);
	}

	if (chip->tm_state == SMB_TEMP_NORMAL_STATE)
		period = TEMP_DETECT_WORK_DELAY_30S;
	else
		period = TEMP_DETECT_WORK_DELAY_2S;
	schedule_delayed_work(&chip->temp_control_work, round_jiffies_relative(msecs_to_jiffies(period)));
}

static enum alarmtimer_restart update_temp_alarm_cb(struct alarm *alarm,
							ktime_t now)
{
	struct smb23x_chip *chip = container_of(alarm, struct smb23x_chip,
											update_temp_alarm);

	pr_info("alarm fired, usb_present=%d\n", chip->usb_present);

	return ALARMTIMER_NORESTART;

}

static int smb23x_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int rc;
	struct power_supply *usb_psy;
	struct smb23x_chip *chip;

	usb_psy = power_supply_get_by_name("usb");
	if (!usb_psy) {
		dev_dbg(&client->dev, "USB supply not found; defer probe\n");
		return (-EPROBE_DEFER);
	}

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (chip == NULL)
		return (-ENOMEM);

	chip->client = client;
	chip->dev = &client->dev;
	chip->usb_psy = usb_psy;
	chip->fake_battery_soc = -EINVAL;
	chip->board_ver_id = socinfo_get_board_ver_id();
	chip->vadc_dev = qpnp_get_vadc(chip->dev, "smb23x");
	if (IS_ERR(chip->vadc_dev)) {
		rc = PTR_ERR(chip->vadc_dev);
		if (rc != -EPROBE_DEFER)
			pr_err("%s ,vadc property missing\n", __func__);
		else
			pr_err("%s ,vadc property fail\n", __func__);
		chip->vadc_dev = NULL;
		return rc;
	}
	i2c_set_clientdata(client, chip);

	mutex_init(&chip->read_write_lock);
	mutex_init(&chip->irq_complete);
	mutex_init(&chip->chg_disable_lock);
	mutex_init(&chip->usb_suspend_lock);
	mutex_init(&chip->icl_set_lock);
	mutex_init(&chip->jeita_configure_lock);
	smb23x_wakeup_src_init(chip);
	wake_lock_init(&chip->charger_wake_lock, WAKE_LOCK_SUSPEND, "zte_chg_event");
	wake_lock_init(&chip->charger_valid_lock, WAKE_LOCK_SUSPEND, "zte_chg_valid");
	INIT_DELAYED_WORK(&chip->irq_polling_work, smb23x_irq_polling_work_fn);
	INIT_DELAYED_WORK(&chip->temp_control_work, smb23x_temp_control_func);

	alarm_init(&chip->update_temp_alarm, ALARM_REALTIME,
			update_temp_alarm_cb);

	/* enable the USB_SUSPEND always */
	chip->workaround_flags |= WRKRND_USB_SUSPEND;

	rc = smb23x_parse_dt(chip);
	if (rc < 0) {
		pr_err("Parse DT nodes failed!\n");
		goto destroy_mutex;
	}

	/*
	 * Enable register based battery charging as the hw_init moves CHG_EN
	 * control from pin-based to register based.
	 */
	rc = smb23x_charging_disable(chip, USER, false);
	if (rc < 0) {
		pr_err("Register control based charging enable failed\n");
		goto destroy_mutex;
	}

	rc = smb23x_hw_init(chip);
	if (rc < 0) {
		pr_err("Initialize hardware failed!\n");
		goto destroy_mutex;
	}

	smb23x_irq_polling_wa_check(chip);

	/*
	 * Disable charging if device tree (USER) requested:
	 * set USB_SUSPEND to cutoff USB power completely
	 */
	rc = smb23x_suspend_usb(chip, USER,
		chip->cfg_charging_disabled ? true : false);
	if (rc < 0) {
		pr_err("%suspend USB failed\n",
			chip->cfg_charging_disabled ? "S" : "Un-s");
		goto destroy_mutex;
	}

	rc = smb23x_determine_initial_status(chip);
	if (rc < 0) {
		pr_err("Update initial status failed\n");
		goto destroy_mutex;
	}

	/* register battery power_supply */
	chip->batt_psy.name		= "battery";
	chip->batt_psy.type		= POWER_SUPPLY_TYPE_BATTERY;
	chip->batt_psy.get_property	= smb23x_battery_get_property;
	chip->batt_psy.set_property	= smb23x_battery_set_property;
	chip->batt_psy.properties	= smb23x_battery_properties;
	chip->batt_psy.num_properties	= ARRAY_SIZE(smb23x_battery_properties);
	chip->batt_psy.external_power_changed = smb23x_external_power_changed;
	chip->batt_psy.property_is_writeable = smb23x_battery_is_writeable;

	smb23x_get_bms_psy(chip);

	if (chip->cfg_bms_controlled_charging) {
		chip->batt_psy.supplied_to	= batt_supplied_to;
		chip->batt_psy.num_supplicants	=
					ARRAY_SIZE(batt_supplied_to);
	}

	rc = power_supply_register(chip->dev, &chip->batt_psy);
	if (rc < 0) {
		pr_err("Register power_supply failed, rc = %d\n", rc);
		goto destroy_mutex;
	}
	chip->resume_completed = true;
	/* Register IRQ */
	if (client->irq) {
		rc = devm_request_threaded_irq(&client->dev, client->irq, NULL,
				smb23x_stat_handler, IRQF_ONESHOT,
				"smb23x_stat_irq", chip);
		if (rc < 0) {
			pr_err("Request IRQ(%d) failed, rc = %d\n",
				client->irq, rc);
			goto unregister_batt_psy;
		}
		enable_irq_wake(client->irq);
	}

	create_debugfs_entries(chip);
	schedule_delayed_work(&chip->temp_control_work, 0);

	pr_info("SMB23x successfully probed batt=%d usb = %d,board_id = %d\n",
			smb23x_get_prop_batt_present(chip), chip->usb_present, chip->board_ver_id);

	return 0;
unregister_batt_psy:
	power_supply_unregister(&chip->batt_psy);
destroy_mutex:
	wakeup_source_trash(&chip->smb23x_ws.source);
	mutex_destroy(&chip->read_write_lock);
	mutex_destroy(&chip->irq_complete);
	mutex_destroy(&chip->chg_disable_lock);
	mutex_destroy(&chip->usb_suspend_lock);
	mutex_destroy(&chip->icl_set_lock);

	return rc;
}

static int smb23x_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct smb23x_chip *chip = i2c_get_clientdata(client);
	int rc;

	/* Save the current IRQ config */
	rc = smb23x_read(chip, IRQ_CFG_REG_9, &chip->irq_cfg_mask);
	if (rc)
		pr_err("Save irq config failed, rc=%d\n", rc);

	/* enable only important IRQs */
	rc = smb23x_write(chip, IRQ_CFG_REG_9,
			BATT_MISSING_IRQ_EN_BIT | INOK_IRQ_EN_BIT);
	if (rc < 0)
		pr_err("Set irq_cfg failed, rc = %d\n", rc);

	mutex_lock(&chip->irq_complete);
	chip->resume_completed = false;
	mutex_unlock(&chip->irq_complete);
	cancel_delayed_work_sync(&chip->temp_control_work);

	if (chip->usb_present) {
		alarm_start_relative(&chip->update_temp_alarm,
				ns_to_ktime(BMS_NORMAL_UPDATE_TEMP_INTERVAL_NS));
		pr_info("start alarm to %llds with charger\n",
				BMS_NORMAL_UPDATE_TEMP_INTERVAL_NS/1000000000);
	} else {
		alarm_start_relative(&chip->update_temp_alarm,
				ns_to_ktime(BMS_SLOW_UPDATE_TEMP_INTERVAL_NS));
		pr_info("start alarm to %llds with no charger\n",
				BMS_SLOW_UPDATE_TEMP_INTERVAL_NS/1000000000);
	}

	return 0;
}

static int smb23x_suspend_noirq(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct smb23x_chip *chip = i2c_get_clientdata(client);

	if (chip->irq_waiting) {
		pr_err_ratelimited("Aborting suspend, an interrupt was detected while suspending\n");
		return (-EBUSY);
	}

	return 0;
}

static int smb23x_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct smb23x_chip *chip = i2c_get_clientdata(client);
	int rc;

	rc = smb23x_write(chip, IRQ_CFG_REG_9, chip->irq_cfg_mask);
	if (rc)
		pr_err("Restore irq cfg reg failed, rc=%d\n", rc);

	mutex_lock(&chip->irq_complete);
	chip->resume_completed = true;
	if (chip->irq_waiting) {
		mutex_unlock(&chip->irq_complete);
		smb23x_stat_handler(client->irq, chip);
		enable_irq(client->irq);
	} else {
		mutex_unlock(&chip->irq_complete);
	}
	schedule_delayed_work(&chip->temp_control_work, 0);
	alarm_cancel(&chip->update_temp_alarm);
	pr_info("cancel alarm\n");

	return 0;
}

static int smb23x_remove(struct i2c_client *client)
{
	struct smb23x_chip *chip = i2c_get_clientdata(client);
	cancel_delayed_work_sync(&chip->temp_control_work);

	power_supply_unregister(&chip->batt_psy);
	wakeup_source_trash(&chip->smb23x_ws.source);
	mutex_destroy(&chip->read_write_lock);
	mutex_destroy(&chip->irq_complete);
	mutex_destroy(&chip->chg_disable_lock);
	mutex_destroy(&chip->usb_suspend_lock);
	mutex_destroy(&chip->icl_set_lock);
	mutex_destroy(&chip->jeita_configure_lock);

	return 0;
}

static const struct dev_pm_ops smb23x_pm_ops = {
	.resume		= smb23x_resume,
	.suspend_noirq	= smb23x_suspend_noirq,
	.suspend	= smb23x_suspend,
};

static struct of_device_id smb23x_match_table[] = {
	{ .compatible = "qcom,smb231-lbc",},
	{ .compatible = "qcom,smb232-lbc",},
	{ .compatible = "qcom,smb233-lbc",},
	{ },
};

static const struct i2c_device_id smb23x_id[] = {
	{"smb23x-lbc", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, smb23x_id);

static struct i2c_driver smb23x_driver = {
	.driver		= {
		.name		= "smb23x-lbc",
		.owner		= THIS_MODULE,
		.of_match_table	= smb23x_match_table,
		.pm		= &smb23x_pm_ops,
	},
	.probe		= smb23x_probe,
	.remove		= smb23x_remove,
	.id_table	= smb23x_id,
};

module_i2c_driver(smb23x_driver);

MODULE_DESCRIPTION("SMB23x Linear Battery Charger");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("i2c:smb23x-lbc");
