/*******************************************************************************
 * main.c
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

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include <stdlib.h>
#include <gccore.h>
#include <fat.h>
#include <ogc/lwp_watchdog.h>
#include <wiiuse/wpad.h>

#include "main.h"
#include "tools.h"
#include "isfs.h"
#include "name.h"
#include "lz77.h"
#include "config.h"
#include "patch.h"
#include "codes/codes.h"
#include "codes/patchcode.h"
#include "nand.h"
#include "background.h"

extern s32 __IOS_ShutdownSubsystems();
extern void __exception_closeall();

u32 *xfb = NULL;
GXRModeObj *rmode = NULL;
u8 Video_Mode;

void*	dolchunkoffset[64];			//TODO: variable size
u32		dolchunksize[64];			//TODO: variable size
u32		dolchunkcount;

void _unstub_start();

// Prevent IOS36 loading at startup
s32 __IOS_LoadStartupIOS()
{
	return 0;
}

static void power_cb() 
{
	Power_Flag = true;
}

static void reset_cb() 
{
	Reset_Flag = true;
}

void reboot()
{
	Disable_Emu();
	if (strncmp("STUBHAXX", (char *)0x80001804, 8) == 0)
	{
		Print("Exiting to HBC...\n");
		sleep(3);
		exit(0);
	}
	Print("Rebooting...\n");
	sleep(3);
	SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
}

typedef void (*entrypoint) (void);

typedef struct _dolheader
{
	u32 text_pos[7];
	u32 data_pos[11];
	u32 text_start[7];
	u32 data_start[11];
	u32 text_size[7];
	u32 data_size[11];
	u32 bss_start;
	u32 bss_size;
	u32 entry_point;
} dolheader;


void videoInit(bool banner)
{
	VIDEO_Init();
	rmode = VIDEO_GetPreferredMode(0);
	xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(xfb);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();
 	
    int x, y, w, h;
	
	if (banner)
	{
		x = 32;
		y = 212;
		w = rmode->fbWidth - 64;
		h = rmode->xfbHeight - 212 - 32;
	} else
	{
		x = 24;
		y = 32;
		w = rmode->fbWidth - (32);
		h = rmode->xfbHeight - (48);
	}

	CON_InitEx(rmode, x, y, w, h);
	
	// Set console text color
	Print("\x1b[%u;%um", 37, false);
	Print("\x1b[%u;%um", 40, false);
	

	VIDEO_ClearFrameBuffer(rmode, xfb, COLOR_BLACK);
}


s32 get_game_list(u64 **TitleIds, u32 *num)
{
	int ret;
	u32 maxnum;
	u32 tempnum = 0;
	u32 number;
	dirent_t *list = NULL;
    char path[ISFS_MAXPATH] ATTRIBUTE_ALIGN(32);
    sprintf(path, "/title/00010001");
 
	ret = getdir(path, &list, &maxnum);
	if (ret < 0)
	{
		Print("Reading folder %s failed\n", path);
		return ret;
	}

	u64 *temp = malloc(sizeof(u64) * maxnum);
	if (temp == NULL)
	{
		free(list);
		Print("Out of memory\n");
		return -1;
	}

	int i;
	for (i = 0; i < maxnum; i++)
	{	
		// Ignore channels starting with H (Channels) and U (Loadstructor channels)
		if (memcmp(list[i].name, "48", 2) != 0 && memcmp(list[i].name, "55", 2) != 0 
		// Also ignore the HBC, "JODI" and 0xaf1bf516 
		&& memcmp(list[i].name, "4a4f4449", 8) != 0 && memcmp(list[i].name, "4A4F4449", 8) != 0 
		&& memcmp(list[i].name, "af1bf516", 8) != 0 && memcmp(list[i].name, "AF1BF516", 8) != 0
 		// And ignore everything that's not using characters or numbers(only check 1st char)
		&& strtol(list[i].name,NULL,16) >= 0x30000000 && strtol(list[i].name,NULL,16) <= 0x7a000000 )
		{
			sprintf(path, "/title/00010001/%s/content", list[i].name);
			
			ret = getdircount(path, &number);
			
			if (ret >= 0 && number > 1) // 1 == tmd only
			{
				temp[tempnum] = TITLE_ID(0x00010001, strtol(list[i].name,NULL,16));
				tempnum++;		
			}
		}
	}

	*TitleIds = temp;
	*num = tempnum;
	free(list);
	return 0;
}


s32 check_dol(u64 titleid, char *out, u16 bootcontent)
{
    s32 ret;
	u32 num;
	dirent_t *list;
    char path[ISFS_MAXPATH] ATTRIBUTE_ALIGN(32);
    int cnt = 0;
	
	u8 *decompressed = NULL;
	u32 decomp_size = 0;
	
	u8 *buffer = allocate_memory(32);	// Needs to be aligned because it's used for nand access
	
	if (buffer == NULL)
	{
		Print("Out of memory\n");
		return -1;
	}
 
    u8 check[6] = {0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
 
    sprintf(path, "/title/%08x/%08x/content", TITLE_UPPER(titleid), TITLE_LOWER(titleid));
    ret = getdir(path, &list, &num);
    if (ret < 0)
	{
		Print("Reading folder of the title failed\n");
		free(buffer);
		return ret;
	}
	
	for (cnt=0; cnt < num; cnt++)
    {        
        if ((strstr(list[cnt].name, ".app") != NULL || strstr(list[cnt].name, ".APP") != NULL) && (strtol(list[cnt].name, NULL, 16) != bootcontent))
        {			
			memset(buffer, 0, 32);
            sprintf(path, "/title/%08x/%08x/content/%s", TITLE_UPPER(titleid), TITLE_LOWER(titleid), list[cnt].name);
  
            ret = read_file_from_nand(path, buffer, 32);
	        if (ret < 0)
	        {
	    	    // Error is printed in read_file_from_nand already
				continue;
	        }

			if (isLZ77compressed(buffer))
			{
				//Print("Found LZ77 compressed content --> %s\n", list[cnt].name);
				//Print("This is most likely the main DOL, decompressing for checking\n");

				// We only need 6 bytes...
				ret = decompressLZ77content(buffer, 32, &decompressed, &decomp_size, 32);
				if (ret < 0)
				{
					Print("Decompressing failed\n");
					free(list);
					free(buffer);
					return ret;
				}				
				memcpy(buffer, decompressed, 8);

				free(decompressed);
 			}
			
	        ret = memcmp(buffer, check, 6);
            if(ret == 0)
            {
				//Print("Found DOL --> %s\n", list[cnt].name);
				sprintf(out, "%s", path);
				free(list);
				free(buffer);
				return 0;
            } 
        }
    }
	
	free(list);
	free(buffer);
	
	Print("No .dol found\n");
	return -1;
}

void sneek_video_patch(void *addr, u32 len)
{
	u8 *addr_start = addr;
	u8 *addr_end = addr+len;
	
	while(addr_start < addr_end)
	{
		if( *(vu32*)(addr_start) == 0x3C608000 )
		{
			if( ((*(vu32*)(addr_start+4) & 0xFC1FFFFF ) == 0x800300CC) && ((*(vu32*)(addr_start+8) >> 24) == 0x54 ) )
			{
				//dbgprintf("DIP:[patcher] Found VI pattern:%08X\n", (u32)(addr_start) | 0x80000000 );
				*(vu32*)(addr_start+4) = 0x5400F0BE | ((*(vu32*)(addr_start+4) & 0x3E00000) >> 5	);
			}
		}
		addr_start += 4;
	}
}

void patch_dol(bool bootcontent)
{
	s32 ret;
	int i;
	bool hookpatched = false;
	
	for (i=0;i < dolchunkcount;i++)
	{		
		if (!bootcontent)
		{
			if (languageoption != -1)
			{
				ret = patch_language(dolchunkoffset[i], dolchunksize[i], languageoption);
			}
			
			search_video_modes(dolchunkoffset[i], dolchunksize[i]);
			switch(videopatchoption)
			{
				case 1:
					sneek_video_patch(dolchunkoffset[i], dolchunksize[i]);
				break;
				
				case 2:
					patch_video_modes_to(rmode, 1);
				break;
				
				case 3:
					patch_video_modes_to(rmode, 2);
				break;

				case 4:
					patch_video_modes_to(rmode, 3);
				break;
				
				case 5:
					patch_video_modes_to(rmode, 3);
					sneek_video_patch(dolchunkoffset[i], dolchunksize[i]);
				break;
			
				case 0:
				default:
				break;			
			}	
		}

		if (hooktypeoption != 0)
		{
			// Before this can be done, the codehandler needs to be in memory, and the code to patch needs to be in the right pace
			if (dochannelhooks(dolchunkoffset[i], dolchunksize[i], bootcontent))
			{
				hookpatched = true;
			}			
		}
	}
	if (hooktypeoption != 0 && !hookpatched)
	{
		Print("Error: Could not patch the hook\n");
		Print("Ocarina and debugger won't work\n");
	}
}  


u32 load_dol(u8 *buffer)
{
	dolchunkcount = 0;
	
	dolheader *dolfile;
	dolfile = (dolheader *)buffer;
	
	Print("Entrypoint: %08x\n", dolfile->entry_point);
	//Print("BSS: %08x, size = %08x(%u)\n", dolfile->bss_start, dolfile->bss_size, dolfile->bss_size);

	memset((void *)dolfile->bss_start, 0, dolfile->bss_size);
	DCFlushRange((void *)dolfile->bss_start, dolfile->bss_size);
	ICInvalidateRange((void *)dolfile->bss_start, dolfile->bss_size);
	
    //Print("BSS cleared\n");
	
	u32 doloffset;
	u32 memoffset;
	u32 restsize;
	u32 size;

	int i;
	for (i = 0; i < 7; i++)
	{	
		if(dolfile->text_pos[i] < sizeof(dolheader))
			continue;
	    
		dolchunkoffset[dolchunkcount] = (void *)dolfile->text_start[i];
		dolchunksize[dolchunkcount] = dolfile->text_size[i];
		dolchunkcount++;
		
		doloffset = (u32)buffer + dolfile->text_pos[i];
		memoffset = dolfile->text_start[i];
		restsize = dolfile->text_size[i];

		//Print("Moving text section %u from %08x to %08x-%08x...", i, dolfile->text_pos[i], dolfile->text_start[i], dolfile->text_start[i]+dolfile->text_size[i]);
		//fflush(stdout);
			
		while (restsize > 0)
		{
			if (restsize > 2048)
			{
				size = 2048;
			} else
			{
				size = restsize;
			}
			restsize -= size;
			
			memcpy((void *)memoffset, (void *)doloffset, size);
			
			DCFlushRange((void *)memoffset, size);
			ICInvalidateRange ((void *)memoffset, size);
			
			doloffset += size;
			memoffset += size;
		}

		//Print("done\n");
		//fflush(stdout);			
	}

	for(i = 0; i < 11; i++)
	{
		if(dolfile->data_pos[i] < sizeof(dolheader))
			continue;
		
		dolchunkoffset[dolchunkcount] = (void *)dolfile->data_start[i];
		dolchunksize[dolchunkcount] = dolfile->data_size[i];
		dolchunkcount++;

		doloffset = (u32)buffer + dolfile->data_pos[i];
		memoffset = dolfile->data_start[i];
		restsize = dolfile->data_size[i];

		//Print("Moving data section %u from %08x to %08x-%08x...", i, dolfile->data_pos[i], dolfile->data_start[i], dolfile->data_start[i]+dolfile->data_size[i]);
		//fflush(stdout);
			
		while (restsize > 0)
		{
			if (restsize > 2048)
			{
				size = 2048;
			} else
			{
				size = restsize;
			}
			restsize -= size;
			memcpy((void *)memoffset, (void *)doloffset, size);

			DCFlushRange((void *)memoffset, size);
			ICInvalidateRange ((void *)memoffset, size);
			
			doloffset += size;
			memoffset += size;
		}

		//Print("done\n");
		//fflush(stdout);			
	} 
	//Print("All .dol sections moved\n");
	//fflush(stdout);			
	return dolfile->entry_point;
}


s32 search_and_read_dol(u64 titleid, u8 **contentBuf, u32 *contentSize, bool skip_bootcontent, u8 *tmdBuffer)
{
	char filepath[ISFS_MAXPATH] ATTRIBUTE_ALIGN(32);
	int ret;
	u16 bootindex;
	u16 bootcontent;
	bool bootcontent_loaded;
	
	tmd_content *p_cr = NULL;

	u32 pressed;
	u32 pressedGC;

	bootindex = ((tmd *)SIGNATURE_PAYLOAD((signed_blob *)tmdBuffer))->boot_index;
	p_cr = TMD_CONTENTS(((tmd *)SIGNATURE_PAYLOAD((signed_blob *)tmdBuffer)));
	bootcontent = p_cr[bootindex].cid;

	// Write bootcontent to filepath and overwrite it in case another .dol is found
	sprintf(filepath, "/title/%08x/%08x/content/%08x.app", TITLE_UPPER(titleid), TITLE_LOWER(titleid), bootcontent);

	if (skip_bootcontent)
	{
		bootcontent_loaded = false;
		//Print("Searching for main DOL...\n");
	
		// Search the folder for .dols and ignore the apploader
		ret = check_dol(titleid, filepath, bootcontent);
		if (ret < 0)
		{
			Print("Searching for main.dol failed\n");
			Print("Press A to load nand loader instead...\n");
			waitforbuttonpress(&pressed, &pressedGC);
			if (pressed != WPAD_BUTTON_A && pressed != WPAD_CLASSIC_BUTTON_A && pressedGC != PAD_BUTTON_A)
			{
				Print("Other button pressed\n");
				return ret;
			}
			bootcontent_loaded = true;
		}
	} else
	{
		bootcontent_loaded = true;
	}
	
    if (bootcontent_loaded)
	{
		Print("Loading Apploader: %s\n", filepath);
	} else
	{
		Print("Loading DOL: %s\n", filepath);
	}
	
	ret = read_full_file_from_nand(filepath, contentBuf, contentSize);
	if (ret < 0)
	{
		Print("Reading .dol failed\n");
		return ret;
	}
	
	if (isLZ77compressed(*contentBuf))
	{
		Print("Decompressing ...");
		u8 *decompressed;
		ret = decompressLZ77content(*contentBuf, *contentSize, &decompressed, contentSize, 0);
		if (ret < 0)
		{
			Print("Decompression failed\n");
			free(*contentBuf);
			return ret;
		}
		free(*contentBuf);
		*contentBuf = decompressed;
		Print("done\n");
	}	
	
	if (bootcontent_loaded)
	{
		return 1;
	} else
	{
		return 0;
	}
}


void determineVideoMode(u64 titleid)
{
	if (videooption == 0)
	{
		// Get rmode and Video_Mode for system settings first
		u32 tvmode = CONF_GetVideo();

		// Attention: This returns &TVNtsc480Prog for all progressive video modes
		rmode = VIDEO_GetPreferredMode(0);
		
		switch (tvmode) 
		{
			case CONF_VIDEO_PAL:
				if (CONF_GetEuRGB60() > 0) 
				{
					Video_Mode = VI_EURGB60;
				}
				else 
				{
					Video_Mode = VI_PAL;
				}
				break;

			case CONF_VIDEO_MPAL:
				Video_Mode = VI_MPAL;
				break;

			case CONF_VIDEO_NTSC:
			default:
				Video_Mode = VI_NTSC;
				
		}

		// Overwrite rmode and Video_Mode when Default Video Mode is selected and Wii region doesn't match the channel region
		u32 low;
		low = TITLE_LOWER(titleid);
		char Region = low % 256;
		if (*(char *)&low != 'W') // Don't overwrite video mode for WiiWare
		{
			switch (Region) 
			{
				case 'P':
				case 'D':
				case 'F':
				case 'X':
				case 'Y':
					if (CONF_GetVideo() != CONF_VIDEO_PAL)
					{
						Video_Mode = VI_EURGB60;

						if (CONF_GetProgressiveScan() > 0 && VIDEO_HaveComponentCable())
						{
							rmode = &TVNtsc480Prog; // This seems to be correct!
						}
						else
						{
							rmode = &TVEurgb60Hz480IntDf;
						}				
					}
					break;

				case 'E':
				case 'J':
				case 'T':
					if (CONF_GetVideo() != CONF_VIDEO_NTSC)
					{
						Video_Mode = VI_NTSC;
						if (CONF_GetProgressiveScan() > 0 && VIDEO_HaveComponentCable())
						{
							rmode = &TVNtsc480Prog;
						}
						else
						{
							rmode = &TVNtsc480IntDf;
						}				
					}
			}
		}
	} else
	{
		if (videooption == 1)
		{
			rmode = &TVNtsc480IntDf;
		} else
		if (videooption == 2)
		{
			rmode = &TVNtsc480Prog;
		} else
		if (videooption == 3)
		{
			rmode = &TVEurgb60Hz480IntDf;
		} else
		if (videooption == 4)
		{
			rmode = &TVEurgb60Hz480Prog;
		} else
		if (videooption == 5)
		{
			rmode = &TVPal528IntDf;
		} else
		if (videooption == 6)
		{
			rmode = &TVMpal480IntDf;
		} else
		if (videooption == 7)
		{
			rmode = &TVMpal480Prog;
		}
		Video_Mode = (rmode->viTVMode) >> 2;
	}
}

void setVideoMode()
{	
	*(u32 *)0x800000CC = Video_Mode;
	DCFlushRange((void*)0x800000CC, sizeof(u32));
	
	// Overwrite all progressive video modes as they are broken in libogc
	if (videomode_interlaced(rmode) == 0)
	{
		rmode = &TVNtsc480Prog;
	}

	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(xfb);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	
	if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
}

void green_fix() //GREENSCREEN FIX
{     
    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(TRUE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
}

void bootTitle(u64 titleid)
{
	entrypoint appJump;
	int ret;
	u32 requested_ios;
	u8 *dolbuffer;
	u32 dolsize;
	bool bootcontentloaded;
	
	u8 *tmdBuffer = NULL;
	u32 tmdSize;
	
	ret = read_TMD(titleid, &tmdBuffer, &tmdSize);
	if (ret < 0 || tmdSize == 0)
	{
		if (tmdBuffer)
		{
			free(tmdBuffer);
		}
		return;
	}
	
	requested_ios = (u32)(tmdBuffer[0x18b]);
	
	ret = search_and_read_dol(titleid, &dolbuffer, &dolsize, (bootmethodoption == 0), tmdBuffer);
	if (ret < 0)
	{
		Print(".dol loading failed\n");
		free(tmdBuffer);
		return;
	}
	bootcontentloaded = (ret == 1);

	determineVideoMode(titleid);
	
	entryPoint = load_dol(dolbuffer);
	
	free(dolbuffer);

	//Print(".dol loaded\n");

	// Set the clock
	settime(secs_to_ticks(time(NULL) - 946684800));

	*(u32 *)0x80000000 = TITLE_LOWER(titleid);
	DCFlushRange((void*)0x80000000, 32);
	
	// Memory setup when booting the main.dol
	if (entryPoint != 0x3400)
	{
		*(u32 *)0x80000034 = 0;			// Arena High, the apploader does this too
		*(u32 *)0x800000F4 = 0x817FE000;	// BI2, the apploader does this too
		*(u32 *)0x800000F8 = 0x0E7BE2C0;	// bus speed
		*(u32 *)0x800000FC = 0x2B73A840;	// cpu speed

		DCFlushRange((void*)0x80000000, 0x100);
		
		memset((void *)0x817FE000, 0, 0x2000); // Clearing BI2, or should this be read from somewhere?
		DCFlushRange((void*)0x817FE000, 0x2000);		

		if (hooktypeoption == 0)
		{
			*(u32 *)0x80003180 = TITLE_LOWER(titleid);
		} else
		{
			*(u32 *)0x80003180 = 0;		// No comment required here
		}
		
		*(u32 *)0x80003184 = 0x81000000;	// Game id address, while there's all 0s at 0x81000000 when using the apploader...

		DCFlushRange((void*)0x80003180, 32);
	}
	
	if (hooktypeoption != 0)
	{
		do_codes(titleid);
	}
	
	patch_dol(bootcontentloaded);

	appJump = (entrypoint)entryPoint;

	// Auto cIOS
	u8 ios_to_load = find_cIOS_with_base(requested_ios);
	s32 nand_device = get_nand_device();

	if (ios_to_load == 0)
	{
		Print("The requested IOS is: IOS%u\n", requested_ios);
	} else
	{
		Print("Loading IOS%u(base IOS%u).", ios_to_load, requested_ios);

		WPAD_Shutdown();
		Disable_Emu();
		ISFS_Deinitialize();
		
		Print(".");

		IOS_ReloadIOS(ios_to_load);
		ISFS_Initialize();	
		
		Print(".");
	
		// Reinit controls in case of an error later
		PAD_Init();
		WPAD_Init();
		WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC_IR);

		Print(".");
		
		ret = Enable_Emu(nand_device);
		if (ret < 0)
		{
			return;
		}

		Print(".\n");
	}

	// ES_Identify as the title
	ret = identify(titleid, tmdBuffer, tmdSize);
	if (ret < 0)
	{
		Print("Identify failed\n");
		free(tmdBuffer);
		return;
	}

	// ES_SetUID, maybe not required at all?	
	ret = ES_SetUID(titleid);
	if (ret < 0)
	{
		Print("ES_SetUID failed %d", ret);
		free(tmdBuffer);
		return;
	}	
	//Print("ES_SetUID successful\n");
	
	// Remove 002 error
	*(u16 *)0x80003140 = requested_ios;
	*(u16 *)0x80003142 = 0xffff;
	*(u16 *)0x80003188 = requested_ios;
	*(u16 *)0x8000318A = 0xffff;
	
	DCFlushRange((void*)0x80003140, 32);
	DCFlushRange((void*)0x80003180, 32);
	
	tell_cIOS_to_return_to_channel();

	Print("Preparations complete, booting...\n");

	if (!get_silent())
	{
		sleep(5);
	}

	// TODO
	waitforbuttonpress(NULL, NULL);
	
	setVideoMode();
	green_fix();
	
	WPAD_Shutdown();
	IRQ_Disable();
	__IOS_ShutdownSubsystems();
	__exception_closeall();

	//SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);

	if (entryPoint != 0x3400)
	{
		if (hooktypeoption != 0)
		{
			__asm__(
						"lis %r3, entryPoint@h\n"
						"ori %r3, %r3, entryPoint@l\n"
						"lwz %r3, 0(%r3)\n"
						"mtlr %r3\n"
						"lis %r3, 0x8000\n"
						"ori %r3, %r3, 0x18A8\n"
						"mtctr %r3\n"
						"bctr\n"
						);
						
		} else
		{
			appJump();	
		}
	} else
	{
		if (hooktypeoption != 0)
		{
			__asm__(
						"lis %r3, returnpoint@h\n"
						"ori %r3, %r3, returnpoint@l\n"
						"mtlr %r3\n"
						"lis %r3, 0x8000\n"
						"ori %r3, %r3, 0x18A8\n"
						"mtctr %r3\n"
						"bctr\n"
						"returnpoint:\n"
						"bl DCDisable\n"
						"bl ICDisable\n"
						"li %r3, 0\n"
						"mtsrr1 %r3\n"
						"lis %r4, entryPoint@h\n"
						"ori %r4,%r4,entryPoint@l\n"
						"lwz %r4, 0(%r4)\n"
						"mtsrr0 %r4\n"
						"rfi\n"
						);
		} else
		{
			_unstub_start();
		}
	}
}

#define menuitems 9

void show_menu()
{
	int i;
	u32 pressed;
	u32 pressedGC;
	int ret;

	int selection = 0;
	u32 optioncount[menuitems] = { 1, 1, 8, 6, 11, 8, 4, 3, 2 };

	u32 optionselected[menuitems] = { 0 , 0, videooption, videopatchoption, languageoption+1, hooktypeoption, ocarinaoption, debuggeroption, bootmethodoption };

	char *start[1] = { "Start" };
	char *videooptions[8] = { "Default Video Mode", "Force NTSC480i", "Force NTSC480p", "Force PAL480i", "Force PAL480p", "Force PAL576i", "Force MPAL480i", "Force MPAL480p" };
	char *videopatchoptions[6] = { "No Video patches", "Sneek's video patch", "Smart Video patching", "More Video patching", "Full Video patching", "Sneek + Full patching" };
	char *languageoptions[11] = { "Default Language", "Japanese", "English", "German", "French", "Spanish", "Italian", "Dutch", "S. Chinese", "T. Chinese", "Korean" };
	char *hooktypeoptions[8] = { "No Ocarina&debugger", "Hooktype: VBI", "Hooktype: KPAD", "Hooktype: Joypad", "Hooktype: GXDraw", "Hooktype: GXFlush", "Hooktype: OSSleepThread", "Hooktype: AXNextFrame" };
	char *ocarinaoptions[4] = { "No Ocarina", "Ocarina from NAND", "Ocarina from SD", "Ocarina from USB" };
	char *debuggeroptions[3] = { "No debugger", "Debugger enabled", "paused start" };
	char *bootmethodoptions[2] = { "Normal boot method", "Load apploader" };

	Print("\nLoading...");

	u64 *TitleIds;
	u32 Titlecount;

	ret = get_game_list(&TitleIds, &Titlecount);
	if (ret < 0)
	{
		Print("Error getting the title list\n");
		return;
	}

	if (Titlecount == 0)
	{
		Print("No titles found\n");
		return;
	}
	
	Print("...");
	
	char **TitleNames = malloc(sizeof(char *) * Titlecount);
	if (TitleNames == NULL)
	{
		free(TitleIds);
		Print("\nOut of memory\n");
		return;	
	}
	
	Print("...");
	
	optioncount[1] = Titlecount;
	char **optiontext[menuitems] = { start, TitleNames, videooptions, videopatchoptions, languageoptions, hooktypeoptions, ocarinaoptions, debuggeroptions, bootmethodoptions };

	for (i = 0; i < Titlecount; i++)
	{
        TitleNames[i] = get_name(TitleIds[i]);		
		Print(".");
	}

	
	// TODO
	waitforbuttonpress(NULL, NULL);

	// Sort the titles by their names
	bool changed = true;
	char *temp_char;
	u64 temp_u64;
	u32 sort_i;
	u32 sort_j;
	for(sort_i=0; sort_i < Titlecount-1 && changed; sort_i++)
	{
		changed = false;
		for(sort_j=0; sort_j < Titlecount-1-sort_i; sort_j++)
		{
			if (strcmp(TitleNames[sort_j], TitleNames[sort_j+1]) > 0)
			{
				temp_char = TitleNames[sort_j];
				temp_u64 = TitleIds[sort_j];
				
				TitleNames[sort_j] = TitleNames[sort_j+1];
				TitleIds[sort_j] = TitleIds[sort_j+1];
				
				TitleNames[sort_j+1] = temp_char;
				TitleIds[sort_j+1] = temp_u64;
				
				changed = true;
			}
		}
	}		

	while (true)
	{
		Print("\x1b[2J");
		
		printheadline();
		Print("\n");
		
		for (i = 0; i < menuitems; i++)
		{
			set_highlight(selection == i);
			if (optiontext[i][optionselected[i]] == NULL)
            {
				Print("???\n");
            } else
			{
				Print("%s\n", optiontext[i][optionselected[i]]);
            }
			set_highlight(false);
		}
		Print("\n");
		fflush(stdout);
		
		waitforbuttonpress(&pressed, &pressedGC);
		
		if (pressed == WPAD_BUTTON_B || pressed == WPAD_CLASSIC_BUTTON_B || pressedGC == PAD_BUTTON_B)
		{
			selection = 0;
		}
		
		if (pressed == WPAD_BUTTON_UP || pressed == WPAD_CLASSIC_BUTTON_UP || pressedGC == PAD_BUTTON_UP)
		{
			if (selection > 0)
			{
				selection--;
			} else
			{
				selection = menuitems-1;
			}
		}

		if (pressed == WPAD_BUTTON_DOWN || pressed == WPAD_CLASSIC_BUTTON_DOWN || pressedGC == PAD_BUTTON_DOWN)
		{
			if (selection < menuitems-1)
			{
				selection++;
			} else
			{
				selection = 0;
			}
		}

		if (pressed == WPAD_BUTTON_LEFT || pressed == WPAD_CLASSIC_BUTTON_LEFT || pressedGC == PAD_BUTTON_LEFT)
		{	
			if (optionselected[selection] > 0)
			{
				optionselected[selection]--;
			} else
			{
				optionselected[selection] = optioncount[selection]-1;
			}
		}

		if (pressed == WPAD_BUTTON_RIGHT || pressed == WPAD_CLASSIC_BUTTON_RIGHT || pressedGC == PAD_BUTTON_RIGHT)
		{	
			if (optionselected[selection] < optioncount[selection]-1)
			{
				optionselected[selection]++;
			} else
			{
				optionselected[selection] = 0;
			}
		}

		if (pressed == WPAD_BUTTON_A || pressed == WPAD_CLASSIC_BUTTON_A || pressedGC == PAD_BUTTON_A)
		{
			if (selection == 0)
			{
				videooption = optionselected[2];
				videopatchoption = optionselected[3];
				languageoption = optionselected[4]-1;				
				hooktypeoption = optionselected[5];				
				ocarinaoption = optionselected[6];				
				debuggeroption = optionselected[7];				
				bootmethodoption = optionselected[8];				
				
				free(TitleNames);

				bootTitle(TitleIds[optionselected[1]]);

				free(TitleIds);

				return;
			}
		}
		
		if (pressed == WPAD_BUTTON_HOME || pressed == WPAD_CLASSIC_BUTTON_HOME || pressedGC == PAD_BUTTON_START)
		{
			free(TitleIds);
			free(TitleNames);
			ISFS_Deinitialize();
			return;
		}	
	}	
}

#define nandmenuitems 1

void show_nand_menu()
{
	int i;
	u32 pressed;
	u32 pressedGC;
	int ret;

	int selection = 0;
	u32 optioncount[nandmenuitems] = { 3 };
	u32 optionselected[nandmenuitems] = { 0 };

	char *nandoptions[3] = { "Use real NAND", "Use SD-NAND", "Use USB-NAND" };
	char **optiontext[nandmenuitems] = { nandoptions };

	while (true)
	{
		Print("\x1b[2J");
		
		printheadline();
		Print("\n");
		
		for (i = 0; i < nandmenuitems; i++)
		{
			set_highlight(selection == i);
			if (optiontext[i][optionselected[i]] == NULL)
            {
                Print("???\n");
            } else
			{
				Print("%s\n", optiontext[i][optionselected[i]]);
            }
			set_highlight(false);
		}
		Print("\n");
		fflush(stdout);
		
		waitforbuttonpress(&pressed, &pressedGC);
		
		if (pressed == WPAD_BUTTON_UP || pressed == WPAD_CLASSIC_BUTTON_UP || pressedGC == PAD_BUTTON_UP)
		{
			if (selection > 0)
			{
				selection--;
			} else
			{
				selection = nandmenuitems-1;
			}
		}

		if (pressed == WPAD_BUTTON_DOWN || pressed == WPAD_CLASSIC_BUTTON_DOWN || pressedGC == PAD_BUTTON_DOWN)
		{
			if (selection < nandmenuitems-1)
			{
				selection++;
			} else
			{
				selection = 0;
			}
		}

		if (pressed == WPAD_BUTTON_LEFT || pressed == WPAD_CLASSIC_BUTTON_LEFT || pressedGC == PAD_BUTTON_LEFT)
		{	
			if (optionselected[selection] > 0)
			{
				optionselected[selection]--;
			} else
			{
				optionselected[selection] = optioncount[selection]-1;
			}
		}

		if (pressed == WPAD_BUTTON_RIGHT || pressed == WPAD_CLASSIC_BUTTON_RIGHT || pressedGC == PAD_BUTTON_RIGHT)
		{	
			if (optionselected[selection] < optioncount[selection]-1)
			{
				optionselected[selection]++;
			} else
			{
				optionselected[selection] = 0;
			}
		}

		if (pressed == WPAD_BUTTON_A || pressed == WPAD_CLASSIC_BUTTON_A || pressedGC == PAD_BUTTON_A)
		{
			if (selection == 0)
			{
				ret = 0;
				if (optionselected[0] == 1)
				{
					ret = Enable_Emu(EMU_SD);
				} else
				if (optionselected[0] == 2)
				{
					ret = Enable_Emu(EMU_USB);
				}
				if (ret < 0)
				{
					waitforbuttonpress(NULL, NULL);
					continue;
				}
				
				show_menu();
				return;
			}
		}
		
		if (pressed == WPAD_BUTTON_HOME || pressed == WPAD_CLASSIC_BUTTON_HOME || pressedGC == PAD_BUTTON_START)
		{
			return;
		}	
	}	
}


int main(int argc, char* argv[])
{
	int ret;

	if (ES_GetTitleID(&old_title_id) < 0)
	{
		old_title_id = (0x00010001ULL << 32) | *(u32 *)0x80000000;
	}

	if (argc == 11 && argv != NULL)
	{
		/*
			title id
			IOS
			0 no nand emu	1 sd nand emu	2 usb nand emu
			0 debug	1 silent
			0 Default Video Mode	1 NTSC480i	2 NTSC480p	3 PAL480i	4 PAL480p	5 PAL576i	6 MPAL480i	7 MPAL480p
			-1 Default Language	0 Japanese	1 English	2 German	3 French	4 Spanish	5 Italian	6 Dutch	7 S. Chinese	8 T. Chinese	9 Korean
			0 No Video patches	1 Smart Video patching	2 More Video patching	3 Full Video patching
			0 No Ocarina&debugger	1 Hooktype: VBI	2 Hooktype: KPAD	3 Hooktype: Joypad	4 Hooktype: GXDraw	5 Hooktype: GXFlush	6 Hooktype: OSSleepThread	7 Hooktype: AXNextFrame
			0 No debugger	1 Debugger enabled
			0 No Ocarina	1 Ocarina from NAND 	2 Ocarina from SD	3 Ocarina from USB"
			0 Normal boot method	1 Load apploader
		*/

		Print("Loading cIOS...\n");
		
		IOS_ReloadIOS(strtol(argv[1],NULL,10));

		ISFS_Initialize();

		if (strtol(argv[3],NULL,10) == 0)
		{
			videoInit(false);
		} else
		{
			set_silent(true);
		}
	
		if (strtol(argv[2],NULL,10) != 0)
		{
			Print("Starting nand emu...\n");
			if (strtol(argv[2],NULL,10) == 1)
			{
				ret = Enable_Emu(EMU_SD);
			} else
			{
				ret = Enable_Emu(EMU_USB);
			}
			if (ret < 0)
			{
				Print("Starting nand emu failed\n");
				sleep(10);
				reboot();
			}
		}		
		
		videooption = strtol(argv[4],NULL,10);
		languageoption = strtol(argv[5],NULL,10);
		videopatchoption = strtol(argv[6],NULL,10);
		hooktypeoption = strtol(argv[7],NULL,10);
		debuggeroption = strtol(argv[8],NULL,10);
		ocarinaoption = strtol(argv[9],NULL,10);
		bootmethodoption = strtol(argv[10],NULL,10);

		Print("Booting title %s...\n", argv[0]);
		bootTitle((u64)(0x0001000100000000ULL + (u32)argv[0]));
		sleep(10);
		reboot();
	}	
	
	videoInit(true);
	
	DrawBackground(rmode);
	
	Set_Config_to_Defaults();

	IOS_ReloadIOS(CIOS_VERSION);

	ISFS_Initialize();

	printheadline();

	Power_Flag = false;
	Reset_Flag = false;
	SYS_SetPowerCallback (power_cb);
    SYS_SetResetCallback (reset_cb);

	PAD_Init();
	WPAD_Init();
	WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC_IR);					

	Set_Config_to_Defaults();
	
	//if (IOS_GetVersion() == CIOS_VERSION && IOS_GetRevision() >= 14)
	//{
		show_nand_menu();
	//} else
	//{
	//	show_menu();
	//}
	
	//Print("Press any button\n");
	//waitforbuttonpress(NULL, NULL);
	
	reboot();
	
	return 0;
}
