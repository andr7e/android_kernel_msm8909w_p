/*
 *  stc3117_battery.c
 *  STC3117 fuel-gauge systems for lithium-ion (Li+) batteries
 *
 *  Copyright (C) 2011 STMicroelectronics.
 *  Copyright (c) 2016 The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#define pr_fmt(fmt) "STC311x: %s: " fmt, __func__

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/power_supply.h>
#include <linux/power/stc3117_battery.h>
#include <linux/slab.h>
#include <linux/qpnp/qpnp-adc.h> 
#include <linux/wakelock.h>

#define GG_VERSION "1.00a"

/*Function declaration*/

/* ************************************************************************ */
/*        STC311x DEVICE SELECTION                                          */
/* STC3117 version only                                                     */
/* ------------------------------------------------------------------------ */
#define STC3117

#define BATD_UC8
/* ************************************************************************ */




/* Private define ----------------------------------------------------------*/


/* ************************************************************************ */
/*        SPECIAL FUNCTIONS                                                 */
/* ------------------------------------------------------------------------ */
/*                                                                          */
/* define TEMPCOMP_SOC to enable SOC temperature compensation */
#define TEMPCOMP_SOC

/* ************************************************************************ */


/* ************************************************************************ */
/*        INTERNAL PARAMETERS                                               */
/*   TO BE ADJUSTED ACCORDING TO BATTERY/APPLICATION CHARACTERISTICS        */
/* ------------------------------------------------------------------------ */
/*                                                                          */
/* min voltage at the end of the charge (mV)      */
#define BATT_CHG_VOLTAGE   4390 //4250
/* nearly empty battery detection level (mV)      */
#define BATT_MIN_VOLTAGE   3500
#define MAX_HRSOC          51200  /* 100% in 1/512% units*/
#define MAX_SOC            1000   /* 100% in 0.1% units */

#define CHG_MIN_CURRENT     200   /* min charge current in mA*/
#define CHG_END_CURRENT      40 //20   /* end charge current in mA*/
/* minimum application current consumption in mA ( <0 !) */
#define APP_MIN_CURRENT     (-5)
#define APP_MIN_VOLTAGE	    3400  /* application cut-off voltage*/
#define APP_MIN_THRESHOLD	100
#define TEMP_MIN_ADJ	    (-5) /* minimum temperature for gain adjustment */

/* normalized VM_CNF at 60, 40, 25, 10, 0, -10°C, -20°C */
#define VMTEMPTABLE        { 85, 90, 100, 160, 320, 440, 840 }

#define AVGFILTER           4  /* average filter constant */

/* ************************************************************************ */



/* Private define ---------------------------------------------------------*/

#define STC31xx_SLAVE_ADDRESS            0xE0 /* STC31xx 8-bit address byte */

/*Address of the STC311x register ------------------------------------------*/
#define STC311x_REG_MODE                 0x00 /* Mode Register             */
#define STC311x_REG_CTRL                 0x01 /* Control and Status Register*/
#define GG_VM_BIT						BIT(2)
#define GG_RUN_BIT						BIT(4)
#define PORDET_BIT						BIT(4)
#define STC311x_REG_SOC                  0x02 /* SOC Data (2 bytes) */
/* Number of Conversion (2 bytes) */
#define STC311x_REG_COUNTER              0x04
/* Battery Current (2 bytes) */
#define STC311x_REG_CURRENT              0x06
/* Battery Voltage (2 bytes) */
#define STC311x_REG_VOLTAGE              0x08
/* Temperature               */
#define STC311x_REG_TEMPERATURE          0x0A

#ifdef STC3117
/* Battery Average Current (2 bytes)   */
#define STC311x_REG_AVG_CURRENT          0x0B
#endif
#define STC311x_REG_OCV                  0x0D    /* Battery OCV (2 bytes) */
/* CC configuration (2 bytes)    */
#define STC311x_REG_CC_CNF               0x0F
/* VM configuration (2 bytes)    */
#define STC311x_REG_VM_CNF               0x11
#define STC311x_REG_ALARM_SOC            0x13    /* SOC alarm level         */
#define STC311x_REG_ALARM_VOLTAGE        0x14    /* Low voltage alarm level */
/* Current threshold for relaxation */
#define STC311x_REG_CURRENT_THRES        0x15
/* Current monitoring counter   */
#define STC311x_REG_CMONIT_COUNT         0x16
/* Current monitoring max count */
#define STC311x_REG_CMONIT_MAX           0x17

#define STC311x_REG_CC_ADJ               0x1B    /* CC adjustement (2 bytes)*/
#define STC311x_REG_VM_ADJ               0x1D    /* VM adjustement (2 bytes)*/

/*Bit mask definition*/
#define STC311x_VMODE			 0x01	 /* Voltage mode bit mask */
#define STC311x_ALM_ENA			 0x08	 /* Alarm enable bit mask */
#define STC311x_GG_RUN			 0x10	 /* Alarm enable bit mask */
#define STC311x_FORCE_CC		 0x20	 /* Force CC bit mask     */
#define STC311x_FORCE_VM		 0x40	 /* Force VM bit mask     */
#define STC311x_SOFTPOR			 0x11	 /* soft reset     */

/* Enable internal Pull-Up on BATD bit mask */
#define STC311x_BATD_PU                  0x02
#define STC311x_FORCE_CD                 0x04  /* Force CD high bit mask */

#define STC311x_REG_ID                   0x18    /* Chip ID (1 byte)       */
#define STC3117_ID                       0x16    /* STC3117 ID */

/* General Purpose RAM Registers */
#define STC311x_REG_RAM                  0x20
/* Total RAM size of STC311x in bytes */
#define RAM_SIZE                         16

#define STC311x_REG_OCVTAB               0x30
#define STC311x_REG_SOCTAB               0x50

/* counter value for 1st current/temp measurements */
#define VCOUNT				 0

/* GG_RUN & PORDET mask in STC311x_BattDataTypeDef status word */
#define M_STAT 0x1010
#define M_RST  0x9800       /* UVLOD & BATFAIL & PORDET mask */
/* GG_RUN mask in STC311x_BattDataTypeDef status word */
#define M_RUN  0x0010
#define M_GGVM 0x0400       /* GG_VM mask */
#define M_BATFAIL 0x0800    /* BATFAIL mask*/
#define M_UVLOD   0x8000    /* UVLOD mask (STC3117 only) */

#define M_VMOD 0x0001       /* VMODE mask */

#define OK 0

/* Battery charge state definition for BattState */
#define  BATT_CHARGING  3
#define  BATT_ENDCHARG  2
#define  BATT_FULCHARG  1
#define  BATT_IDLE      0
#define  BATT_DISCHARG (-1)
#define  BATT_LOWBATT  (-2)

/* STC311x RAM test word */
#define RAM_TSTWORD 0x53A9

/* Gas gauge states */
#define GG_INIT     'I'
#define GG_RUNNING  'R'
#define GG_POWERDN  'D'

#define VM_MODE 1
#define CC_MODE 0

#define SMB231_REG0_DEFAULT 			0x54
/* gas gauge structure definition ------------------------------------*/

/* Private constants -------------------------------------------------------*/

#define NTEMP 7
/* temperature table from 60°C to -20°C (descending order!) */
static const int TempTable[NTEMP] = {60, 40, 25, 10, 0, -10, -20};
static const int DefVMTempTable[NTEMP] = VMTEMPTABLE;
static const char *charger_name = "battery";
//static int g_low_battery_counter;
static bool g_debug = 1, g_standby_mode, g_boot_phase, g_force_SOC_update;
static int g_ui_soc, g_last_status, g_ocv, g_reg_soc, g_dummy_soc;
static const char * const charge_status[] = {
	"unknown",
	"charging",
	"discharging",
	"not charging",
	"full",
};
static int STC311x_Status(void);
static int STC31xx_Read(int length, int reg , unsigned char *values);
static int STC31xx_ReadByte(int RegAddress);
static int STC31xx_ReadWord(int RegAddress);
static int conv(short value, unsigned short factor);

/* Private variables -------------------------------------------------------*/

/* structure of the STC311x battery monitoring parameters */
struct GasGauge_DataTypeDef {
	int Voltage;        /* battery voltage in mV */
	int Current;        /* battery current in mA */
	int Temperature;    /* battery temperature in 0.1°C */
	int SOC;            /* battery relative SOC (%) in 0.1% */
	int OCV;
	int AvgSOC;
	int AvgCurrent;
	int AvgVoltage;
	int AvgTemperature;
	int ChargeValue;    /* remaining capacity in mAh */
	/* battery remaining operating time during discharge (min) */
	int RemTime;
	int State;          /* charge (>0)/discharge(<0) state */
	int CalStat;        /* Internal status */
	/* -- parameters -- */
	int Vmode;       /* 1=Voltage mode, 0=mixed mode */
	int Alm_SOC;     /* SOC alm level */
	int Alm_Vbat;    /* Vbat alm level */
	int CC_cnf;      /* nominal CC_cnf */
	int VM_cnf;      /* nominal VM cnf */
	int Cnom;        /* nominal capacity in mAh */
	int Rsense;      /* sense resistor */
	int Rint;         /* battery internal resistance */
	int RelaxCurrent; /* current for relaxation (< C/20) */
	int Adaptive;     /* adaptive mode */
	/* capacity derating in 0.1%, for temp = 60, 40, 25, 10,   0, -10, -20
	 * °C */
	int CapDerating[7];
	int OCVValue[OCVTAB_SIZE];    /* OCV curve values */
	int SOCValue[SOCTAB_SIZE];    /* SOC curve values */
	int ExternalTemperature;
	int ForceExternalTemperature;
	int Ropt;
	int Var1;
};

/* structure of the STC311x battery monitoring data */
struct STC311x_BattDataTypeDef {
	/* STC311x data */
	int STC_Status;  /* status word  */
	int Vmode;       /* 1=Voltage mode, 0=mixed mode */
	int Voltage;     /* voltage in mV            */
	int Current;     /* current in mA            */
	int Temperature; /* temperature in 0.1°C     */
	int HRSOC;       /* uncompensated SOC in 1/512%   */
	int OCV;         /* OCV in mV*/
	int ConvCounter; /* convertion counter       */
	int RelaxTimer;  /* current relax timer value */
	int CC_adj;      /* CC adj */
	int VM_adj;      /* VM adj */
	/* results & internals */
	int SOC;         /* compensated SOC in 0.1% */
	int AvgSOC;      /* in 0.1% */
	int AvgVoltage;
	int AvgCurrent;
	int AvgTemperature;
	int AccSOC;
	int AccVoltage;
	int AccCurrent;
	int AccTemperature;
	int BattState;
	int GG_Mode;     /* 1=VM active, 0=CC active */
	int LastTemperature;
	int BattOnline;	/* BATD*/
	int IDCode;
	/* parameters */
	int Alm_SOC;     /* SOC alm level in % */
	int Alm_Vbat;    /* Vbat alm level in mV */
	int CC_cnf;      /* nominal CC_cnf */
	int VM_cnf;      /* nominal VM cnf */
	int Cnom;        /* nominal capacity is mAh */
	int Rsense;      /* sense resistor in milliOhms */
	int Rint;        /* internal resistance in milliOhms */
	int CurrentFactor;
	int CRateFactor;
	int RelaxThreshold;   /* current threshold for VM (mA)  */
	int VM_TempTable[NTEMP];
	int CapacityDerating[NTEMP];
	int  OCVValue[OCVTAB_SIZE];
	unsigned char SOCValue[OCVTAB_SIZE];
	int  Ropt;
	int  Nropt;
};

static struct STC311x_BattDataTypeDef BattData;   /* STC311x data */

/* structure of the STC311x RAM registers for the Gas Gauge algorithm data */
static union {
	unsigned char db[RAM_SIZE];  /* last byte holds the CRC */
	struct {
		short int TstWord;     /* 0-1 */
		unsigned short int HRSOC;       /* 2-3 SOC backup */
		short int CC_cnf;      /* 4-5 current CC_cnf */
		short int VM_cnf;      /* 6-7 current VM_cnf */
		char SOC;              /* 8 SOC for trace (in %) */
		char GG_Status;        /* 9  */
		/* bytes ..RAM_SIZE-2 are free, last byte RAM_SIZE-1 is the
		 * CRC*/
	} reg;
} GG_Ram;


int Capacity_Adjust;


/* ------------------------------------------------------------------------ */
/*        INTERNAL ANDROID DRIVER PARAMETERS                                */
/*   TO BE ADJUSTED ACCORDING TO BATTERY/APPLICATION CHARACTERISTICS        */
/* ------------------------------------------------------------------------ */

#define STC311x_BATTERY_FULL 100
#define STC311x_DELAY_BOOTUP	 12000 //120 sec
#define STC311x_DELAY	 3000 //30 sec
#define STC311x_DELAY_LOW_BATT 2000 //20 sec
#define STC311x_DELAY_CRITICAL_BATT 500 //5 sec
#define STC311x_SOC_LOW_THRESHOLD 7
#define STC311x_SOC_CRITICAL_THRESHOLD 3
#define BATTERY_NTC_ERROR_TEMP -400 //-40degC

/* ************************************************************************ */

static struct i2c_client *sav_client;

struct stc311x_chip {
	struct i2c_client		*client;
	struct delayed_work		work;
	struct delayed_work		boot_up_work;	
	struct power_supply		battery;
	struct stc311x_platform_data	*pdata;
	struct wake_lock wlock;

	/* State Of Connect */
	int online;
	/* battery SOC (capacity) */
	int batt_soc;
	/* battery voltage */
	int batt_voltage;
	/* Current */
	int batt_current;
	/* State Of Charge */
	int status;

	int Temperature;
	struct device     *dev;
	struct qpnp_vadc_chip	*vadc_dev;    
};

int null_fn(void)
{
	/*for discharging status*/
	return 0;
}

int Temperature_fn(void)
{
	struct stc311x_chip *chip = i2c_get_clientdata(sav_client);
	struct qpnp_vadc_result result;
	int res;

	//Use pm8916 mpp3 to read battery NTC to get the battery temperature
	if (NULL == chip->vadc_dev) {
		chip->vadc_dev = qpnp_get_vadc(chip->dev, "pm8916");
		if (IS_ERR(chip->vadc_dev)) {
			res = PTR_ERR(chip->vadc_dev);
			if (res == -EPROBE_DEFER)
				pr_err("stc311x - pm8916 vadc not found - defer rc \n");
			else
				pr_err("stc311x - fail to get the pm8916 vadc \n");
			chip->vadc_dev = NULL;
			goto Error;
		}
	}

	res=qpnp_vadc_read(chip->vadc_dev, P_MUX3_1_1, &result);//get channel 0x12	
	if (res < 0) {
		pr_err("stc311x - Error reading pm8916 mpp3: %d\n", res);
		goto Error; 
	} else {
		pr_debug("stc311x - pm8916 mpp3,  temperature = %lld \n", result.physical);
		return (int)(result.physical * 10);
	}

	Error:
		return BATTERY_NTC_ERROR_TEMP;
}

static struct stc311x_platform_data stc3117_data = {
	.battery_online = NULL,
	/* used in stc311x_get_status()*/
	.charger_online = NULL,
	/* used in stc311x_get_status()*/
	.charger_enable = NULL,
	.power_supply_register = NULL,
	.power_supply_unregister = NULL,

	.Vmode = 0,/*REG_MODE, BIT_VMODE 1=Voltage mode, 0=mixed mode */
	.Alm_SOC = 10,/* SOC alm level %*/
	.Alm_Vbat = 3600,/* Vbat alm level mV*/
	/* nominal CC_cnf, coming from battery characterisation*/
	.CC_cnf = 85,
	/* nominal VM cnf , coming from battery characterisation*/
	.VM_cnf = 197,
	/* nominal internal impedance*/
	.Rint = 457,
	/* nominal capacity in mAh, coming from battery characterisation*/
	.Cnom = 415,
	.Rsense = 10, /* sense resistor mOhms*/
	.RelaxCurrent = 20, /* current for relaxation in mA (< C/20) */
	.Adaptive = 1, /* 1=Adaptive mode enabled, 0=Adaptive mode disabled */

	/* Elentec Co Ltd Battery pack - 80 means 8% */
	/* capacity derating in 0.1%, for temp = -20°C */
	.CapDerating[6] = 212,
	/* capacity derating in 0.1%, for temp = -10°C */
	.CapDerating[5] = 71,
	/* capacity derating in 0.1%, for temp = 0°C */
	.CapDerating[4] = 35,
	/* capacity derating in 0.1%, for temp = 10°C */
	.CapDerating[3] = 35,
	/* capacity derating in 0.1%, for temp = 25°C */
	.CapDerating[2] = 0,
	/* capacity derating in 0.1%, for temp = 40°C */
	.CapDerating[1] = 0,
	/* capacity derating in 0.1%, for temp = 60°C */
	.CapDerating[0] = 0,

	/*OCV curve example for a 4.4V li-ion battery*/
	.OCVValue[15] = 4374,             /* OCV curve value */
	.OCVValue[14] = 4257,             /* OCV curve value */
	.OCVValue[13] = 4142,             /* OCV curve value */
	.OCVValue[12] = 4043,             /* OCV curve value */
	.OCVValue[11] = 3985,             /* OCV curve value */
	.OCVValue[10] = 3937,             /* OCV curve value */
	.OCVValue[9] = 3855,              /* OCV curve value */
	.OCVValue[8] = 3811,              /* OCV curve value */
	.OCVValue[7] = 3776,              /* OCV curve value */
	.OCVValue[6] = 3755,              /* OCV curve value */
	.OCVValue[5] = 3737,              /* OCV curve value */
	.OCVValue[4] = 3710,              /* OCV curve value */
	.OCVValue[3] = 3686,              /* OCV curve value */
	.OCVValue[2] = 3680,              /* OCV curve value */
	.OCVValue[1] = 3573,              /* OCV curve value */
	.OCVValue[0] = 3300,              /* OCV curve value */

	/* SOC_TAB data */
	.SOCValue[15] = 100,
	.SOCValue[14] = 90,
	.SOCValue[13] = 80,
	.SOCValue[12] = 70,
	.SOCValue[11] = 65,
	.SOCValue[10] = 60,
	.SOCValue[9] = 50,
	.SOCValue[8] = 40,
	.SOCValue[7] = 30,
	.SOCValue[6] = 25,
	.SOCValue[5] = 20,
	.SOCValue[4] = 15,
	.SOCValue[3] = 10,
	.SOCValue[2] = 6,
	.SOCValue[1] = 3,
	.SOCValue[0] = 0,

	/*if the application temperature data is preferred than the STC3117
	 * temperature*/
	/*External temperature fonction, return degC*/
	.ExternalTemperature = Temperature_fn,
	/* 1=External temperature, 0=STC3117 temperature */
	.ForceExternalTemperature = 1,
};

static int stc311x_set_property(struct power_supply *psy,
				  enum power_supply_property psp,
				  const union power_supply_propval *val)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		if(val->intval)
			g_debug = 1;
		else
			g_debug = 0;
		break;
	case POWER_SUPPLY_PROP_CAPACITY_RAW:
		if ((val->intval >= 0) && (val->intval <= 100))
			g_dummy_soc = val->intval;
		else
			g_dummy_soc = 101;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int stc311x_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct stc311x_chip *chip = container_of(psy,
						 struct stc311x_chip, battery);

	/* from power_supply.h:
	 * All voltages, currents, charges, energies, time and
	 * temperatures in uV,
	 * ÂµA, ÂµAh, ÂµWh, seconds and tenths of degree Celsius
	 * unless otherwise
	 * stated. It's driver's job to convert its raw values to
	 * units in which
	 * this class operates.
	 */

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = chip->batt_voltage * 1000;  /* in uV */
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = chip->batt_current * 1000;  /* in uA */
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		if (g_dummy_soc == 101)
			val->intval = g_ui_soc;
		else
			val->intval = g_dummy_soc;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = chip->Temperature;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = g_debug;
		break;
	case POWER_SUPPLY_PROP_CAPACITY_RAW:
		val->intval = g_dummy_soc;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int stc311x_battery_is_writeable(struct power_supply *psy,
					enum power_supply_property prop)
{
	switch (prop) {
	case POWER_SUPPLY_PROP_TECHNOLOGY:
	case POWER_SUPPLY_PROP_CAPACITY_RAW:
		return 1;
	default:
		break;
	}
	return 0;
}


static void stc311x_get_version(struct i2c_client *client)
{
	dev_info(&client->dev, "STC3117 Fuel-Gauge Ver %s\n", GG_VERSION);
}

/* -------------------------------------------------------------------------- */
/* I2C interface */

/* -----------------------------------------------------------------
   The following routines interface with the I2C primitives
   I2C_Read(u8_I2C_address, u8_NumberOfBytes, u8_RegAddress, pu8_RxBuffer);
   I2C_Write(u8_I2C_address, u8_NumberOfBytes, u8_RegAddress, pu8_TxBuffer);
note: here I2C_Address is the 8-bit address byte
----------------------------------------------------------------- */

#define NBRETRY 5


/****************************************************************************
 * Function Name  : STC31xx_Write
 * Description    : utility function to write several bytes to
 * STC311x registers
 * Input          : NumberOfBytes, RegAddress, TxBuffer
 * Return         : error status
 * Note: Recommended implementation is to used I2C block write.
 * If not available,
 * STC311x registers can be written by 2-byte words (unless NumberOfBytes=1)
 * or byte per byte.
 ****************************************************************************/
static int STC31xx_Write(int length, int reg , unsigned char *values)
{
	int ret;

	ret = i2c_smbus_write_i2c_block_data(sav_client, reg, length, values);
	if (ret < 0)
		dev_err(&sav_client->dev, "%s: err %d\n", __func__, ret);

	return ret;
}

/****************************************************************************
 * Function Name  : STC31xx_Read
 * Description    : utility function to read several bytes from
 *                  STC311x registers
 * Input          : NumberOfBytes, RegAddress, , RxBuffer
 * Return         : error status
 * Note: Recommended implementation is to used I2C block read.
 *       If not available,
 * STC311x registers can be read by 2-byte words (unless NumberOfBytes=1)
 * Using byte per byte read is not recommended since it doesn't
 * ensure register data integrity
 ****************************************************************************/
static int STC31xx_Read(int length, int reg , unsigned char *values)
{
	int ret;

	ret = i2c_smbus_read_i2c_block_data(sav_client, reg, length, values);
	if (ret < 0)
		dev_err(&sav_client->dev, "%s: err %d\n", __func__, ret);

	return ret;
}



/* ---- end of I2C primitive interface ------------------------------------ */


/****************************************************************************
 * Function Name  : STC31xx_ReadByte
 * Description    : utility function to read the value stored in one register
 * Input          : RegAddress: STC311x register,
 * Return         : 8-bit value, or 0 if error
 ****************************************************************************/
static int STC31xx_ReadByte(int RegAddress)
{
	int value;
	unsigned char data[2];
	int res;

	res = STC31xx_Read(1, RegAddress, data);

	if (res >= 0) {
		/* no error */
		value = data[0];
	} else
		value = 0;

	return value;
}



/****************************************************************************
 * Function Name  : STC31xx_WriteByte
 * Description    : utility function to write a 8-bit value into a register
 * Input          : RegAddress: STC311x register, Value: 8-bit value to write
 * Return         : error status (OK, !OK)
 ****************************************************************************/
static int STC31xx_WriteByte(int RegAddress, unsigned char Value)
{
	int res;
	unsigned char data[2];

	data[0] = Value;
	res = STC31xx_Write(1, RegAddress, data);

	return res;
}


/****************************************************************************
 * Function Name  : STC31xx_ReadWord
 * Description    : utility function to read the value stored in
 *                  one register pair
 * Input          : RegAddress: STC311x register,
 * Return         : 16-bit value, or 0 if error
 ****************************************************************************/
static int STC31xx_ReadWord(int RegAddress)
{
	int value;
	unsigned char data[2];
	int res;

	res = STC31xx_Read(2, RegAddress, data);

	if (res >= 0) {
		/* no error */
		value = data[1];
		value = (value << 8) + data[0];
	} else
		value = 0;

	return value;
}


/****************************************************************************
 * Function Name  : STC31xx_WriteWord
 * Description    : utility function to write a 16-bit value into
 *                  a register pair
 * Input          : RegAddress: STC311x register, Value: 16-bit value to write
 * Return         : error status (OK, !OK)
 ****************************************************************************/
static int STC31xx_WriteWord(int RegAddress, int Value)
{
	int res;
	unsigned char data[2];

	data[0] = Value & 0xff;
	data[1] = (Value>>8) & 0xff;
	res = STC31xx_Write(2, RegAddress, data);

	return res;
}



/* ---- end of I2C R/W interface ------------------------------------------ */


/* ------------------------------------------------------------------------ */

/* #define CurrentFactor  (24084/SENSERESISTOR)         LSB=5.88uV/R=
 * ~24084/R/4096 - convert to mA*/
#define VoltageFactor  9011  /* LSB=2.20mV ~9011/4096 - convert to mV*/



/*****************************************************************************
 * Function Name  : STC311x_Status
 * Description    :  Read the STC311x status
 * Input          : None
 * Return         : status word (REG_MODE / REG_CTRL), -1 if error
 *****************************************************************************/
static int STC311x_Status(void)
{
	int value;
	int ret;

	/* first, check the presence of the STC311x by reading first byte of
	 * dev. ID*/
	BattData.IDCode = STC31xx_ReadByte(STC311x_REG_ID);
	if (BattData.IDCode != STC3117_ID) {
		ret = -1;
		return ret;
	}

	/* read REG_MODE and REG_CTRL */
	value = STC31xx_ReadWord(STC311x_REG_MODE);

	return value;
}


/*****************************************************************************
 * Function Name  : STC311x_SetParam
 * Description    :  initialize the STC311x parameters
 * Input          : rst: init algo param
 * Return         : 0
 *****************************************************************************/
static void STC311x_SetParam(void)
{
	int value;
	int ii;

	/*   set GG_RUN=0 before changing algo parameters */
	STC31xx_WriteByte(STC311x_REG_MODE, 0x01);

	/* init OCV curve */
	for (ii = 0; ii < OCVTAB_SIZE; ii++) {
		if (BattData.OCVValue[ii] != 0)
			STC31xx_WriteWord(STC311x_REG_OCVTAB+ii*2,
					  BattData.OCVValue[ii]*100/55);
	}

	/* init SOC Table */
	for (ii = 0; ii < SOCTAB_SIZE; ii++)
			STC31xx_WriteByte(STC311x_REG_SOCTAB+ii,
					  (BattData.SOCValue[ii]*2));

	/* set alm level if different from default */
	if (BattData.Alm_SOC != 0)
		STC31xx_WriteByte(STC311x_REG_ALARM_SOC, BattData.Alm_SOC*2);

	if (BattData.Alm_Vbat != 0) {
		/* LSB=8*2.44mV */
		value = ((BattData.Alm_Vbat << 9) / VoltageFactor);
		STC31xx_WriteByte(STC311x_REG_ALARM_VOLTAGE, value);
	}

	/* relaxation timer */
	if (BattData.RelaxThreshold != 0) {
		value = ((BattData.RelaxThreshold << 9) /
			 BattData.CurrentFactor);   /* LSB=8*5.88uV/Rsense */
		value = value & 0x7f;
		STC31xx_WriteByte(STC311x_REG_CURRENT_THRES, value);
	}

	/* CC_CNF, VM_CNF */
	STC31xx_WriteWord(STC311x_REG_CC_CNF, BattData.CC_cnf);
	STC31xx_WriteWord(STC311x_REG_VM_CNF, BattData.VM_cnf);

	/*   clear PORDET, BATFAIL, free ALM pin, reset conv counter */
	STC31xx_WriteByte(STC311x_REG_CTRL, 0x83);

	if (BattData.Vmode) {
		/*   set GG_RUN=1, voltage mode, alm disabled */
		STC31xx_WriteByte(STC311x_REG_MODE, 0x11);
	} else {
		/*   set GG_RUN=1, mixed mode, alm disabled */
		STC31xx_WriteByte(STC311x_REG_MODE, 0x10);
	}
}




/*****************************************************************************
 * Function Name  : STC311x_Startup
 * Description    :  initialize and start the STC311x at application startup
 * Input          : None
 * Return         : 0 if ok, -1 if error
 *****************************************************************************/
static int STC311x_Startup(void)
{
	int res;
	int ocv, curr;

	/* check STC310x status */
	res = STC311x_Status();
	if (res < 0)
		return res;

	/* read OCV */
	ocv = STC31xx_ReadWord(STC311x_REG_OCV);

	STC311x_SetParam();  /* set parameters  */

	/* with STC3117, it is possible here to read the current and
	 * compensate OCV:*/
	curr = STC31xx_ReadWord(STC311x_REG_CURRENT);
	curr &= 0x3fff;   /* mask unused bits */
	if (curr >= 0x2000)
		curr -= 0x4000;  /* convert to signed value */
	if (BattData.Rsense != 0) {
		/*avoid divide by 0*/
		ocv = ocv - BattData.Rint * curr * 588 /
			BattData.Rsense / 55000;
	} else
		return (-1);

	/* rewrite ocv to start SOC with updated OCV curve */
	STC31xx_WriteWord(STC311x_REG_OCV, ocv);

	return 0;
}


/*****************************************************************************
 * Function Name  : STC311x_Restore
 * Description    :  Restore STC311x state
 * Input          : None
 * Return         :
 *****************************************************************************/
#define HRSOC_1_PERCENT (MAX_HRSOC/100)
static int STC311x_Restore(void)
{
	int res;
	int ocv;
	int ram_hrsoc, reg_soc;

	/* check STC310x status */
	res = STC311x_Status();
	if (res < 0)
		return res;

	/* read OCV */
	ocv = STC31xx_ReadWord(STC311x_REG_OCV);

	STC311x_SetParam();  /* set parameters  */

	reg_soc = (STC31xx_ReadWord(STC311x_REG_SOC)/HRSOC_1_PERCENT);
	ram_hrsoc = (GG_Ram.reg.HRSOC);
	pr_info("STC311x_Restore-reg_soc:%d, ram_HRSOC:%d \n", reg_soc, ram_hrsoc);
	pr_info("STC311x_Restore-GG_status: %c, GG_Ram.reg.SOC: %d \n", GG_Ram.reg.GG_Status,GG_Ram.reg.SOC);

	/* if restore from unexpected reset, restore SOC (system dependent) */
	if (GG_Ram.reg.GG_Status == GG_RUNNING) {
		if (GG_Ram.reg.SOC != 0) {
			/*   restore SOC */
			STC31xx_WriteWord(STC311x_REG_SOC, GG_Ram.reg.HRSOC);
		} else
			STC31xx_WriteWord(STC311x_REG_SOC, HRSOC_1_PERCENT);
	}

	/* rewrite ocv to start SOC with updated OCV curve*/
	/*STC31xx_WriteWord(STC311x_REG_OCV, ocv);*/

	return 0;
}




/*****************************************************************************
 * Function Name  : STC311x_Powerdown
 * Description    :  stop the STC311x at application power down
 * Input          : None
 * Return         : error status (OK, !OK)
 *****************************************************************************/
static int STC311x_Powerdown(void)
{
	int res;

	/* write 0x01 into the REG_CTRL to release IO0 pin open, */
	STC31xx_WriteByte(STC311x_REG_CTRL, 0x01);

	/* write 0 into the REG_MODE register to put the STC311x in standby
	 * mode*/
	res = STC31xx_WriteByte(STC311x_REG_MODE, 0);
	if (res != OK)
		return res;

	return OK;
}


/*****************************************************************************
 * Function Name  : STC311x_xxxx
 * Description    :  misc STC311x utility functions
 * Input          : None
 * Return         : None
 *****************************************************************************/
static void STC311x_Reset(void)
{
	/*   set soft POR */
	STC31xx_WriteByte(STC311x_REG_CTRL, STC311x_SOFTPOR);
}

static void STC311x_SetSOC(int SOC)
{
	STC31xx_WriteWord(STC311x_REG_SOC, SOC);
}

static void STC311x_ForceCC(void)
{
	int value;

	value = STC31xx_ReadByte(STC311x_REG_MODE);
	/*   force CC mode */
	STC31xx_WriteByte(STC311x_REG_MODE, value | STC311x_FORCE_CC);
}

static int STC311x_SaveVMCnf(void)
{
	int reg_mode;

	/* mode register*/
	reg_mode = BattData.STC_Status & 0xff;

	/*   set GG_RUN=0 before changing algo parameters */
	reg_mode &= ~STC311x_GG_RUN;

	STC31xx_WriteByte(STC311x_REG_MODE, reg_mode);

	STC31xx_ReadByte(STC311x_REG_ID);

	STC31xx_WriteWord(STC311x_REG_VM_CNF, GG_Ram.reg.VM_cnf);

	if (BattData.Vmode) {
		/*   set GG_RUN=1, voltage mode, alm disabled */
		STC31xx_WriteByte(STC311x_REG_MODE, 0x11);
	} else {
		/*   set GG_RUN=1, mixed mode, alm disabled */
		STC31xx_WriteByte(STC311x_REG_MODE, 0x10);
		if (BattData.GG_Mode == CC_MODE) {
			/*   force CC mode */
			STC31xx_WriteByte(STC311x_REG_MODE, 0x30);
		} else {
			/*   force VM mode */
			STC31xx_WriteByte(STC311x_REG_MODE, 0x50);
		}
	}

	return 0;
}

/*****************************************************************************
 * Function Name  : conv
 * Description    : conversion utility
 *  convert a raw 16-bit value from STC311x registers into user units
 (mA, mAh, mV, °C)
 *  (optimized routine for efficient operation on 8-bit processors
 such as STM8)
 * Input          : value, factor
 * Return         : result = value * factor / 4096
 *****************************************************************************/
static int conv(short value, unsigned short factor)
{
	int v;

	v = ((long) value * factor) >> 11;
	v = (v+1)/2;

	return v;
}

/*****************************************************************************
 * Function Name  : STC311x_ReadBatteryData
 * Description    :  utility function to read the battery data from STC311x
 *                  to be called every 5s or so
 * Input          : ref to BattData structure
 * Return         : error status (OK, !OK)
 *****************************************************************************/
static int STC311x_ReadBatteryData(struct STC311x_BattDataTypeDef *BattData)
{
	unsigned char data[16];
	int res;
	int value;

	res = STC311x_Status();
	/* return if I2C error or STC3117 not responding */
	if (res < 0)
		return res;

	/* STC311x status */
	BattData->STC_Status = res;
	if (BattData->STC_Status & M_GGVM)
		BattData->GG_Mode = VM_MODE;   /* VM active */
	else
		BattData->GG_Mode = CC_MODE;   /* CC active */

	/* read STC3117 registers 0 to 14 */
	res = STC31xx_Read(15, 0, data);

	if (res < 0)
		return res;  /* read failed */

	/* fill the battery status data */
	/* SOC */
	value = data[3];
	value = (value<<8) + data[2];

	BattData->HRSOC = value;     /* result in 1/512% */
	g_reg_soc = (int)(value/512);

	/* conversion counter */
	value = data[5];
	value = (value<<8) + data[4];

	BattData->ConvCounter = value;

	/* current */
	value = data[7];
	value = (value<<8) + data[6];

	value &= 0x3fff;   /* mask unused bits */
	if (value >= 0x2000)
		value -= 0x4000;  /* convert to signed value */
	/* result in mA */
	BattData->Current = conv(value, BattData->CurrentFactor);

	/* voltage */
	value = data[9];
	value = (value<<8) + data[8];

	value &= 0x0fff; /* mask unused bits */
	if (value >= 0x0800)
		value -= 0x1000;  /* convert to signed value */
	value = conv(value, VoltageFactor);  /* result in mV */
	BattData->Voltage = value;  /* result in mV */

	/* temperature */
	value = data[10];
	if (value >= 0x80)
		value -= 0x100;  /* convert to signed value */
	BattData->Temperature = value*10;  /* result in 0.1°C */

	/* Avg current */
	value = data[12];
	value = (value<<8) + data[11];

	if (value >= 0x8000)
		value -= 0x10000;  /* convert to signed value */
	if (BattData->Vmode == 0) {
		value = conv(value, BattData->CurrentFactor);
		value = value / 4;  /* divide by 4  */
	} else {
		value = conv(value, BattData->CRateFactor);
	}
	BattData->AvgCurrent = value;  /* result in mA */

	/* OCV */
	value = data[14];
	value = (value<<8) + data[13];

	value &= 0x3fff; /* mask unused bits */
	if (value >= 0x02000)
		value -= 0x4000;  /* convert to signed value */
	value = conv(value, VoltageFactor);
	value = (value+2) / 4;  /* divide by 4 with rounding */
	BattData->OCV = value;  /* result in mV */
	g_ocv = BattData->OCV;

	/* read STC3117 registers CC & VM adj */
	res = STC31xx_Read(4, STC311x_REG_CC_ADJ, data);
	if (res < 0)
		return res;  /* read failed */

	/* CC & VM adjustment counters */
	value = data[1];
	value = (value<<8) + data[0];

	if (value >= 0x8000)
		value -= 0x10000;  /* convert to signed value */
	BattData->CC_adj = value; /* in 1/512% */

	value = data[3];
	value = (value<<8) + data[2];

	if (value >= 0x8000)
		value -= 0x10000;  /* convert to signed value */
	BattData->VM_adj = value; /* in 1/512% */

	/* relax counter */
	res = STC31xx_Read(1, STC311x_REG_CMONIT_COUNT, data);
	if (res < 0)
		return res;  /* read failed */
	BattData->RelaxTimer = data[0];

	return OK;
}


/*****************************************************************************
 * Function Name  : STC311x_ReadRamData
 * Description    : utility function to read the RAM data from STC311x
 * Input          : ref to RAM data array
 * Return         : error status (OK, !OK)
 *****************************************************************************/
static int STC311x_ReadRamData(unsigned char *RamData)
{
	int ret;

	ret = STC31xx_Read(RAM_SIZE, STC311x_REG_RAM, RamData);
	return ret;
}


/*****************************************************************************
 * Function Name  : STC311x_WriteRamData
 * Description    : utility function to write the RAM data into STC311x
 * Input          : ref to RAM data array
 * Return         : error status (OK, !OK)
 *****************************************************************************/
static int STC311x_WriteRamData(unsigned char *RamData)
{
	int ret;

	ret = STC31xx_Write(RAM_SIZE, STC311x_REG_RAM, RamData);
	return ret;
}

/*****************************************************************************
 * Function Name  : Interpolate
 * Description    : interpolate a Y value from a X
 * value and X, Y tables (n points)
 * Input          : x
 * Return         : y
 *****************************************************************************/
static int interpolate(int x, int n, int const *tabx, int const *taby)
{
	int index;
	int y;

	if (x >= tabx[0])
		y = taby[0];
	else if (x <= tabx[n-1])
		y = taby[n-1];
	else {
		/*  find interval */
		for (index = 1; index < n; index++)
			if (x > tabx[index])
				break;
		/*  interpolate */
		if ((tabx[index-1] - tabx[index]) != 0) {
			/*avoid divide by 0*/
			y = (taby[index-1] - taby[index]) *
				(x - tabx[index]) * 2 /
				(tabx[index-1] - tabx[index]);
			y = (y+1) / 2;
			y += taby[index];
		} else
			y = taby[index];
	}
	return y;
}



/*****************************************************************************
 * Function Name  : calcCRC8
 * Description    : calculate the CRC8
 * Input          : data: pointer to byte array, n: number of vytes
 * Return         : CRC calue
 *****************************************************************************/
static int calcCRC8(unsigned char *data, int n)
{
	int crc = 0, ret;   /* initial value */
	int i, j;

	for (i = 0; i < n; i++) {
		crc ^= data[i];
		for (j = 0; j < 8; j++) {
			crc <<= 1;
			if (crc & 0x100)
				crc ^= 7;
		}
	}
	ret = crc & 255;
	return ret;
}


/*****************************************************************************
 * Function Name  : UpdateRamCrc
 * Description    : calculate the RAM CRC
 * Input          : none
 * Return         : CRC value
 *****************************************************************************/
static int UpdateRamCrc(void)
{
	int res;

	res = calcCRC8(GG_Ram.db, RAM_SIZE-1);
	/* last byte holds the CRC */
	GG_Ram.db[RAM_SIZE-1] = res;
	return res;
}

/*****************************************************************************
 * Function Name  : Init_RAM
 * Description    : Init the STC311x RAM registers with valid test word and CRC
 * Input          : none
 * Return         : none
 *****************************************************************************/
static void Init_RAM(void)
{
	int index;

	for (index = 0; index < RAM_SIZE; index++)
		GG_Ram.db[index] = 0;
	GG_Ram.reg.TstWord = RAM_TSTWORD;  /* id. to check RAM integrity */
	GG_Ram.reg.CC_cnf = BattData.CC_cnf;
	GG_Ram.reg.VM_cnf = BattData.VM_cnf;
	/* update the crc */
	UpdateRamCrc();
}

/* compensate SOC with temperature, SOC in 0.1% units */
static int CompensateSOC(int value, int temp)
{
	int r, v;

	r = 0;
#ifdef TEMPCOMP_SOC
	/* for APP_TYP_CURRENT */
	r = interpolate(temp/10, NTEMP, TempTable, BattData.CapacityDerating);
#endif

	if ((MAX_SOC-r) != 0) {
		/*avoid divide by 0*/
		v = (long) (value-r) * MAX_SOC * 2 / (MAX_SOC-r);
		/* compensate */
		v = (v+1)/2;  /* rounding */
		if (v < 0)
			v = 0;
		if (v > MAX_SOC)
			v = MAX_SOC;
	}

	return v;
}






/*****************************************************************************
 * Function Name  : MM_FSM
 * Description    : process the Gas Gauge state machine in mixed mode
 * Input          : BattData
 * Return         :
 * Affect         : Global Gas Gauge data
 *****************************************************************************/
static void MM_FSM(void)
{

	switch (BattData.BattState) {
	case BATT_CHARGING:
		if (BattData.AvgCurrent < CHG_MIN_CURRENT) {
			/* end of charge */
			BattData.BattState = BATT_ENDCHARG;
		}
		break;
	/* end of charge state. check if fully charged or charge interrupted */
	case BATT_ENDCHARG:
		if (BattData.Current > CHG_MIN_CURRENT)
			BattData.BattState = BATT_CHARGING;
		else if (BattData.AvgCurrent < CHG_END_CURRENT) {
			/* charge interrupted */
			BattData.BattState = BATT_IDLE;
		} else if ((BattData.Current > CHG_END_CURRENT) &&
			  (BattData.Voltage > BATT_CHG_VOLTAGE)) {
			/* end of charge */
			BattData.BattState = BATT_FULCHARG;
		}
		break;
	/* full charge state. wait for actual end of charge current */
	case BATT_FULCHARG:
		if (BattData.Current > CHG_MIN_CURRENT)
			BattData.BattState = BATT_CHARGING;  /* charge again */
		else if ((BattData.AvgCurrent < CHG_END_CURRENT) && (BattData.AvgCurrent >= 0)) {
			if (BattData.AvgVoltage > BATT_CHG_VOLTAGE) {
				/* end of charge detected */
				STC311x_SetSOC(MAX_HRSOC);
				BattData.SOC = MAX_SOC;  /* 100% */
			}
			/* end of charge cycle */
			BattData.BattState = BATT_IDLE;
		}
		break;
	case BATT_IDLE:  /* no charging, no discharging */
		if (BattData.Current > CHG_END_CURRENT) {
			/* charging again */
			BattData.BattState = BATT_CHARGING;
		} else if (BattData.Current < APP_MIN_CURRENT) {
			/* discharging again */
			BattData.BattState = BATT_DISCHARG;
		}
		break;
	case BATT_DISCHARG:
		if (BattData.Current > APP_MIN_CURRENT)
			BattData.BattState = BATT_IDLE;
		else if (BattData.AvgVoltage < BATT_MIN_VOLTAGE)
			BattData.BattState = BATT_LOWBATT;
		break;
	case BATT_LOWBATT:  /* battery nearly empty... */
		if (BattData.AvgVoltage > (BATT_MIN_VOLTAGE+50))
			BattData.BattState = BATT_IDLE;   /* idle */
		else
			break;
		break;
	default:
		BattData.BattState = BATT_IDLE;   /* idle */
	} /* end switch */
}


static void CompensateVM(int temp)
{
	int r;

#ifdef TEMPCOMP_SOC
	r = interpolate(temp/10, NTEMP, TempTable, BattData.VM_TempTable);
	GG_Ram.reg.VM_cnf = (BattData.VM_cnf * r) / 100;
	STC311x_SaveVMCnf();  /* save new VM cnf values to STC311x */
#endif
}


/*****************************************************************************
 * Function Name  : VM_FSM
 * Description    : process the Gas Gauge machine in voltage mode
 * Input          : BattData
 * Return         :
 * Affect         : Global Gas Gauge data
 *****************************************************************************/
static void VM_FSM(void)
{

#define DELTA_TEMP 30   /* 3 °C */

	/* in voltage mode, monitor temperature to compensate voltage mode
	 * gain*/

	if ((BattData.AvgTemperature > (BattData.LastTemperature+DELTA_TEMP)) ||
	    (BattData.AvgTemperature <
	     (BattData.LastTemperature-DELTA_TEMP))) {
		BattData.LastTemperature = BattData.AvgTemperature;
		CompensateVM(BattData.AvgTemperature);
	}

}




/*****************************************************************************
 * Function Name  : Reset_FSM_GG
 * Description    : reset the gas gauge state machine and flags
 * Input          : None
 * Return         : None
 *****************************************************************************/
static void Reset_FSM_GG(void)
{
	BattData.BattState = BATT_IDLE;
}




/* -------------------- Algo functions ------------------------------------- */

//Temporarily skip soc_correction() to fix soc jumps problem
#define OG2

#define CURRENT_TH  (GG->Cnom/10)
#define GAIN 10
#define A_Var3 500
#define VAR1MAX 64
#define VAR2MAX 128
#define VAR4MAX 128


void SOC_correction(struct GasGauge_DataTypeDef *GG)
{
#ifdef OG2
	int Var1 = 0;
	int Var2, Var3, Var4;
	int SOCopt;

	if (BattData.SOC > 800)
		Var3 = 600;
	else if (BattData.SOC > 500)
		Var3 = 400;
	else if (BattData.SOC > 250)
		Var3 = 200;
	else if (BattData.SOC > 100)
		Var3 = 300;
	else
		Var3 = 400;

	if (g_debug) {
		pr_err("before SOC_correction: BattData.SOC = %d (0.1) \n", BattData.SOC);	
		pr_err("Var3 = %d, BattData.AvgCurrent = %d \n", Var3, BattData.AvgCurrent);
	}
	Var1 = 256*BattData.AvgCurrent*A_Var3/Var3/CURRENT_TH;
	Var1 = 32768 * GAIN / (256+Var1*Var1/256) / 10;
	Var1 = (Var1+1)/2;
	if (Var1 == 0)
		Var1 = 1;
	if (Var1 >= VAR1MAX)
		Var1 = VAR1MAX-1;
	GG->Var1 = Var1;

	Var4 = BattData.CC_adj-BattData.VM_adj;
	if (BattData.GG_Mode == CC_MODE) {
		SOCopt = BattData.HRSOC + Var1 * Var4 / 64;
		if (g_debug)
			pr_err("CC mode:  Raw SOC (%d) = BattData.HRSOC (%d) + Var1 (%d) * Var4 (%d) / 64 \n", SOCopt, BattData.HRSOC, Var1, Var4);
	} else {
		SOCopt = BattData.HRSOC - BattData.CC_adj + Var1 * Var4 / 64;
		if (g_debug)
			pr_err("VM mode:  Raw SOC (%d) = BattData.HRSOC (%d) - BattData.CC_adj (%d) + Var1 (%d) * Var4 (%d) / 64 \n", SOCopt, BattData.HRSOC, BattData.CC_adj, Var1, Var4);
	}

	Var2 = BattData.Nropt;
	if ((BattData.AvgCurrent < (-CURRENT_TH)) ||
	    (BattData.AvgCurrent > CURRENT_TH)) {
		if (Var2 < VAR2MAX)
			Var2++;

		if ((BattData.AvgCurrent != 0) && (Var2 != 0)) {
			/*avoid divide by 0*/
			BattData.Ropt = BattData.Ropt +
				(1000 * (BattData.Voltage-BattData.OCV) /
				 BattData.AvgCurrent - BattData.Ropt / Var2);
		}
		BattData.Nropt = Var2;
	}
	if (Var2 > 0)
		GG->Ropt = BattData.Ropt / Var2;
	else
		GG->Ropt = 0;  /* not available*/

	if (SOCopt <= 0)
		SOCopt = 0;
	if (SOCopt >= MAX_HRSOC)
		SOCopt = MAX_HRSOC;

	BattData.SOC = (SOCopt*10+256)/512;
	if ((Var4 < (-VAR4MAX)) || (Var4 >= VAR4MAX)) {
		/*rewrite SOCopt into STC311x and clear acc registers*/
		pr_err("SOC_correction() set new raw SOC: %d (1/512) \n", SOCopt);
		STC311x_SetSOC(SOCopt);
	}

	if (g_debug)
		pr_err("after SOC_correction: BattData.SOC = %d (0.1) \n", BattData.SOC);	
#endif

}

/* --------------------------------------------------------------------------*/



/* -------------------- firmware interface functions
 * ------------------------------------------- */




/*****************************************************************************
 * Function Name  : GasGauge_Start
 * Description    : Start the Gas Gauge system
 * Input          : algo parameters in GG structure
 * Return         : 0 is ok, -1 if STC310x not found or I2C error
 * Affect         : global STC310x data and gas gauge variables
 *****************************************************************************/
int GasGauge_Start(struct GasGauge_DataTypeDef *GG)
{
	int res, i;

	BattData.Cnom = GG->Cnom;
	BattData.Rsense = GG->Rsense;
	BattData.Rint = GG->Rint;
	BattData.Vmode = GG->Vmode;
	BattData.CC_cnf = GG->CC_cnf;
	BattData.VM_cnf = GG->VM_cnf;
	BattData.Alm_SOC = GG->Alm_SOC;
	BattData.Alm_Vbat = GG->Alm_Vbat;
	BattData.RelaxThreshold = GG->RelaxCurrent;

	/* Init averaging */
	BattData.AvgVoltage = 0;
	BattData.AvgCurrent = 0;
	BattData.AvgTemperature = 0;
	BattData.AvgSOC = 0;  /* in 0.1% unit  */
	BattData.AccVoltage = 0;
	BattData.AccCurrent = 0;
	BattData.AccTemperature = 0;
	BattData.AccSOC = 0;

	/* BATD*/
	BattData.BattOnline = 1;

	/* default value in case, to avoid divide by 0 */
	if (BattData.Rsense == 0)
		BattData.Rsense = 10;
	/* LSB=5.88uV/R= ~24084/R/4096 - convert to mA  */
	BattData.CurrentFactor = 24084/BattData.Rsense;
	/* LSB=0.008789.Cnom= 36*Cnom/4096 - convert to mA  */
	BattData.CRateFactor = 36*BattData.Cnom;

	if (BattData.CC_cnf == 0)
		BattData.CC_cnf = 395;  /* default values */
	if (BattData.VM_cnf == 0)
		BattData.VM_cnf = 321;

	for (i = 0; i < NTEMP; i++)
		BattData.CapacityDerating[i] = GG->CapDerating[i];
	for (i = 0; i < OCVTAB_SIZE; i++) {
		BattData.OCVValue[i] = GG->OCVValue[i];
		BattData.SOCValue[i] = GG->SOCValue[i];
	}
	for (i = 0; i < NTEMP; i++)
		BattData.VM_TempTable[i] = DefVMTempTable[i];

	BattData.Ropt = 0;
	BattData.Nropt = 0;

	/* check RAM valid */
	STC311x_ReadRamData(GG_Ram.db);

	if ((GG_Ram.reg.TstWord != RAM_TSTWORD) ||
	    (calcCRC8(GG_Ram.db, RAM_SIZE) != 0)) {
		/* RAM invalid */
		Init_RAM();
		/* return -1 if I2C error or STC3117 not present */
		res = STC311x_Startup();
	} else {
		/* check STC3117 status */
		if (((STC311x_Status() & M_RST) != 0) || g_standby_mode) {
			/* return -1 if I2C error or STC3117 not present */
			pr_err("UVLO, Battery fail, Power on reset, or in standby mode, do STC311x_Startup \n");
			res = STC311x_Startup();
		} else {
			res = STC311x_Restore(); /* recover from last SOC */
		}
	}


	GG_Ram.reg.GG_Status = GG_INIT;
	GG_Ram.reg.CC_cnf = BattData.CC_cnf;
	GG_Ram.reg.VM_cnf = BattData.VM_cnf;
	/* update the crc */
	UpdateRamCrc();
	STC311x_WriteRamData(GG_Ram.db);

	Reset_FSM_GG();

	return res;    /* return -1 if I2C error or STC3117 not present */
}





/*****************************************************************************
  Restart sequence:
Usage:
call GasGaugeReset()
powerdown everything
wait 500ms
call GasGaugeStart(GG)
continue
 *****************************************************************************/


/*****************************************************************************
 * Function Name  : GasGauge_Reset
 * Description    : Reset the Gas Gauge system
 * Input          : None
 * Return         : 0 is ok, -1 if I2C error
 *****************************************************************************/
void GasGauge_Reset(void)
{
	GG_Ram.reg.TstWord = 0;  /* reset RAM */
	GG_Ram.reg.GG_Status = 0;
	STC311x_WriteRamData(GG_Ram.db);

	STC311x_Reset();
}



/*****************************************************************************
 * Function Name  : GasGauge_Stop
 * Description    : Stop the Gas Gauge system
 * Input          : None
 * Return         : 0 is ok, -1 if I2C error
 *****************************************************************************/
int GasGauge_Stop(void)
{
	int res, ret;

	STC311x_ReadRamData(GG_Ram.db);
	GG_Ram.reg.GG_Status = GG_POWERDN;
	/* update the crc */
	UpdateRamCrc();
	STC311x_WriteRamData(GG_Ram.db);

	res = STC311x_Powerdown();
	if (res != 0) {
		ret = -1;
		return ret;  /* error */
	}

	return 0;
}

void stc311x_debug_info(void)
{
	int value,value2, value3, value4, i, run_mode, data_ocv, data_soc, data_voltage;
	unsigned char data[31], ram[16];
	unsigned char OCVTAB[32]; /* 48~79 */
	unsigned char SOCTAB[16]; /* 80~95 */

	/* read STC3117 registers from 0 to 30 */
	value = STC31xx_Read(31, 0, data);
	if (value < 0)
		return;

	// read ram data from 32-47
	value = STC31xx_Read(16, 32, ram);
	pr_err("Ram data: [0-1] TstWord = 0x%x, [2-3] HRSOC = %d (1/512)\n", (ram[1]<<8)+ram[0], ( (ram[3]<<8)+ram[2]));
	pr_err("Ram data: [4-5] CC_cnf = %d, [6-7] VM_cnf = %d \n", (ram[5]<<8)+ram[4], (ram[7]<<8)+ram[6]);
	pr_err("Ram data: [8] SOC = %d (1/1),  [9] GG_Status = %d \n", ram[8], ram[9]);

	/* MODE */
	pr_err("REG[00] MODE = 0x%x \n", data[0]);

	/* CTRL */
	run_mode = data[1] & GG_VM_BIT ? VM_MODE : CC_MODE;
	pr_err("REG[01] CTRL = 0x%x, %s \n", data[1], run_mode ? "VM_MODE" : "CC_MODE");

	/* SOC */	
	value = data[3];
	value = (value<<8) + data[2];
	data_soc = (int)(value/512);
	pr_err("REG[02-03] HRSOC = %d, SOC = %d\n", value, (int)(value/512));

	/* COUNTER */
	value = data[5];
	value = (value<<8) + data[4];
	pr_err("REG[04-05] COUNTER = %d \n", value);

	/* CURRENT */
	value = data[7];
	value = (value<<8) + data[6];

	value &= 0x3fff;   /* mask unused bits */
	if (value >= 0x2000)
		value -= 0x4000;  /* convert to signed value */
	/* result in mA */
	value = conv(value, (24084/10)); //BattData->CurrentFactor
	pr_err("REG[06-07] CURRENT = %d mA \n", value);

	/* VOLTAGE */
	value = data[9];
	value = (value<<8) + data[8];

	value &= 0x0fff; /* mask unused bits */
	if (value >= 0x0800)
		value -= 0x1000;  /* convert to signed value */
	value = conv(value, VoltageFactor);  /* result in mV */
	data_voltage = value;
	pr_err("REG[08-09] VOLTAGE = %d mv \n", value);

	/* temperature */
	value = data[10];
	if (value >= 0x80)
		value -= 0x100;  /* convert to signed value */
	//BattData->Temperature = value*10;  /* result in 0.1°C */
	pr_err("REG[10] temperature = %d degC \n", value*10);
	
	/* Avg current */
	value = data[12];
	value = (value<<8) + data[11];

	if (value >= 0x8000)
		value -= 0x10000;  /* convert to signed value */
	if ((data[0] & 0x01) == 0) {//mix mode
		value = conv(value, (24084/10)); //BattData->CurrentFactor
		value = value / 4;  /* divide by 4  */
	} else {
		value = conv(value, (36*400)); //BattData->CRateFactor
	}
	pr_err("REG[11-12] AVG Current = %d mA \n", value);
	
	/* OCV */
	value = data[14];
	value = (value<<8) + data[13];

	value &= 0x3fff; /* mask unused bits */
	if (value >= 0x02000)
		value -= 0x4000;  /* convert to signed value */
	value = conv(value, VoltageFactor);
	data_ocv = (value+2) / 4;  /* divide by 4 with rounding */
	//BattData->OCV = value;  /* result in mV */
	pr_err("REG[13-14] OCV = %d mv \n", data_ocv);

	/* CC_CNF */
	value = data[16];
	value = (value<<8) + data[15];
	pr_err("REG[15-16] CC_CNF = %d \n", value);

	/* VM_CNF */
	value = data[18];
	value = (value<<8) + data[17];
	pr_err("REG[17-18] VM_CNF = %d \n", value);

	/* ALARM_SOC */
	//pr_err("REG[19] ALARM_SOC = %d \n", (int)(data[19]/2));
	
	/* ALARM_VOLTAGE */
	//pr_err("REG[20] ALARM_VOLTAGE = %d mv \n", (int)((data[20]*176)/10));

	/* CURRENT_THRES */
	pr_err("REG[21] CURRENT_THRES = %d \n", (int)(data[21]*47/10));

	/* CMONIT_COUNT */
	pr_err("REG[22] CMONIT_COUNT = %d \n", data[22]);

	/* CMONIT_MAX */
	pr_err("REG[23] CMONIT_MAX = %d \n", data[23]);

	/* ID */
	//pr_err("REG[24] ID = %d \n", data[24]);

	/* read STC3117 registers CC & VM adj */
	/* CC & VM adjustment counters */
	value = data[28];
	value = (value<<8) + data[27];

	if (value >= 0x8000)
		value -= 0x10000;  /* convert to signed value */
	//BattData->CC_adj = value; /* in 1/512% */
	pr_err("REG[[27-28] CC_ADJ = %d \n", value);


	value = data[30];
	value = (value<<8) + data[29];

	if (value >= 0x8000)
		value -= 0x10000;  /* convert to signed value */
	//BattData->VM_adj = value; /* in 1/512% */
	pr_err("REG[29-30] VM_ADJ = %d \n", value);


	// read OCVTAB registers from 48 to 79
	value = STC31xx_Read(32, 48, OCVTAB);
	if (value < 0)
		return;
	else {
		for (i=0; i<32; i+=8) {
			value = OCVTAB[i+1];
			value = (value<<8) + OCVTAB[i];
			value2 = OCVTAB[i+3];
			value2 = (value2<<8) + OCVTAB[i+2];
			value3 = OCVTAB[i+5];	
			value3 = (value3<<8) + OCVTAB[i+4];
			value4 = OCVTAB[i+7];
			value4 = (value4<<8) + OCVTAB[i+6];
			pr_err("OCV_TAB[%d]: %d, [%d]: %d, [%d]: %d, [%d]: %d \n",
				(i/2), (value*55/100), (i+2)/2, (value2*55/100), (i+4)/2, (value3*55/100), (i+6)/2, (value4*55/100));
			}
			
		}

	// read SOCTAB registers from 80 to 95
	value = STC31xx_Read(16, 80, SOCTAB);
	if (value < 0)
		return;
	else {
		for (i=0; i<16; i+=4)
			pr_err("SOC_TAB[%d]: %d, [%d]: %d, [%d]: %d, [%d]: %d \n",
			i, (SOCTAB[i]/2), i+1, (SOCTAB[i+1]/2), i+2, (SOCTAB[i+2]/2), i+3, (SOCTAB[i+3]/2));
	}

	pr_err("mode=0x%x, ctrl=0x%x, CMONIT_COUNT=%d, SOC=%d, voltage=%d, OCV=%d \n",data[0], data[1], data[22], data_soc, data_voltage, data_ocv);
}

static void STC311x_Rewrite_OCV(void)
{
	int mode, ocv, curr, voltage, soc;	

	mode = STC31xx_ReadByte(STC311x_REG_MODE);
	if (mode < 0)
		return ;

	if(mode & GG_RUN_BIT)
		return;

	pr_info("Rewrite OCV due to standby mode, reg_mode = 0x%x \n", mode);
	//set operation mode to get current	
	STC31xx_WriteByte(STC311x_REG_MODE, 0x10);
	mdelay(200);

	/* read OCV */
	ocv = STC31xx_ReadWord(STC311x_REG_OCV);
	pr_info("reg_OCV = %d \n", (ocv * 55 / 100));

	/* with STC3117, it is possible here to read the current and
	 * compensate OCV:*/
	curr = STC31xx_ReadWord(STC311x_REG_CURRENT);
	curr &= 0x3fff;   /* mask unused bits */
	if (curr >= 0x2000)
		curr -= 0x4000;  /* convert to signed value */

	pr_info("BattData.Rsense = %d, BattData.Rint = %d,  curr = %d \n", BattData.Rsense, BattData.Rint, curr);
	if (BattData.Rsense != 0) {
		/*avoid divide by 0*/
		ocv = ocv - BattData.Rint * curr * 588 /
			BattData.Rsense / 55000;
	} else
		return;

	//back to standby state
	STC31xx_WriteByte(STC311x_REG_MODE, mode);

	/* rewrite ocv to start SOC with updated OCV curve */
	pr_info("rewrite ocv = %d \n", (ocv * 55 / 100));
	STC31xx_WriteWord(STC311x_REG_OCV, ocv);

	mdelay(200);
	mode = STC31xx_ReadByte(STC311x_REG_MODE);	
	soc = STC31xx_ReadWord(STC311x_REG_SOC);
	ocv = STC31xx_ReadWord(STC311x_REG_OCV);
	voltage = STC31xx_ReadWord(STC311x_REG_VOLTAGE);
	pr_info("mode = 0x%x, soc = %d, OCV = %d, voltage = %d \n", mode, (soc/512), (ocv * 55 / 100), (voltage * 22 / 10));
}

/*****************************************************************************
 * Function Name  : GasGauge_Task
 * Description    : Periodic Gas Gauge task, to be called e.g. every 5 sec.
 * Input          : pointer to gas gauge data structure
 * Return         : 1 if data available, 0 if no data, -1 if error
 * Affect         : global STC310x data and gas gauge variables
 *****************************************************************************/
int GasGauge_Task(struct GasGauge_DataTypeDef *GG)
{
	int res, value, ret, batt_temp;

	BattData.Cnom = GG->Cnom;
	BattData.Rsense = GG->Rsense;
	BattData.Vmode = GG->Vmode;
	BattData.Rint = GG->Rint;
	BattData.CC_cnf = GG->CC_cnf;
	BattData.VM_cnf = GG->VM_cnf;
	BattData.Alm_SOC = GG->Alm_SOC;
	BattData.Alm_Vbat = GG->Alm_Vbat;
	BattData.RelaxThreshold = GG->RelaxCurrent;

	/* read battery data into global variables */
	res = STC311x_ReadBatteryData(&BattData);
	if (res != 0) {
		ret = -1;
		return ret; /* abort in case of I2C failure */
	}

	/* check if RAM data is ok (battery has not been changed) */
	STC311x_ReadRamData(GG_Ram.db);
	if ((GG_Ram.reg.TstWord != RAM_TSTWORD) ||
	    (calcCRC8(GG_Ram.db, RAM_SIZE) != 0)) {
		/* if RAM non ok, reset it and set init state */
		Init_RAM();
		GG_Ram.reg.GG_Status = GG_INIT;
	}

	/*Check battery presence*/
	if ((BattData.STC_Status & M_BATFAIL) != 0)
		BattData.BattOnline = 0;
	/* check STC3117 status */
#ifdef BATD_UC8
	/* check STC3117 status */
	if (((BattData.STC_Status & (M_BATFAIL | M_UVLOD)) != 0) || g_standby_mode) {
		/* BATD or UVLO detected */
		if (BattData.ConvCounter > 0) {
			GG->Voltage = BattData.Voltage;
			GG->SOC = (BattData.HRSOC*10+256)/512;
		}
		if ((BattData.STC_Status & M_BATFAIL) != 0)
			pr_err("stc3117 BATD error, gauge reset.\n");
		else if ((BattData.STC_Status & M_UVLOD) != 0)
			pr_err("stc3117 UVLOD error, gauge reset.\n");
		else if (g_standby_mode)
			pr_err("stc3117 in standby mode, gauge reset.\n");

		g_standby_mode = 0;
		/* BATD or UVLO detected */
		GasGauge_Reset();

		ret = -1;
		return ret;
	}
#endif

	if ((BattData.STC_Status & M_RUN) == 0) {
		pr_err("stc311x in standby mode, rewrite OCV \n");
		STC311x_Rewrite_OCV();
		g_force_SOC_update = 1;

		/* if not running, restore STC3117 */
		STC311x_Restore();
		GG_Ram.reg.GG_Status = GG_INIT;

		//update SOC
		mdelay(500);
		BattData.HRSOC = STC31xx_ReadWord(STC311x_REG_SOC);
		pr_info("update new SOC = %d \n", BattData.HRSOC);
	}

	BattData.SOC = (BattData.HRSOC*10+256)/512;  /* in 0.1% unit  */
	if (g_debug)
		pr_err(" 1st BattData.SOC = %d (0.1)\n", BattData.SOC);

	/*Force an external temperature*/
	if (GG->ForceExternalTemperature == 1) {
		batt_temp = GG->ExternalTemperature;
		if (g_debug)
			pr_err("stc3117 temp= %d, battery NTC temp = %d \n", BattData.Temperature, batt_temp);

		//If read PMIC NTC success, update the temperature. Else, use stc3117 temperature
		if (batt_temp != BATTERY_NTC_ERROR_TEMP)
			BattData.Temperature = batt_temp;
		else
			pr_err("stc3117 temp = %d, NTC temp error = %d \n", BattData.Temperature, batt_temp);
	}

	/* check INIT state */
	if (GG_Ram.reg.GG_Status == GG_INIT) {
		if (g_debug)	
			pr_err("GG_INIT \n");
		/* INIT state, wait for current & temperature value available:
		 * */
		if (BattData.ConvCounter > VCOUNT) {
			/* update VM_cnf */
			CompensateVM(BattData.Temperature);
			BattData.LastTemperature = BattData.Temperature;

			/* Init averaging */
			BattData.AvgVoltage = BattData.Voltage;
			BattData.AvgCurrent = BattData.Current;
			BattData.AvgTemperature = BattData.Temperature;
			/* in 0.1% unit  */
			BattData.AvgSOC = CompensateSOC(BattData.SOC,
							BattData.Temperature);
			BattData.AccVoltage = BattData.AvgVoltage*AVGFILTER;
			BattData.AccCurrent = BattData.AvgCurrent*AVGFILTER;
			BattData.AccTemperature = BattData.AvgTemperature*
				AVGFILTER;
			BattData.AccSOC = BattData.AvgSOC*AVGFILTER;

			GG_Ram.reg.GG_Status = GG_RUNNING;
		}
	}

	if (GG_Ram.reg.GG_Status != GG_RUNNING) {
		if (g_debug) {
			pr_err("not GG_RUNNING \n");
			pr_err("before CompensateSOC: GG->SOC = %d (0.1),  BattData.SOC = %d (0.1), temp = 25 degC \n", GG->SOC, BattData.SOC);
		}
		GG->SOC = CompensateSOC(BattData.SOC, 250);
		if (g_debug)
			pr_err("after CompensateSOC: GG->SOC = %d (0.1),  BattData.SOC (0.1) = %d \n", GG->SOC, BattData.SOC);

		GG->Voltage = BattData.Voltage;
		GG->OCV = BattData.OCV;
		GG->Current = 0;
		GG->RemTime = -1;   /* means no estimated time available */
		GG->Temperature = 250;
	} else {
		if (g_debug)
			pr_err("GG_RUNNING \n");
		/*Check battery presence*/
		if ((BattData.STC_Status & M_BATFAIL) == 0)
			BattData.BattOnline = 1;

		if (g_boot_phase == 0)
			SOC_correction(GG);
		/* SOC derating with temperature */
		BattData.SOC = CompensateSOC(BattData.SOC,
					     BattData.Temperature);
		if (g_debug)
			pr_err("temperature compensate SOC: BattData.SOC = %d (0.1), temp = %d degC \n", BattData.SOC, BattData.Temperature);

		/*early empty compensation*/
		value = BattData.AvgVoltage;
		if (BattData.Voltage < value)
			value = BattData.Voltage;
		//In the boot up phase, skip early empty compensation to avoid soc drop
		if ((g_boot_phase == 0) && value < (APP_MIN_VOLTAGE+APP_MIN_THRESHOLD) &&
		    value > (APP_MIN_VOLTAGE-500)) {
			if (g_debug)
				pr_err("early empty compensation: voltage = %d, BattData.SOC = %d (0.1)\n", value, BattData.SOC);
			if ((value < APP_MIN_VOLTAGE) && ((BattData.AvgCurrent > -100) && (BattData.AvgCurrent < 0)))
				BattData.SOC = 0;
			else
				BattData.SOC = BattData.SOC *
					(value - APP_MIN_VOLTAGE) / APP_MIN_THRESHOLD;
			if (g_debug)
				pr_err("early empty compensation:  new BattData.SOC = %d (0.1)\n", BattData.SOC);
		}

		BattData.AccVoltage += (BattData.Voltage - BattData.AvgVoltage);
		BattData.AccCurrent += (BattData.Current - BattData.AvgCurrent);
		BattData.AccTemperature +=
			(BattData.Temperature - BattData.AvgTemperature);
		BattData.AccSOC +=  (BattData.SOC - BattData.AvgSOC);

		BattData.AvgVoltage =
			(BattData.AccVoltage+AVGFILTER/2)/AVGFILTER;
		BattData.AvgTemperature =
			(BattData.AccTemperature+AVGFILTER/2)/AVGFILTER;
		BattData.AvgSOC = (BattData.AccSOC+AVGFILTER/2)/AVGFILTER;

		/* ---------- process the Gas Gauge algorithm -------- */

		if (BattData.Vmode)
			VM_FSM();  /* in voltage mode */
		else
			MM_FSM();  /* in mixed mode */

		if (BattData.Vmode == 0) {
			/* Lately fully compensation*/
			if (BattData.AvgCurrent > 0 &&
			   BattData.SOC >= 990 &&
			   BattData.SOC < 995 &&
			   BattData.AvgCurrent > 100) {
				BattData.SOC = 990;
				STC311x_SetSOC(99*512);
			}
			/* Lately empty compensation*/
			if (BattData.AvgCurrent < 0 &&
			   BattData.SOC >= 15 &&
			   BattData.SOC < 20 &&
			   BattData.Voltage > (APP_MIN_VOLTAGE+APP_MIN_THRESHOLD)) {
			   if (g_debug)
					pr_err("2P Lately empty compensation: BattData.SOC=%d, APP_MIN_VOLTAGE=%d, APP_MIN_THRESHOLD=%d \n", BattData.SOC, APP_MIN_VOLTAGE, APP_MIN_THRESHOLD);
				BattData.SOC = 20;
				STC311x_SetSOC(2*512);
			}
		}

		//Set max SOC to fix SOC > 100
		if (BattData.HRSOC > (MAX_HRSOC+512)) {
			BattData.SOC = MAX_SOC;
			STC311x_SetSOC(MAX_HRSOC+512);
		}
		if (BattData.SOC <= 0)
			BattData.SOC = 0;

		/* -------- APPLICATION RESULTS ------------ */

		/* fill gas gauge data with battery data */
		GG->Voltage = BattData.Voltage;
		GG->Current = BattData.Current;
		GG->Temperature = BattData.Temperature;
		GG->SOC = BattData.SOC;
		GG->OCV = BattData.OCV;

		GG->AvgVoltage = BattData.AvgVoltage;
		GG->AvgCurrent = BattData.AvgCurrent;
		GG->AvgTemperature = BattData.AvgTemperature;
		GG->AvgSOC = BattData.AvgSOC;

		if (BattData.Vmode) {
			/* no current value in voltage mode */
			GG->Current = 0;
			GG->AvgCurrent = 0;
		}

		GG->ChargeValue =
			(long) BattData.Cnom * BattData.AvgSOC / MAX_SOC;
		if (GG->AvgCurrent < APP_MIN_CURRENT) {
			GG->State = BATT_DISCHARG;
			/* in minutes */
			if (GG->AvgCurrent != 0) {
				/*avoid divide by 0*/
				value = GG->ChargeValue * 60 /
					(-GG->AvgCurrent);
				if (value < 0)
					value = 0;
				GG->RemTime = value;
			}
		} else {
			/* means no estimated time available */
			GG->RemTime = -1;
			if (GG->AvgCurrent > CHG_END_CURRENT)
				GG->State = BATT_CHARGING;
			else
				GG->State = BATT_IDLE;
		}
	}

	/* save SOC */
	GG_Ram.reg.HRSOC = BattData.HRSOC;
	GG_Ram.reg.SOC = (GG->SOC+5)/10;    /* trace SOC in % */
	UpdateRamCrc();
	STC311x_WriteRamData(GG_Ram.db);
	if (g_debug)
		pr_err("Save to memory:  GG_Ram.reg.HRSOC = %d (1/512), GG_Ram.reg.SOC = %d (1/1) \n", GG_Ram.reg.HRSOC, GG_Ram.reg.SOC);

	if (GG_Ram.reg.GG_Status == GG_RUNNING)
		return 1;
	else
		return 0;  /* only SOC, OCV and voltage are valid */
}




/*****************************************************************************
 * Function Name  : STC31xx_SetPowerSavingMode
 * Description    :  Set the power saving mode
 * Input          : None
 * Return         : error status (OK, !OK)
 *****************************************************************************/
int STC31xx_SetPowerSavingMode(void)
{
	int res;

	/* Read the mode register*/
	res = STC31xx_ReadByte(STC311x_REG_MODE);

	/* Set the VMODE bit to 1 */
	res = STC31xx_WriteByte(STC311x_REG_MODE, (res | STC311x_VMODE));
	if (res != OK)
		return res;

	return OK;
}


/*****************************************************************************
 * Function Name  : STC31xx_StopPowerSavingMode
 * Description    :  Stop the power saving mode
 * Input          : None
 * Return         : error status (OK, !OK)
 *****************************************************************************/
int STC31xx_StopPowerSavingMode(void)
{
	int res;

	/* Read the mode register*/
	res = STC31xx_ReadByte(STC311x_REG_MODE);

	/* Set the VMODE bit to 0 */
	res = STC31xx_WriteByte(STC311x_REG_MODE, (res & ~STC311x_VMODE));
	if (res != OK)
		return res;

	return OK;
}


/*****************************************************************************
 * Function Name  : STC31xx_AlarmSet
 * Description    :  Set the alarm function and set the alarm threshold
 * Input          : None
 * Return         : error status (OK, !OK)
 *****************************************************************************/
int STC31xx_AlarmSet(void)
{
	int res;

	/* Read the mode register*/
	res = STC31xx_ReadByte(STC311x_REG_MODE);

	/* Set the ALM_ENA bit to 1 */
	res = STC31xx_WriteByte(STC311x_REG_MODE, (res | STC311x_ALM_ENA));
	if (res != OK)
		return res;

	return OK;
}


/*****************************************************************************
 * Function Name  : STC31xx_AlarmStop
 * Description    :  Stop the alarm function
 * Input          : None
 * Return         : error status (OK, !OK)
 *****************************************************************************/
int STC31xx_AlarmStop(void)
{
	int res;

	/* Read the mode register*/
	res = STC31xx_ReadByte(STC311x_REG_MODE);

	/* Set the ALM_ENA bit to 0 */
	res = STC31xx_WriteByte(STC311x_REG_MODE, (res & ~STC311x_ALM_ENA));
	if (res != OK)
		return res;

	return OK;
}


/*****************************************************************************
 * Function Name  : STC31xx_AlarmGet
 * Description    : Return the ALM status
 * Input          : None
 * Return         : ALM status 00 : no alarm
 *                             01 : SOC alarm
 *                             10 : Voltage alarm
 *                             11 : SOC and voltage alarm
 *****************************************************************************/
int STC31xx_AlarmGet(void)
{
	int res;

	/* Read the mode register*/
	res = STC31xx_ReadByte(STC311x_REG_CTRL);
	res = res >> 5;

	return res;
}


/*****************************************************************************
 * Function Name  : STC31xx_AlarmClear
 * Description    :  Clear the alarm signal
 * Input          : None
 * Return         : error status (OK, !OK)
 *****************************************************************************/
int STC31xx_AlarmClear(void)
{
	int res;

	/* clear ALM bits*/
	res = STC31xx_WriteByte(STC311x_REG_CTRL, 0x01);
	if (res != OK)
		return res;

	return OK;
}


/*****************************************************************************
 * Function Name  : STC31xx_AlarmSetVoltageThreshold
 * Description    : Set the alarm threshold
 * Input          : int voltage threshold
 * Return         : error status (OK, !OK)
 *****************************************************************************/
int STC31xx_AlarmSetVoltageThreshold(int VoltThresh)
{
	int res;
	int value;

	BattData.Alm_Vbat = VoltThresh;

	value = ((BattData.Alm_Vbat << 9) / VoltageFactor); /* LSB=8*2.44mV */
	res = STC31xx_WriteByte(STC311x_REG_ALARM_VOLTAGE, value);
	if (res != OK)
		return res;

	return OK;
}




/*****************************************************************************
 * Function Name  : STC31xx_AlarmSetSOCThreshold
 * Description    : Set the alarm threshold
 * Input          : int voltage threshold
 * Return         : error status (OK, !OK)
 *****************************************************************************/
int STC31xx_AlarmSetSOCThreshold(int SOCThresh)
{
	int res;

	BattData.Alm_SOC = SOCThresh;
	res = STC31xx_WriteByte(STC311x_REG_ALARM_SOC, BattData.Alm_SOC*2);
	if (res != OK)
		return res;

	return OK;
}




/*****************************************************************************
 * Function Name  :	STC31xx_RelaxTmrSet
 * Description    :	Set the current threshold register to
			the passed value in mA
 * Input          :	int current threshold
 * Return         :	error status (OK, !OK)
 *****************************************************************************/
int STC31xx_RelaxTmrSet(int CurrentThreshold)
{
	int res, value;

	BattData.RelaxThreshold = CurrentThreshold;
	if (BattData.CurrentFactor != 0) {
		/*avoid divide by 0*/
		value = ((BattData.RelaxThreshold << 9) /
			 BattData.CurrentFactor);   /* LSB=8*5.88uV/Rsense */
		value = value & 0x7f;
		res = STC31xx_WriteByte(STC311x_REG_CURRENT_THRES, value);
		if (res != OK)
			return res;
	}

	return OK;
}

/*****************************************************************************
 * Function Name  : STC31xx_ForceCC
 * Description    :  Force the CC mode for CC eval
 * Input          :
 * Return         : error status (OK, !OK)
 *****************************************************************************/
int STC31xx_ForceCC(void)
{
	STC311x_ForceCC();

	return OK;
}

	
/* -------------------------------------------------------------- */

int stc311x_updata(void)
{
	struct stc311x_chip *chip = i2c_get_clientdata(sav_client);

	power_supply_changed(&chip->battery);

	return 0;
}

/* -------------------------------------------------------------- */

void stc311x_check_charger_state(struct stc311x_chip *chip)
{
	int rc;
	union power_supply_propval ret = {0,};
	struct power_supply *charger_psy = power_supply_get_by_name((char *)charger_name);

	if (!charger_psy) {
			pr_err("%s not registered\n", charger_name);
			return;
	} else {
		//get charging status
		ret.intval = 0;
		rc = charger_psy->get_property(charger_psy, POWER_SUPPLY_PROP_STATUS, &ret);
		if (rc) {
			pr_err("stc311x can't get smb23x register data\n");
			return;
		} else {
			g_last_status = chip->status;
			chip->status = ret.intval;
			if (chip->status == POWER_SUPPLY_STATUS_DISCHARGING)
				return;
		}

		ret.intval = 0;
		rc = charger_psy->get_property(charger_psy,
					POWER_SUPPLY_PROP_RESISTANCE, &ret);
		if (rc) {
			pr_err("stc311x can't get smb23x register data\n");
			return;
		}

		if (ret.intval > 0) {
			//if charger setting is reset to default, trigger charger to set new setting		
			if (ret.intval == SMB231_REG0_DEFAULT) {
				pr_err("smb23x register is default, call smb23x to set new setting \n");
				ret.intval = 1;	
				charger_psy->set_property(charger_psy, POWER_SUPPLY_PROP_STATUS, &ret);
			}
		}
	}
}

/*
* 1. If charge is fulled and charger exists, keep soc = 100%
* 2. If SOC = 0% and in charging, show SOC = 1%
* 3. The moment when charger is removed, if soc = 100% and drops, keep 100%. Else, update soc
* 4. When discharging, soc can only decrease
*/
void UI_soc_adjustment(struct stc311x_chip *chip)
{
	if (g_debug)
		pr_err("charging status = %d, UI soc = %d,  ST soc = %d \n", chip->status,  g_ui_soc, chip->batt_soc);
	if ((g_ui_soc == STC311x_BATTERY_FULL) && (chip->status != POWER_SUPPLY_STATUS_DISCHARGING))
		return;

	if ((chip->batt_soc == 0) && (chip->status == POWER_SUPPLY_STATUS_CHARGING) && (chip->batt_current > 0)) {
		g_ui_soc = 1;
		return;
	}

	if (chip->status == POWER_SUPPLY_STATUS_DISCHARGING) {
		//charger is plugged out
		if ((g_last_status != POWER_SUPPLY_STATUS_DISCHARGING) && (g_ui_soc == STC311x_BATTERY_FULL))
			return;

		//when discharging, the new SOC can only decrease
		if (chip->batt_soc > g_ui_soc)
			pr_err("Discharging, new soc = %d > original soc = %d, abort \n", chip->batt_soc, g_ui_soc);	
	}

	//normal SOC calculate
	switch (chip->status) {
	case POWER_SUPPLY_STATUS_CHARGING:
	case POWER_SUPPLY_STATUS_NOT_CHARGING:
	case POWER_SUPPLY_STATUS_FULL:
		if (chip->batt_soc > g_ui_soc)
			g_ui_soc++;
		//charger exist but current not enough
		else if (chip->batt_current <= 0) {
			if ((g_ui_soc - chip->batt_soc > 2 ) || (chip->batt_voltage < APP_MIN_VOLTAGE))
				g_ui_soc--;
		}
		break;
	case POWER_SUPPLY_STATUS_DISCHARGING:
		if ((chip->batt_soc < g_ui_soc ) || (chip->batt_voltage < APP_MIN_VOLTAGE))
			g_ui_soc--;
		break;
	case POWER_SUPPLY_STATUS_UNKNOWN:
	default:
		if (chip->batt_soc > g_ui_soc)
			g_ui_soc++;
		else
			g_ui_soc--;
		break;
	}
}

static void stc311x_work(struct work_struct *work)
{
	struct stc311x_chip *chip;
	struct GasGauge_DataTypeDef GasGaugeData;
	int res, Loop;

	chip = container_of(work, struct stc311x_chip, work.work);
	if (!wake_lock_active(&chip->wlock)) {
		wake_lock(&chip->wlock);
		pr_info("stc311x_wake_lock \n");
	}
	sav_client = chip->client;

	if (chip->pdata) {
		/* 1=Voltage mode, 0=mixed mode */
		GasGaugeData.Vmode = chip->pdata->Vmode;
		/* SOC alm level %*/
		GasGaugeData.Alm_SOC = chip->pdata->Alm_SOC;
		/* Vbat alm level mV*/
		GasGaugeData.Alm_Vbat = chip->pdata->Alm_Vbat;
		/* nominal CC_cnf */
		GasGaugeData.CC_cnf = chip->pdata->CC_cnf;
		/* nominal VM cnf */
		GasGaugeData.VM_cnf = chip->pdata->VM_cnf;
		/* nominal Rint */
		GasGaugeData.Rint = chip->pdata->Rint;
		/* nominal capacity in mAh */
		GasGaugeData.Cnom = chip->pdata->Cnom;
		/* sense resistor mOhms*/
		GasGaugeData.Rsense = chip->pdata->Rsense;
		/* current for relaxation in mA (< C/20) */
		GasGaugeData.RelaxCurrent = chip->pdata->RelaxCurrent;
		/* 1=Adaptive mode enabled, 0=Adaptive mode disabled */
		GasGaugeData.Adaptive = chip->pdata->Adaptive;
		/* capacity derating in 0.1%, for temp = 60, 40, 25, 10,   0,
		 * -10 °C */
		for (Loop = 0; Loop < NTEMP; Loop++) {
			GasGaugeData.CapDerating[Loop] =
				chip->pdata->CapDerating[Loop];
		}
		/* OCV curve adjustment */
		for (Loop = 0; Loop < OCVTAB_SIZE; Loop++) {
			GasGaugeData.OCVValue[Loop] =
				chip->pdata->OCVValue[Loop];
		}
		/* SOC TAB */
		for (Loop = 0; Loop < SOCTAB_SIZE; Loop++) {
			GasGaugeData.SOCValue[Loop] =
				chip->pdata->SOCValue[Loop];
		}
		/*External temperature fonction, return °C*/
		GasGaugeData.ExternalTemperature =
			chip->pdata->ExternalTemperature();
		/* 1=External temperature, 0=STC3117 temperature */
		GasGaugeData.ForceExternalTemperature =
			chip->pdata->ForceExternalTemperature;
	}

	if (g_debug) {
		pr_err(" ===  Before GasGauge_Task() === \n");
		stc311x_debug_info();
	}
	
	/* process gas gauge algorithm, returns results */
	res = GasGauge_Task(&GasGaugeData);
	if (res > 0) {
		/* results available */
		chip->batt_soc = (GasGaugeData.SOC+5)/10;
		chip->batt_voltage = GasGaugeData.Voltage;
		chip->batt_current = GasGaugeData.Current;
		chip->Temperature = GasGaugeData.Temperature;
	} else if (res == 0) {
		/* SOC and Voltage  available */
		chip->batt_soc = (GasGaugeData.SOC+5)/10;
		chip->batt_voltage = GasGaugeData.Voltage;
		chip->batt_current = 0;
		chip->Temperature = 250;
		pr_err("GasGauge_Task return (0) \n");
	} else if (res == -1) {
		pr_err("GasGauge_Task return (-1) \n");
		goto i2c_error;
		chip->batt_voltage = GasGaugeData.Voltage;
		chip->batt_soc = (GasGaugeData.SOC+5)/10;
		chip->Temperature = 250;
	}

	if (g_debug) {
		pr_err(" ===  After GasGauge_Task() === \n");
		stc311x_debug_info();
	}

	stc311x_check_charger_state(chip);

	if (g_force_SOC_update) {
		pr_info("gauge reset, update SOC: old: %d, new: %d \n", g_ui_soc, chip->batt_soc);
		g_ui_soc = chip->batt_soc;
		g_force_SOC_update = 0;
	} else if ((chip->batt_soc ^ g_ui_soc) || (chip->batt_soc == 0))
		UI_soc_adjustment(chip);

	//Control SOC between  0 - 100%
	if (g_ui_soc >= 100) 
		g_ui_soc = 100;
	if (g_ui_soc <= 0)
		g_ui_soc = 0;

	stc311x_updata();
	if (g_debug)
		pr_err("*** ST_SOC = %d, UI_SOC = %d, reg_soc = %d, voltage = %d mv, OCV = %d mv, current = %d mA, Temperature = %d, charging_status = %d *** \n", chip->batt_soc, g_ui_soc, g_reg_soc, chip->batt_voltage, g_ocv, chip->batt_current, chip->Temperature, chip->status);
	else
		pr_info("*** ST_SOC=%d, UI_SOC=%d, reg_soc=%d, voltage=%d, OCV=%d, charging_status=%d *** \n", chip->batt_soc, g_ui_soc, g_reg_soc, chip->batt_voltage, g_ocv, chip->status);

i2c_error:
	if (chip->batt_soc > STC311x_SOC_LOW_THRESHOLD)
		schedule_delayed_work(&chip->work, STC311x_DELAY);
	else if ((STC311x_SOC_CRITICAL_THRESHOLD <= chip->batt_soc) && (chip->batt_soc <= STC311x_SOC_LOW_THRESHOLD))
		schedule_delayed_work(&chip->work, STC311x_DELAY_LOW_BATT);
	else
		schedule_delayed_work(&chip->work, STC311x_DELAY_CRITICAL_BATT);

	if (wake_lock_active(&chip->wlock)) {
		wake_unlock(&chip->wlock);
		pr_info("stc311x_wake_unlock \n");
	}

}

static void stc311x_boot_up_work(struct work_struct *work)
{
	g_boot_phase = 0;
}

static enum power_supply_property stc311x_battery_props[] = {
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_RAW,
	POWER_SUPPLY_PROP_TEMP,
};

static int stc311x_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct stc311x_chip *chip;
	int ret, res, Loop, reg_mode, reg_ctrl;

	struct GasGauge_DataTypeDef GasGaugeData;

	/*First check the functionality supported by the host*/
	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_READ_I2C_BLOCK))
		return -EIO;
	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WRITE_I2C_BLOCK))
		return -EIO;

	/*OK. For now, we presume we have a valid client. We now create the
	  client structure*/
	//chip = kzalloc(sizeof(struct stc311x_chip), GFP_KERNEL);
	chip = devm_kzalloc(&client->dev, sizeof(struct stc311x_chip),
				GFP_KERNEL);
	if (!chip)
		return -ENOMEM;  /*Out of memory*/

	pr_err("\n\nstc311x probe started\n\n");
	g_debug = 0;
	g_boot_phase = 1;
	g_force_SOC_update = 0;
	g_dummy_soc = 101;

	/* The common I2C client data is placed right specific data. */
	chip->client = client;
	chip->pdata = &stc3117_data;
	chip->dev = &(client->dev); 
	i2c_set_clientdata(client, chip);

	pinctrl_pm_select_default_state(chip->dev);

	chip->battery.name		= "bms";
	chip->battery.type		= POWER_SUPPLY_TYPE_BMS;
	chip->battery.get_property	= stc311x_get_property;
	chip->battery.set_property	= stc311x_set_property;
	chip->battery.properties	= stc311x_battery_props;
	chip->battery.num_properties	= ARRAY_SIZE(stc311x_battery_props);
	chip->battery.property_is_writeable = stc311x_battery_is_writeable;

	if (chip->pdata && chip->pdata->power_supply_register)
		ret = chip->pdata->power_supply_register(&client->dev,
							 &chip->battery);
	else
		ret = power_supply_register(&client->dev, &chip->battery);
	if (ret) {
		dev_err(&client->dev, "failed: power supply register\n");

		kfree(chip);
		return ret;
	}

	dev_info(&client->dev, "power supply register,%d\n", ret);


	stc311x_get_version(client);

	/* init gas gauge system */
	sav_client = chip->client;
	if (chip->pdata) {
		/* 1=Voltage mode, 0=mixed mode */
		GasGaugeData.Vmode = chip->pdata->Vmode;
		/* SOC alm level %*/
		GasGaugeData.Alm_SOC = chip->pdata->Alm_SOC;
		/* Vbat alm level mV*/
		GasGaugeData.Alm_Vbat = chip->pdata->Alm_Vbat;
		/* nominal CC_cnf */
		GasGaugeData.CC_cnf = chip->pdata->CC_cnf;
		/* nominal VM cnf */
		GasGaugeData.VM_cnf = chip->pdata->VM_cnf;
		/* nominal Rint */
		GasGaugeData.Rint = chip->pdata->Rint;
		/* nominal capacity in mAh */
		GasGaugeData.Cnom = chip->pdata->Cnom;
		/* sense resistor mOhms*/
		GasGaugeData.Rsense = chip->pdata->Rsense;
		/* current for relaxation in mA (< C/20) */
		GasGaugeData.RelaxCurrent = chip->pdata->RelaxCurrent;
		/* 1=Adaptive mode enabled, 0=Adaptive mode disabled */
		GasGaugeData.Adaptive = chip->pdata->Adaptive;
		/* capacity derating in 0.1%, for temp
		 * = 60, 40, 25, 10,   0, -10 °C */
		for (Loop = 0; Loop < NTEMP; Loop++) {
			GasGaugeData.CapDerating[Loop] =
				chip->pdata->CapDerating[Loop];
		}
		/* OCV curve adjustment */
		for (Loop = 0; Loop < OCVTAB_SIZE; Loop++) {
			GasGaugeData.OCVValue[Loop] =
				chip->pdata->OCVValue[Loop];
		}
		/* SOC_TAB */
		for (Loop = 0; Loop < SOCTAB_SIZE; Loop++) {
			GasGaugeData.SOCValue[Loop] =
				chip->pdata->SOCValue[Loop];
		}
		/*External temperature fonction, return °C*/
		GasGaugeData.ExternalTemperature =
			chip->pdata->ExternalTemperature();
		/* 1=External temperature, 0=STC3117 temperature */
		GasGaugeData.ForceExternalTemperature =
			chip->pdata->ForceExternalTemperature;
	}

	if (g_debug) {
		pr_info(" ===  Before GasGauge_start() === \n");
		stc311x_debug_info();
	}

	//standby mode check
	reg_mode = 0;
	reg_ctrl = 0;
	g_standby_mode = 0;
	g_reg_soc = 0;
	reg_mode = STC31xx_ReadByte(STC311x_REG_MODE);
	reg_ctrl = STC31xx_ReadByte(STC311x_REG_CTRL);
	pr_info("mode = 0x%x, (reg_mode & GG_RUN_BIT) = %d  \n", reg_mode, (int)(reg_mode & GG_RUN_BIT));
	if(((reg_mode & GG_RUN_BIT) == 0) && ((reg_ctrl & PORDET_BIT) == 0)) {
		g_standby_mode = 1;
		pr_err("stc311x in standby mode \n");
	}
	
	GasGauge_Start(&GasGaugeData);
	msleep(200);

	if (g_debug) {
		pr_info(" ===  Before GasGauge_Task() === \n");
		stc311x_debug_info();
	}	
	
	/* process gas gauge algorithm, returns results */
	res = GasGauge_Task(&GasGaugeData);
	if (res > 0) {
		/* results available */
		chip->batt_soc = (GasGaugeData.SOC+5)/10;
		chip->batt_voltage = GasGaugeData.Voltage;
		chip->batt_current = GasGaugeData.Current;
		chip->Temperature = GasGaugeData.Temperature;
	} else if (res == 0) {
		/* SOC and Voltage  available */
		chip->batt_soc = (GasGaugeData.SOC+5)/10;
		chip->batt_voltage = GasGaugeData.Voltage;
		chip->batt_current = 0;
		chip->Temperature = 250;
		pr_err("GasGauge_Task return 0 \n");
	} else if (res == -1) {
		chip->batt_voltage = GasGaugeData.Voltage;
		chip->batt_soc = (GasGaugeData.SOC+5)/10;
		chip->Temperature = 250;
		pr_err("GasGauge_Task return (-1) \n");
	}
	g_ui_soc = chip->batt_soc;
	chip->status = POWER_SUPPLY_STATUS_UNKNOWN;
	g_last_status = POWER_SUPPLY_STATUS_UNKNOWN;
	wake_lock_init(&chip->wlock, WAKE_LOCK_SUSPEND, "stc311x");

	if (g_debug) {
		pr_info(" ===  After GasGauge_Task() === \n");
		stc311x_debug_info();
	}
	
	INIT_DEFERRABLE_WORK(&chip->work, stc311x_work);

	//if gauge need to do reset, start work after 5 seconds 
	if (res == -1)
		schedule_delayed_work(&chip->work, 500);
	else
		schedule_delayed_work(&chip->work, STC311x_DELAY);
	/*The specified delay depends of every platform and Linux kernel. It has
	 * to be checked physically during the driver integration*/
	/*a delay of about 5 seconds is correct but 30 seconds is enough compare
	 * to the battery SOC evolution speed*/

	//Init a 120 seconds timer for device boot up
	INIT_DEFERRABLE_WORK(&chip->boot_up_work, stc311x_boot_up_work);
	schedule_delayed_work(&chip->boot_up_work, STC311x_DELAY_BOOTUP);

	if (g_debug)
		pr_err("SOC = %d, reg_soc = %d, voltage = %d, OCV = %d, temp = %d \n", chip->batt_soc, g_reg_soc, chip->batt_voltage, g_ocv, chip->Temperature);
	else
		pr_info("SOC=%d, reg_soc=%d, voltage=%d, OCV=%d, temp=%d \n", chip->batt_soc, g_reg_soc, chip->batt_voltage, g_ocv, chip->Temperature);
	pr_info("stc311x FG successfully probed\n");
	return 0;
}

static int stc311x_remove(struct i2c_client *client)
{
	struct stc311x_chip *chip = i2c_get_clientdata(client);

	/* stop gas gauge system */
	sav_client = chip->client;
	GasGauge_Stop();

	if (chip->pdata && chip->pdata->power_supply_unregister)
		chip->pdata->power_supply_unregister(&chip->battery);
	else
		power_supply_unregister(&chip->battery);
	cancel_delayed_work(&chip->work);
	wake_lock_destroy(&chip->wlock);
	kfree(chip);

	return 0;
}

#ifdef CONFIG_PM

static int stc311x_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct stc311x_chip *chip = i2c_get_clientdata(client);

	pr_info("stc311x_suspend \n");
	cancel_delayed_work(&chip->work);
	return 0;
}

static int stc311x_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct stc311x_chip *chip = i2c_get_clientdata(client);

	pr_info("stc311x_resume \n");

	if (!wake_lock_active(&chip->wlock)) {
		wake_lock(&chip->wlock);
		pr_info("stc311x_wake_lock \n");
	}
	schedule_delayed_work(&chip->work, 0);
	return 0;
}
static SIMPLE_DEV_PM_OPS(stc311x_pm_ops, stc311x_suspend, stc311x_resume);
#define stc311x_PM_OPS (&stc311x_pm_ops)

#else

#define stc311x_suspend NULL
#define stc311x_resume NULL

#endif /* CONFIG_PM */


static struct of_device_id stc3117_match_table[] = {
	{ .compatible = "st,stc3117",},
	{ },
};

/* Every chip have a unique id */
static const struct i2c_device_id stc311x_id[] = {
	{"stc3117", 0},
	{ }
};

/* Every chip have a unique id and we need
   to register this ID using MODULE_DEVICE_TABLE*/
MODULE_DEVICE_TABLE(i2c, stc311x_id);

static struct i2c_driver stc311x_i2c_driver = {
	.driver	= {
		.name	= "stc3117",
		.owner		= THIS_MODULE,
		.pm     = stc311x_PM_OPS,
		.of_match_table = stc3117_match_table,
	},
	.probe		= stc311x_probe,
	.remove		= stc311x_remove,
	.id_table	= stc311x_id,
};
module_i2c_driver(stc311x_i2c_driver);

MODULE_AUTHOR("STMICROELECTRONICS");
MODULE_DESCRIPTION("STC311x Fuel Gauge");
MODULE_LICENSE("GPL");
