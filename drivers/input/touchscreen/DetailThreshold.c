/*
* Copyright © 2016 FocalTech Systems Co., Ltd.  All Rights Reserved.
*
* This program is free software; you may redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; version 2 of the License.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
* BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
* ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
* CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/bug.h>
#include "ini.h"
#include "DetailThreshold.h"
#include "test_lib.h"
#include "Global.h"

struct stCfg_MCap_DetailThreshold g_stCfg_MCap_DetailThreshold;
struct stCfg_SCap_DetailThreshold g_stCfg_SCap_DetailThreshold;


void set_max_channel_num(void)
{
	switch(g_ScreenSetParam.iSelectedIC>>4)
		{
		case IC_FT5822>>4:
			g_ScreenSetParam.iUsedMaxTxNum= TX_NUM_MAX;
			g_ScreenSetParam.iUsedMaxRxNum = RX_NUM_MAX;
			break;
		default:
			g_ScreenSetParam.iUsedMaxTxNum = 30;
			g_ScreenSetParam.iUsedMaxRxNum = 30;
			break;
		}

}
void OnInit_SCap_DetailThreshold(char *strIniFile)
{
	OnGetTestItemParam("RawDataTest_Max", strIniFile, 12500);
	memcpy(g_stCfg_SCap_DetailThreshold.RawDataTest_Max, g_stCfg_SCap_DetailThreshold.TempData, MAX_CHANNEL_NUM*sizeof(int));
	OnGetTestItemParam("RawDataTest_Min", strIniFile, 16500);
	memcpy(g_stCfg_SCap_DetailThreshold.RawDataTest_Min, g_stCfg_SCap_DetailThreshold.TempData, MAX_CHANNEL_NUM*sizeof(int));
	OnGetTestItemParam("CiTest_Max", strIniFile, 5);
	memcpy(g_stCfg_SCap_DetailThreshold.CiTest_Max, g_stCfg_SCap_DetailThreshold.TempData, MAX_CHANNEL_NUM*sizeof(int));
	OnGetTestItemParam("CiTest_Min", strIniFile, 250);
	memcpy(g_stCfg_SCap_DetailThreshold.CiTest_Min, g_stCfg_SCap_DetailThreshold.TempData, MAX_CHANNEL_NUM*sizeof(int));
	OnGetTestItemParam("DeltaCiTest_Base", strIniFile, 0);
	memcpy(g_stCfg_SCap_DetailThreshold.DeltaCiTest_Base, g_stCfg_SCap_DetailThreshold.TempData, MAX_CHANNEL_NUM*sizeof(int));
	OnGetTestItemParam("DeltaCiTest_AnotherBase1", strIniFile, 0);
	memcpy(g_stCfg_SCap_DetailThreshold.DeltaCiTest_AnotherBase1, g_stCfg_SCap_DetailThreshold.TempData, MAX_CHANNEL_NUM*sizeof(int));
	OnGetTestItemParam("DeltaCiTest_AnotherBase2", strIniFile, 0);
	memcpy(g_stCfg_SCap_DetailThreshold.DeltaCiTest_AnotherBase2, g_stCfg_SCap_DetailThreshold.TempData, MAX_CHANNEL_NUM*sizeof(int));
	OnGetTestItemParam("NoiseTest_Max", strIniFile, 20);
	memcpy(g_stCfg_SCap_DetailThreshold.NoiseTest_Max, g_stCfg_SCap_DetailThreshold.TempData, MAX_CHANNEL_NUM*sizeof(int));

	//OnGetTestItemParam("CiDeviation_Base", strIniFile);
	OnGetTestItemParam("CiDeviation_Base", strIniFile,0);
	memcpy(g_stCfg_SCap_DetailThreshold.CiDeviationTest_Base, g_stCfg_SCap_DetailThreshold.TempData, MAX_CHANNEL_NUM*sizeof(int));

	OnGetTestItemParam("DeltaCxTest_Sort", strIniFile, 1);
	memcpy(g_stCfg_SCap_DetailThreshold.DeltaCxTest_Sort, g_stCfg_SCap_DetailThreshold.TempData, MAX_CHANNEL_NUM*sizeof(int));

	OnGetTestItemParam("DeltaCxTest_Area", strIniFile, 1);
	memcpy(g_stCfg_SCap_DetailThreshold.DeltaCxTest_Area, g_stCfg_SCap_DetailThreshold.TempData, MAX_CHANNEL_NUM*sizeof(int));

	//6x36
	//OnGetTestItemParam("CbTest_Max", strIniFile);
	OnGetTestItemParam_CB("CBTest_Max", "CbTest_Max", strIniFile, 0);
	memcpy(g_stCfg_SCap_DetailThreshold.CbTest_Max, g_stCfg_SCap_DetailThreshold.TempData, MAX_CHANNEL_NUM*sizeof(int));

	//OnGetTestItemParam("CbTest_Min", strIniFile);
	OnGetTestItemParam_CB("CBTest_Min", "CbTest_Min", strIniFile, 0);
	memcpy(g_stCfg_SCap_DetailThreshold.CbTest_Min, g_stCfg_SCap_DetailThreshold.TempData, MAX_CHANNEL_NUM*sizeof(int));

	//OnGetTestItemParam("DeltaCbTest_Base", strIniFile);
	OnGetTestItemParam("DeltaCbTest_Base", strIniFile, 0);
	memcpy(g_stCfg_SCap_DetailThreshold.DeltaCbTest_Base, g_stCfg_SCap_DetailThreshold.TempData, MAX_CHANNEL_NUM*sizeof(int));

	//OnGetTestItemParam("DifferTest_Base", strIniFile);
	OnGetTestItemParam("DifferTest_Base", strIniFile, 0);
	memcpy(g_stCfg_SCap_DetailThreshold.DifferTest_Base, g_stCfg_SCap_DetailThreshold.TempData, MAX_CHANNEL_NUM*sizeof(int));

	//OnGetTestItemParam("CBDeviation_Base", strIniFile);
	OnGetTestItemParam("CBDeviation_Base", strIniFile, 0);
	memcpy(g_stCfg_SCap_DetailThreshold.CBDeviationTest_Base, g_stCfg_SCap_DetailThreshold.TempData, MAX_CHANNEL_NUM*sizeof(int));

	//OnGetTestItemParam("K1DifferTest_Base", strIniFile);
	OnGetTestItemParam("K1DifferTest_Base", strIniFile, 0);
	memcpy(g_stCfg_SCap_DetailThreshold.K1DifferTest_Base, g_stCfg_SCap_DetailThreshold.TempData, MAX_CHANNEL_NUM*sizeof(int));
}

void OnGetTestItemParam_CB(char *strBasicThreshold, char *strSpecialSet,
	char *strIniFile, int iDefautValue)
{
	char strValue[800];
	char str_tmp[128];
	int iValue = 0, dividerPos = 0;
	int index = 0;
	int i = 0, j = 0, k = 0;

	memset(g_stCfg_SCap_DetailThreshold.TempData, 0,
		sizeof(g_stCfg_SCap_DetailThreshold.TempData));
	snprintf(str_tmp, ARRAY_SIZE(str_tmp), "%d", iDefautValue);
	GetPrivateProfileString("Basic_Threshold",
		strBasicThreshold, str_tmp, strValue, strIniFile);

	iValue = atoi(strValue);
	for (i = 0; i < MAX_CHANNEL_NUM; i++)
		g_stCfg_SCap_DetailThreshold.TempData[i] = iValue;

	dividerPos = GetPrivateProfileString("SpecialSet",
		strSpecialSet, "", strValue, strIniFile);
	if (dividerPos > 0) {
		index = 0;
		k = 0;
		memset(str_tmp, 0x00, sizeof(str_tmp));
		for (j = 0; j < dividerPos; j++) {
			if (',' == strValue[j]) {
				g_stCfg_SCap_DetailThreshold.TempData[k] =
					(short)(atoi(str_tmp));
				index = 0;
				memset(str_tmp, 0x00, sizeof(str_tmp));
				k++;
			} else {
				if (' ' == strValue[j])
					continue;
				str_tmp[index] = strValue[j];
				index++;
			}
		}
	}
}

void OnGetTestItemParam(char *strItemName, char *strIniFile, int iDefautValue)
{
	//char str[430];
	char strValue[800];
	char str_tmp[128];
	int iValue = 0;
	int dividerPos = 0;
	int index = 0;
	int i = 0, j=0, k = 0;
	memset(g_stCfg_SCap_DetailThreshold.TempData, 0, sizeof(g_stCfg_SCap_DetailThreshold.TempData));
	sprintf(str_tmp, "%d", iDefautValue);
	GetPrivateProfileString("Basic_Threshold",
		strItemName, str_tmp, strValue, strIniFile);
	iValue = atoi(strValue);
	for(i = 0; i < MAX_CHANNEL_NUM; i++)
	{
		g_stCfg_SCap_DetailThreshold.TempData[i] = iValue;
	}

	dividerPos = GetPrivateProfileString("SpecialSet",
		strItemName, "", strValue, strIniFile);

	if (dividerPos > 0) {
		index = 0;
		k = 0;
		memset(str_tmp, 0x00, sizeof(str_tmp));
		for (j = 0; j < dividerPos; j++)
		{
			if (',' == strValue[j])
			{
				g_stCfg_SCap_DetailThreshold.TempData[k] = (short)(atoi(str_tmp));
				index = 0;
				memset(str_tmp, 0x00, sizeof(str_tmp));
				k++;
			} else {
				if(' ' == strValue[j])
					continue;
				str_tmp[index] = strValue[j];
				index++;
			}
		}
		//If (k+1) < MAX_CHANNEL_NUM, set Default Vaule to Other
		//for(i = k+1; i < MAX_CHANNEL_NUM; i++)
		//{
		//	g_stCfg_SCap_DetailThreshold.TempData[i] = iValue;
		//}
	}
}
void OnInit_MCap_DetailThreshold(char *strIniFile)
{
	set_max_channel_num();//set used TxRx

	OnInit_InvalidNode(strIniFile);
	OnInit_DThreshold_RawDataTest(strIniFile);
	OnInit_DThreshold_SCapRawDataTest(strIniFile);
	OnInit_DThreshold_SCapCbTest(strIniFile);

/*	OnInit_DThreshold_RxCrosstalkTest(strIniFile);
	OnInit_DThreshold_PanelDifferTest(strIniFile);*/
	OnInit_DThreshold_RxLinearityTest(strIniFile);
	OnInit_DThreshold_TxLinearityTest(strIniFile);
/*	OnInit_DThreshold_TxShortTest(strIniFile);


	OnInit_DThreshold_CMTest(strIniFile);

    OnInit_DThreshold_NoiseTest(strIniFile);

	//5422 SITO_RAWDATA_UNIFORMITY_TEST
	OnInit_DThreshold_SITORawdata_RxLinearityTest(strIniFile);
	OnInit_DThreshold_SITORawdata_TxLinearityTest(strIniFile);

	OnInit_DThreshold_SITO_RxLinearityTest(strIniFile);
	OnInit_DThreshold_SITO_TxLinearityTest(strIniFile);

	OnInit_DThreshold_UniformityRxLinearityTest(strIniFile);
	OnInit_DThreshold_UniformityTxLinearityTest(strIniFile);
*/
}
void OnInit_InvalidNode(char *strIniFile)
{

	char str[MAX_PATH] = {0},strTemp[MAX_PATH] = {0};
	int i = 0, j=0;
	//memset(str , 0x00, sizeof(str));
	/* memset(strTemp , 0x00, sizeof(strTemp)); */
	for(i = 0; i < g_ScreenSetParam.iUsedMaxTxNum; i++)
	{
		for(j = 0; j < g_ScreenSetParam.iUsedMaxRxNum; j++)
		{
			sprintf(strTemp, "InvalidNode[%d][%d]", (i+1), (j+1));

			GetPrivateProfileString("INVALID_NODE",strTemp,"1",str, strIniFile);
			if(atoi(str) == 0)
			{
				g_stCfg_MCap_DetailThreshold.InvalidNode[i][j] = 0;
			}
			else if( atoi( str ) == 2 )
			{
				g_stCfg_MCap_DetailThreshold.InvalidNode[i][j] = 2;
			}
			else
				g_stCfg_MCap_DetailThreshold.InvalidNode[i][j] = 1;
		}
	}

	for(i = 0; i < 2; i++)
	{
		for(j = 0; j < g_ScreenSetParam.iUsedMaxRxNum; j++)
		{
			sprintf(strTemp, "InvalidNodeS[%d][%d]", (i+1), (j+1));
			GetPrivateProfileString("INVALID_NODES",strTemp,"1",str, strIniFile);
			if(atoi(str) == 0)
			{
				g_stCfg_MCap_DetailThreshold.InvalidNode_SC[i][j] = 0;
			}
			else if( atoi( str ) == 2 )
			{
				g_stCfg_MCap_DetailThreshold.InvalidNode_SC[i][j] = 2;
			}
			else
				g_stCfg_MCap_DetailThreshold.InvalidNode_SC[i][j] = 1;
		}

	}
}

void OnInit_DThreshold_RawDataTest(char *strIniFile)
{
	char str[128], strTemp[MAX_PATH],strValue[MAX_PATH];
	int MaxValue, MinValue;
	int  dividerPos = 0;
	char str_tmp[128];
	int index = 0;
	int  k = 0, i = 0, j = 0;
	////////////////////////////RawData Test
	GetPrivateProfileString( "Basic_Threshold","RawDataTest_Max","10000",str, strIniFile);
	MaxValue = atoi(str);

	for(i = 0; i < g_ScreenSetParam.iUsedMaxTxNum; i++)
	{
		for(j = 0; j < g_ScreenSetParam.iUsedMaxRxNum; j++)
		{
			g_stCfg_MCap_DetailThreshold.RawDataTest_Max[i][j] = MaxValue;
		}
	}

	for(i = 0; i < g_ScreenSetParam.iUsedMaxTxNum; i++)
	{
		sprintf(str, "RawData_Max_Tx%d", (i + 1));
		//printk("%s \n", str);
		dividerPos = GetPrivateProfileString( "SpecialSet", str, "111",strTemp, strIniFile);
		//printk("GetPrivateProfileString = %d \n", dividerPos);
		sprintf(strValue, "%s",strTemp);
		if(0 == dividerPos) continue;
		index = 0;
		k = 0;
		memset(str_tmp, 0, sizeof(str_tmp));
		for (j = 0; j < dividerPos; j++)
		{
			if (',' == strValue[j])
			{
				g_stCfg_MCap_DetailThreshold.RawDataTest_Max[i][k] = (short)(atoi(str_tmp));
				index = 0;
				memset(str_tmp, 0x00, sizeof(str_tmp));
				k++;
			} else {
				if(' ' == strValue[j])
					continue;
				str_tmp[index] = strValue[j];
				index++;
			}
		}

	}

	GetPrivateProfileString("Basic_Threshold","RawDataTest_Min","7000",str, strIniFile);
	MinValue = atoi(str);

	for(i = 0; i < g_ScreenSetParam.iUsedMaxTxNum; i++)
	{
		for(j = 0; j < g_ScreenSetParam.iUsedMaxRxNum; j++)
		{
			g_stCfg_MCap_DetailThreshold.RawDataTest_Min[i][j] = MinValue;
		}
	}
	for(i = 0; i < g_ScreenSetParam.iUsedMaxTxNum; i++)
	{
		sprintf(str, "RawData_Min_Tx%d", (i + 1));
		dividerPos = GetPrivateProfileString( "SpecialSet", str, "NULL",strTemp, strIniFile);
		sprintf(strValue, "%s",strTemp);
		if(0 == dividerPos) continue;
		index = 0;
		k = 0;
		memset(str_tmp, 0x00, sizeof(str_tmp));
		for (j = 0; j < dividerPos; j++)
		{
			if (',' == strValue[j])
			{
				g_stCfg_MCap_DetailThreshold.RawDataTest_Min[i][k] = (short)(atoi(str_tmp));
				index = 0;
				memset(str_tmp, 0x00, sizeof(str_tmp));
				k++;
			} else {
				if (' ' == strValue[j])
					continue;
				str_tmp[index] = strValue[j];
				index++;
			}
		}
	}

	//RawData Test Low
	GetPrivateProfileString( "Basic_Threshold","RawDataTest_Low_Max","15000",str, strIniFile);
	MaxValue = atoi(str);

	for(i = 0; i < g_ScreenSetParam.iUsedMaxTxNum; i++)
	{
		for(j = 0; j < g_ScreenSetParam.iUsedMaxRxNum; j++)
		{
			g_stCfg_MCap_DetailThreshold.RawDataTest_Low_Max[i][j] = MaxValue;
		}
	}
	for(i = 0; i < g_ScreenSetParam.iUsedMaxTxNum; i++)
	{
		sprintf(str, "RawData_Max_Low_Tx%d", (i + 1));
		dividerPos = GetPrivateProfileString( "SpecialSet", str, "NULL",strTemp, strIniFile);
		sprintf(strValue, "%s",strTemp);
		if(0 == dividerPos) continue;
		index = 0;
		k = 0;
		memset(str_tmp, 0x00, sizeof(str_tmp));
		for (j = 0; j < dividerPos; j++)
		{
			if (',' == strValue[j])
			{
				g_stCfg_MCap_DetailThreshold.RawDataTest_Low_Max[i][k] = (short)(atoi(str_tmp));
				index = 0;
				memset(str_tmp, 0x00, sizeof(str_tmp));
				k++;
			} else {
				if (' ' == strValue[j])
					continue;
				str_tmp[index] = strValue[j];
				index++;
			}
		}
	}

	GetPrivateProfileString("Basic_Threshold","RawDataTest_Low_Min","3000",str, strIniFile);
	MinValue = atoi(str);

	for(i = 0; i < g_ScreenSetParam.iUsedMaxTxNum; i++)
	{
		for(j = 0; j < g_ScreenSetParam.iUsedMaxRxNum; j++)
		{
			g_stCfg_MCap_DetailThreshold.RawDataTest_Low_Min[i][j] = MinValue;
		}
	}
	for(i = 0; i < g_ScreenSetParam.iUsedMaxTxNum; i++)
	{
		sprintf(str, "RawData_Min_Low_Tx%d", (i + 1));
		dividerPos = GetPrivateProfileString( "SpecialSet", str, "NULL",strTemp, strIniFile);
		sprintf(strValue, "%s",strTemp);
		if(0 == dividerPos) continue;
		index = 0;
		k = 0;
		memset(str_tmp, 0x00, sizeof(str_tmp));
		for (j = 0; j < dividerPos; j++)
		{
			if (',' == strValue[j])
			{
				g_stCfg_MCap_DetailThreshold.RawDataTest_Low_Min[i][k] = (short)(atoi(str_tmp));
				index = 0;
				memset(str_tmp, 0x00, sizeof(str_tmp));
				k++;
			} else {
				if(' ' == strValue[j])
					continue;
				str_tmp[index] = strValue[j];
				index++;
			}
		}
	}

	//RawData Test High
	GetPrivateProfileString( "Basic_Threshold","RawDataTest_High_Max","15000",str, strIniFile);
	MaxValue = atoi(str);

	for(i = 0; i < g_ScreenSetParam.iUsedMaxTxNum; i++)
	{
		for(j = 0; j < g_ScreenSetParam.iUsedMaxRxNum; j++)
		{
			g_stCfg_MCap_DetailThreshold.RawDataTest_High_Max[i][j] = MaxValue;
		}
	}
	for(i = 0; i < g_ScreenSetParam.iUsedMaxTxNum; i++)
	{
		sprintf(str, "RawData_Max_High_Tx%d", (i + 1));
		dividerPos = GetPrivateProfileString( "SpecialSet", str, "NULL",strTemp, strIniFile);
		sprintf(strValue, "%s",strTemp);
		if(0 == dividerPos) continue;
		index = 0;
		k = 0;
		memset(str_tmp, 0x00, sizeof(str_tmp));
		for (j = 0; j < dividerPos; j++)
		{
			if (',' == strValue[j])
			{
				g_stCfg_MCap_DetailThreshold.RawDataTest_High_Max[i][k] = (short)(atoi(str_tmp));
				index = 0;
				memset(str_tmp, 0x00, sizeof(str_tmp));
				k++;
			} else {
				if(' ' == strValue[j])
					continue;
				str_tmp[index] = strValue[j];
				index++;
			}
		}
	}

	GetPrivateProfileString("Basic_Threshold","RawDataTest_High_Min","3000",str, strIniFile);
	MinValue = atoi(str);

	for(i = 0; i < g_ScreenSetParam.iUsedMaxTxNum; i++)
	{
		for(j = 0; j < g_ScreenSetParam.iUsedMaxRxNum; j++)
		{
			g_stCfg_MCap_DetailThreshold.RawDataTest_High_Min[i][j] = MinValue;
		}
	}
	for(i = 0; i < g_ScreenSetParam.iUsedMaxTxNum; i++)
	{
		sprintf(str, "RawData_Min_High_Tx%d", (i + 1));
		dividerPos = GetPrivateProfileString( "SpecialSet", str, "NULL",strTemp, strIniFile);
		sprintf(strValue, "%s",strTemp);
		if(0 == dividerPos) continue;
		index = 0;
		k = 0;
		memset(str_tmp, 0x00, sizeof(str_tmp));
		for (j = 0; j < dividerPos; j++)
		{
			if (',' == strValue[j])
			{
				g_stCfg_MCap_DetailThreshold.RawDataTest_High_Min[i][k] = (short)(atoi(str_tmp));
				index = 0;
				memset(str_tmp, 0x00, sizeof(str_tmp));
				k++;
			} else {
				if(' ' == strValue[j])
					continue;
				str_tmp[index] = strValue[j];
				index++;
			}
		}
	}
/*
	for(i = 0; i < g_ScreenSetParam.iUsedMaxTxNum; i++)
	{
		for(j = 0; j < g_ScreenSetParam.iUsedMaxRxNum; j++)
		{
			g_stCfg_MCap_DetailThreshold.TxShortAdvance[i][j] = 0;
		}
	}
	for(i = 0; i < g_ScreenSetParam.iUsedMaxTxNum; i++)
	{
		sprintf(str, "TxShortAdvanceThreshold_Tx%d", (i + 1));
		dividerPos = GetPrivateProfileString( "SpecialSet", str, "NULL",strTemp, strIniFile);
		sprintf(strValue, "%s",strTemp);
		if(0 == dividerPos) continue;
		index = 0;
		k = 0;
		memset(str_tmp, 0x00, sizeof(str_tmp));
		for(j=0; j<dividerPos; j++)
		{
			if(',' == strValue[j])
			{
				g_stCfg_MCap_DetailThreshold.TxShortAdvance[i][k] = (short)(atoi(str_tmp));
				index = 0;
				memset(str_tmp, 0x00, sizeof(str_tmp));
				k++;
			}
			else
			{
				if(' ' == strValue[j])
					continue;
				str_tmp[index] = strValue[j];
				index++;
			}
		}
	}*/
}
void OnInit_DThreshold_SCapRawDataTest(char *strIniFile)
{
	char str[128], strTemp[MAX_PATH],strValue[MAX_PATH];
	int MaxValue, MinValue;
	int dividerPos = 0;
	char str_tmp[128];
	int index = 0;
	int  k = 0, i = 0, j = 0;

	//////////////////OFF
	GetPrivateProfileString("Basic_Threshold","SCapRawDataTest_OFF_Min","150",str,strIniFile);
	MinValue = atoi(str);
	GetPrivateProfileString("Basic_Threshold","SCapRawDataTest_OFF_Max","1000",str,strIniFile);
	MaxValue = atoi(str);

	///Max
	for(i = 0; i < 2; i++)
	{
		for(j = 0; j < g_ScreenSetParam.iUsedMaxRxNum; j++)
		{
			g_stCfg_MCap_DetailThreshold.SCapRawDataTest_OFF_Max[i][j] = MaxValue;
		}
	}
	for(i = 0; i < 2; i++)
	{
		sprintf(str, "ScapRawData_OFF_Max_%d", (i + 1));
		dividerPos = GetPrivateProfileString( "SpecialSet", str, "NULL",strTemp, strIniFile);
		sprintf(strValue, "%s",strTemp);
		if(0 == dividerPos) continue;
		index = 0;
		k = 0;
		memset(str_tmp, 0x00, sizeof(str_tmp));
		for (j = 0; j < dividerPos; j++)
		{
			if (',' == strValue[j])
			{
				g_stCfg_MCap_DetailThreshold.SCapRawDataTest_OFF_Max[i][k] = (short)(atoi(str_tmp));
				index = 0;
				memset(str_tmp, 0x00, sizeof(str_tmp));
				k++;
			} else {
				if(' ' == strValue[j])
					continue;
				str_tmp[index] = strValue[j];
				index++;
			}
		}
	}
	////Min
	for(i = 0; i < 2; i++)
	{
		for(j = 0; j < g_ScreenSetParam.iUsedMaxRxNum; j++)
		{
			g_stCfg_MCap_DetailThreshold.SCapRawDataTest_OFF_Min[i][j] = MinValue;
		}
	}
	for(i = 0; i < 2; i++)
	{
		sprintf(str, "ScapRawData_OFF_Min_%d", (i + 1));
		dividerPos = GetPrivateProfileString( "SpecialSet", str, "NULL",strTemp, strIniFile);
		sprintf(strValue, "%s",strTemp);
		if(0 == dividerPos) continue;
		index = 0;
		k = 0;
		memset(str_tmp, 0x00, sizeof(str_tmp));
		for (j = 0; j < dividerPos; j++)
		{
			if (',' == strValue[j])
			{
				g_stCfg_MCap_DetailThreshold.SCapRawDataTest_OFF_Min[i][k] = (short)(atoi(str_tmp));
				index = 0;
				memset(str_tmp, 0x00, sizeof(str_tmp));
				k++;
			} else {
				if (' ' == strValue[j])
					continue;
				str_tmp[index] = strValue[j];
				index++;
			}
		}
	}

	//////////////////ON
	GetPrivateProfileString("Basic_Threshold","SCapRawDataTest_ON_Min","150",str,strIniFile);
	MinValue = atoi(str);
	GetPrivateProfileString("Basic_Threshold","SCapRawDataTest_ON_Max","1000",str,strIniFile);
	MaxValue = atoi(str);

	///Max
	for(i = 0; i < 2; i++)
	{
		for(j = 0; j < g_ScreenSetParam.iUsedMaxRxNum; j++)
		{
			g_stCfg_MCap_DetailThreshold.SCapRawDataTest_ON_Max[i][j] = MaxValue;
		}
	}
	for(i = 0; i < 2; i++)
	{
		sprintf(str, "ScapRawData_ON_Max_%d", (i + 1));
		dividerPos = GetPrivateProfileString( "SpecialSet", str, "NULL",strTemp, strIniFile);
		sprintf(strValue, "%s",strTemp);
		if(0 == dividerPos) continue;
		index = 0;
		k = 0;
		memset(str_tmp, 0x00, sizeof(str_tmp));
		for (j = 0; j < dividerPos; j++)
		{
			if (',' == strValue[j])
			{
				g_stCfg_MCap_DetailThreshold.SCapRawDataTest_ON_Max[i][k] = (short)(atoi(str_tmp));
				index = 0;
				memset(str_tmp, 0x00, sizeof(str_tmp));
				k++;
			} else {
				if(' ' == strValue[j])
					continue;
				str_tmp[index] = strValue[j];
				index++;
			}
		}
	}
	////Min
	for(i = 0; i < 2; i++)
	{
		for(j = 0; j < g_ScreenSetParam.iUsedMaxRxNum; j++)
		{
			g_stCfg_MCap_DetailThreshold.SCapRawDataTest_ON_Min[i][j] = MinValue;
		}
	}
	for(i = 0; i < 2; i++)
	{
		sprintf(str, "ScapRawData_ON_Min_%d", (i + 1));
		dividerPos = GetPrivateProfileString( "SpecialSet", str, "NULL",strTemp, strIniFile);
		sprintf(strValue, "%s",strTemp);
		if (0 == dividerPos)
			continue;

		index = 0;
		k = 0;
		memset(str_tmp, 0x00, sizeof(str_tmp));
		for (j = 0; j < dividerPos; j++)
		{
			if (',' == strValue[j])
			{
				g_stCfg_MCap_DetailThreshold.SCapRawDataTest_ON_Min[i][k] = (short)(atoi(str_tmp));
				index = 0;
				memset(str_tmp, 0x00, sizeof(str_tmp));
				k++;
			} else {
				if(' ' == strValue[j])
					continue;
				str_tmp[index] = strValue[j];
				index++;
			}
		}
	}
}
void OnInit_DThreshold_SCapCbTest(char *strIniFile)
{
	char str[128], strTemp[MAX_PATH],strValue[MAX_PATH];
	int MaxValue, MinValue;
	int dividerPos = 0;
	char str_tmp[128];
	int index = 0;
	int  k = 0, i = 0, j = 0;

	GetPrivateProfileString("Basic_Threshold","SCapCbTest_ON_Min","0",str,strIniFile);
	MinValue = atoi(str);
	GetPrivateProfileString("Basic_Threshold","SCapCbTest_ON_Max","240",str,strIniFile);
	MaxValue = atoi(str);
	///Max
	for(i = 0; i < 2; i++)
	{
		for(j = 0; j < g_ScreenSetParam.iUsedMaxRxNum; j++)
		{
			g_stCfg_MCap_DetailThreshold.SCapCbTest_ON_Max[i][j] = MaxValue;
		}
	}
	for(i = 0; i < 2; i++)
	{
		sprintf(str, "ScapCB_ON_Max_%d", (i + 1));
		dividerPos = GetPrivateProfileString( "SpecialSet", str, "NULL",strTemp, strIniFile);
		sprintf(strValue, "%s",strTemp);
		if(0 == dividerPos) continue;
		index = 0;
		k = 0;
		memset(str_tmp, 0x00, sizeof(str_tmp));
		for (j = 0; j < dividerPos; j++)
		{
			if (',' == strValue[j])
			{
				g_stCfg_MCap_DetailThreshold.SCapCbTest_ON_Max[i][k] = (short)(atoi(str_tmp));
				index = 0;
				memset(str_tmp, 0x00, sizeof(str_tmp));
				k++;
			} else {
				if (' ' == strValue[j])
					continue;
				str_tmp[index] = strValue[j];
				index++;
			}
		}
	}
	////Min
	for(i = 0; i < 2; i++)
	{
		for(j = 0; j < g_ScreenSetParam.iUsedMaxRxNum; j++)
		{
			g_stCfg_MCap_DetailThreshold.SCapCbTest_ON_Min[i][j] = MinValue;
		}
	}
	for(i = 0; i < 2; i++)
	{
		sprintf(str, "ScapCB_ON_Min_%d", (i + 1));
		dividerPos = GetPrivateProfileString( "SpecialSet", str, "NULL",strTemp, strIniFile);
		sprintf(strValue, "%s",strTemp);
		if(0 == dividerPos) continue;
		index = 0;
		k = 0;
		memset(str_tmp, 0x00, sizeof(str_tmp));
		for (j = 0; j < dividerPos; j++)
		{
			if (',' == strValue[j])
			{
				g_stCfg_MCap_DetailThreshold.SCapCbTest_ON_Min[i][k] = (short)(atoi(str_tmp));
				index = 0;
				memset(str_tmp, 0x00, sizeof(str_tmp));
				k++;
			} else {
				if(' ' == strValue[j])
					continue;
				str_tmp[index] = strValue[j];
				index++;
			}
		}
	}
	GetPrivateProfileString("Basic_Threshold","SCapCbTest_OFF_Min","0",str,strIniFile);
	MinValue = atoi(str);
	GetPrivateProfileString("Basic_Threshold","SCapCbTest_OFF_Max","240",str,strIniFile);
	MaxValue = atoi(str);
	///Max
	for(i = 0; i < 2; i++)
	{
		for(j = 0; j < g_ScreenSetParam.iUsedMaxRxNum; j++)
		{
			g_stCfg_MCap_DetailThreshold.SCapCbTest_OFF_Max[i][j] = MaxValue;
		}
	}
	for(i = 0; i < 2; i++)
	{
		sprintf(str, "ScapCB_OFF_Max_%d", (i + 1));
		dividerPos = GetPrivateProfileString( "SpecialSet", str, "NULL",strTemp, strIniFile);
		sprintf(strValue, "%s",strTemp);
		if(0 == dividerPos) continue;
		index = 0;
		k = 0;
		memset(str_tmp, 0x00, sizeof(str_tmp));
		for (j = 0; j < dividerPos; j++)
		{
			if (',' == strValue[j])
			{
				g_stCfg_MCap_DetailThreshold.SCapCbTest_OFF_Max[i][k] = (short)(atoi(str_tmp));
				index = 0;
				memset(str_tmp, 0x00, sizeof(str_tmp));
				k++;
			} else {
				if(' ' == strValue[j])
					continue;
				str_tmp[index] = strValue[j];
				index++;
			}
		}
	}
	////Min
	for(i = 0; i < 2; i++)
	{
		for(j = 0; j < g_ScreenSetParam.iUsedMaxRxNum; j++)
		{
			g_stCfg_MCap_DetailThreshold.SCapCbTest_OFF_Min[i][j] = MinValue;
		}
	}
	for(i = 0; i < 2; i++)
	{
		sprintf(str, "ScapCB_OFF_Min_%d", (i + 1));
		dividerPos = GetPrivateProfileString( "SpecialSet", str, "NULL",strTemp, strIniFile);
		sprintf(strValue, "%s",strTemp);
		if(0 == dividerPos) continue;
		index = 0;
		k = 0;
		memset(str_tmp, 0x00, sizeof(str_tmp));
		for (j = 0; j < dividerPos; j++)
		{
			if (',' == strValue[j])
			{
				g_stCfg_MCap_DetailThreshold.SCapCbTest_OFF_Min[i][k] = (short)(atoi(str_tmp));
				index = 0;
				memset(str_tmp, 0x00, sizeof(str_tmp));
				k++;
			} else {
				if(' ' == strValue[j])
					continue;
				str_tmp[index] = strValue[j];
				index++;
			}
		}
	}
}

void OnInit_DThreshold_RxLinearityTest(char *strIniFile)
{
	char str[128], strTemp[MAX_PATH],strValue[MAX_PATH];
	int MaxValue = 0, dividerPos = 0;
	char str_tmp[128];
	int index = 0;
	int  k = 0, i = 0, j = 0;
	////////////////////////////Rx_Linearity Test
	GetPrivateProfileString( "Basic_Threshold","RxLinearityTest_Max", "50",str, strIniFile);
	MaxValue = atoi(str);

	for(i = 0; i < g_ScreenSetParam.iUsedMaxTxNum; i++)
	{
		for(j = 0; j < g_ScreenSetParam.iUsedMaxRxNum; j++)
			g_stCfg_MCap_DetailThreshold.RxLinearityTest_Max[i][j] = MaxValue;
	}

	for(i = 0; i < g_ScreenSetParam.iUsedMaxTxNum; i++)
	{
		sprintf(str, "Rx_Linearity_Max_Tx%d", (i + 1));
		//printk("%s \n", str);
		dividerPos = GetPrivateProfileString( "SpecialSet", str, "111",strTemp, strIniFile);
		//printk("GetPrivateProfileString = %d \n", dividerPos);
		sprintf(strValue, "%s",strTemp);
		if(0 == dividerPos) continue;
		index = 0;
		k = 0;
		memset(str_tmp, 0, sizeof(str_tmp));
		for (j = 0; j < dividerPos; j++)
		{
			if (',' == strValue[j])
			{
				g_stCfg_MCap_DetailThreshold.RxLinearityTest_Max[i][k] = (short)(atoi(str_tmp));
				index = 0;
				memset(str_tmp, 0x00, sizeof(str_tmp));
				k++;
			} else {
				if(' ' == strValue[j])
					continue;
				str_tmp[index] = strValue[j];
				index++;
			}
		}

	}
}

void OnInit_DThreshold_TxLinearityTest(char *strIniFile)
{
	char str[128], strTemp[MAX_PATH],strValue[MAX_PATH];
	int MaxValue = 0, dividerPos = 0;
	char str_tmp[128];
	int index = 0;
	int  k = 0, i = 0, j = 0;
	////////////////////////////Tx_Linearity Test
	GetPrivateProfileString( "Basic_Threshold","TxLinearityTest_Max", "50",str, strIniFile);
	MaxValue = atoi(str);

	for(i = 0; i < g_ScreenSetParam.iUsedMaxTxNum; i++)
	{
		for(j = 0; j < g_ScreenSetParam.iUsedMaxRxNum; j++)
		{
			g_stCfg_MCap_DetailThreshold.TxLinearityTest_Max[i][j] = MaxValue;
		}
	}

	for(i = 0; i < g_ScreenSetParam.iUsedMaxTxNum; i++)
	{
		sprintf(str, "Tx_Linearity_Max_Tx%d", (i + 1));
		//printk("%s \n", str);
		dividerPos = GetPrivateProfileString( "SpecialSet", str, "111",strTemp, strIniFile);
		//printk("GetPrivateProfileString = %d \n", dividerPos);
		sprintf(strValue, "%s",strTemp);
		if(0 == dividerPos) continue;
		index = 0;
		k = 0;
		memset(str_tmp, 0, sizeof(str_tmp));
		for (j = 0; j < dividerPos; j++)
		{
			if (',' == strValue[j])
			{
				g_stCfg_MCap_DetailThreshold.TxLinearityTest_Max[i][k] = (short)(atoi(str_tmp));
				index = 0;
				memset(str_tmp, 0x00, sizeof(str_tmp));
				k++;
			} else {
				if (' ' == strValue[j])
					continue;
				str_tmp[index] = strValue[j];
				index++;
			}
		}

	}
}
