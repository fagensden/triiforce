/*******************************************************************************
 * tools.c
 *
 * Copyright (c) 2009 The Lemon Man
 * Copyright (c) 2009 Nicksasa
 * Copyright (c) 2009 WiiPower
 *
 * Distributed under the terms of the GNU General Public License (v2)
 * See http://www.gnu.org/licenses/gpl-2.0.txt for more info.
 *
 * Description:
 * -----------
 *
 ******************************************************************************/

#include <gccore.h>
#include <malloc.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <wiiuse/wpad.h>

#include "tools.h"

static bool silent = false;

void set_silent(bool value)
{
	silent = value;
}

bool get_silent()
{
	return silent;
}

void Print(const char *text, ...)
{
	if (!silent)
	{
		char Buffer[1024];
		va_list args;

		va_start(args, text);
		vsprintf(Buffer, text, args);

		va_end(args);
		
		printf(Buffer);
	}
}

void printheadline()
{
	int rows, cols;
	CON_GetMetrics(&cols, &rows);

	Print("TriiForce r72");
	
	char buf[64];
	sprintf(buf, "IOS%u (Rev %u)\n", IOS_GetVersion(), IOS_GetRevision());
	Print("\x1B[%d;%dH", 0, cols-strlen(buf)-1);	
	Print(buf);
}

void set_highlight(bool highlight)
{
	if (highlight)
	{
		Print("\x1b[%u;%um", 47, false);
		Print("\x1b[%u;%um", 30, false);
	} else
	{
		Print("\x1b[%u;%um", 37, false);
		Print("\x1b[%u;%um", 40, false);
	}
}

void *allocate_memory(u32 size)
{
	return memalign(32, (size+31)&(~31) );
}

void Verify_Flags()
{
	if (Power_Flag)
	{
		WPAD_Shutdown();
		STM_ShutdownToStandby();
	}
	if (Reset_Flag)
	{
		WPAD_Shutdown();
		STM_RebootSystem();
	}
}


void waitforbuttonpress(u32 *out, u32 *outGC)
{
	u32 pressed = 0;
	u32 pressedGC = 0;

	while (true)
	{
		Verify_Flags();
		
		WPAD_ScanPads();
		pressed = WPAD_ButtonsDown(0);

		PAD_ScanPads();
		pressedGC = PAD_ButtonsDown(0);

		if(pressed || pressedGC) 
		{
			if (pressedGC)
			{
				// Without waiting you can't select anything
				usleep (20000);
			}
			if (out) *out = pressed;
			if (outGC) *outGC = pressedGC;
			return;
		}
	}
}


s32 identify(u64 titleid, u32 *ios)
{
	char filepath[ISFS_MAXPATH] ATTRIBUTE_ALIGN(0x20);
	u8 *tmdBuffer = NULL;
	u32 tmdSize;
	u8 *tikBuffer = NULL;
	u32 tikSize;
	u8 *certBuffer = NULL;
	u32 certSize;
	
	int ret;

	Print("Reading TMD...");
	fflush(stdout);
	
	sprintf(filepath, "/title/%08x/%08x/content/title.tmd", TITLE_UPPER(titleid), TITLE_LOWER(titleid));
	ret = read_file(filepath, &tmdBuffer, &tmdSize);
	if (ret < 0)
	{
		Print("Reading TMD failed\n");
		return ret;
	}
	Print("done\n");

	*ios = (u32)(tmdBuffer[0x18b]);

	Print("Reading ticket...");
	fflush(stdout);

	sprintf(filepath, "/ticket/%08x/%08x.tik", TITLE_UPPER(titleid), TITLE_LOWER(titleid));
	ret = read_file(filepath, &tikBuffer, &tikSize);
	if (ret < 0)
	{
		Print("Reading ticket failed\n");
		free(tmdBuffer);
		return ret;
	}
	Print("done\n");

	Print("Reading certs...");
	fflush(stdout);

	sprintf(filepath, "/sys/cert.sys");
	ret = read_file(filepath, &certBuffer, &certSize);
	if (ret < 0)
	{
		Print("Reading certs failed\n");
		free(tmdBuffer);
		free(tikBuffer);
		return ret;
	}
	Print("done\n");
	
	Print("ES_Identify...");
	fflush(stdout);

	ret = ES_Identify((signed_blob*)certBuffer, certSize, (signed_blob*)tmdBuffer, tmdSize, (signed_blob*)tikBuffer, tikSize, NULL);
	if (ret < 0)
	{
		switch(ret)
		{
			case ES_EINVAL:
				Print("Error! ES_Identify (ret = %d;) Data invalid!\n", ret);
				break;
			case ES_EALIGN:
				Print("Error! ES_Identify (ret = %d;) Data not aligned!\n", ret);
				break;
			case ES_ENOTINIT:
				Print("Error! ES_Identify (ret = %d;) ES not initialized!\n", ret);
				break;
			case ES_ENOMEM:
				Print("Error! ES_Identify (ret = %d;) No memory!\n", ret);
				break;
			default:
				Print("Error! ES_Identify (ret = %d)\n", ret);
				break;
		}
		free(tmdBuffer);
		free(tikBuffer);
		free(certBuffer);
		return ret;
	}
	Print("done\n");
	
	free(tmdBuffer);
	free(tikBuffer);
	free(certBuffer);
	return 0;
}

