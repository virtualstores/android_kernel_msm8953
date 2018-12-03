/*
 * Copyright (C) 2006-2017 ILITEK TECHNOLOGY CORP.
 *
 * Description: ILITEK I2C touchscreen driver for linux platform.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Author: Dicky Chiang
 * Maintain: Luca Hsu, Tigers Huang
 */

#include "MpCommon.h"

extern u8 SLAVE_I2C_ID_DBBUS; //0x62 // for MSG21XX/MSG21XXA/MSG26XXM/MSG28XX
//u8 SLAVE_I2C_ID_DBBUS = (0xB2>>1); //0x59 // for MSG22XX
extern u8 SLAVE_I2C_ID_DWI2C; //0x26

void RegGet16BitByteValueBuf(u16 nAddr, u8 *pBuf, u16 nLen)
{
	u16 i;
    u8 tx_data[3] = {0x10, (nAddr >> 8) & 0xFF, nAddr & 0xFF};
    u8 *rx_data = (u8 *)kcalloc(nLen, sizeof(u8), GFP_KERNEL);

    IicWriteData(SLAVE_I2C_ID_DBBUS, &tx_data[0], 3);
    IicReadData(SLAVE_I2C_ID_DBBUS, rx_data, nLen);

    for (i = 0; i < nLen; i++)
    	pBuf[i] = rx_data[i];
    kfree(rx_data);
}

char szStrBuf[1024];
char szStrTmp[10];

void DebugShowArray2(void *pBuf, u16 nLen, int nDataType, int nCarry, int nChangeLine)
{
    u8 * pU8Buf = NULL;
    s8 * pS8Buf = NULL;
    u16 * pU16Buf = NULL;
    s16 * pS16Buf = NULL;
    u32 * pU32Buf = NULL;
    s32 * pS32Buf = NULL;

    int i;

    printk(" %s \n", __func__);


    if(nDataType == 8)
        pU8Buf = (u8 *)pBuf;
    else if(nDataType == -8)
        pS8Buf = (s8 *)pBuf;
    else if(nDataType == 16)
        pU16Buf = (u16 *)pBuf;
    else if(nDataType == -16)
        pS16Buf = (s16 *)pBuf;
    else if(nDataType == 32)
        pU32Buf = (u32 *)pBuf;
    else if(nDataType == -32)
        pS32Buf = (s32 *)pBuf;

    for(i=0; i < nLen; i++)
    {
        if(nCarry == 16)
        {
            if(nDataType == 8)
            	sprintf(szStrTmp, "%02X ", pU8Buf[i]);
            else if(nDataType == -8)
            	sprintf(szStrTmp, "%02X ", pS8Buf[i]);
            else if(nDataType == 16)
            	sprintf(szStrTmp, "%04X ", pU16Buf[i]);
            else if(nDataType == -16)
            	sprintf(szStrTmp, "%04X ", pS16Buf[i]);
            else if(nDataType == 32)
            	sprintf(szStrTmp, "%08X ", pU32Buf[i]);
            else if(nDataType == -32)
            	sprintf(szStrTmp, "%08X ", pS32Buf[i]);
        }
        else if(nCarry == 10)
        {
            if(nDataType == 8)
            	sprintf(szStrTmp, "%6d ", pU8Buf[i]);
            else if(nDataType == -8)
            	sprintf(szStrTmp, "%6d ", pS8Buf[i]);
            else if(nDataType == 16)
            	sprintf(szStrTmp, "%6d ", pU16Buf[i]);
            else if(nDataType == -16)
            	sprintf(szStrTmp, "%6d ", pS16Buf[i]);
            else if(nDataType == 32)
            	sprintf(szStrTmp, "%6d ", pU32Buf[i]);
            else if(nDataType == -32)
            	sprintf(szStrTmp, "%6d ", pS32Buf[i]);
        }

        strcat(szStrBuf, szStrTmp);
        memset(szStrTmp, 0, 10);         
        if(i%nChangeLine==nChangeLine-1){
        	printk("%s\n", szStrBuf);
        	memset(szStrBuf, 0, 1024);
        }
    }
    printk("\n");
}