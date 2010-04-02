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
	if (strncmp("STUBHAXX", (char *)0x80001804, 8) == 0) exit(0);
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
    char path[ISFS_MAXPATH];
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
		// Also ignore the HBC, title id "JODI"
		if (memcmp(list[i].name, "48", 2) != 0 && memcmp(list[i].name, "55", 2) != 0 && memcmp(list[i].name, "4a4f4449", 8) != 0 && memcmp(list[i].name, "4A4F4449", 8) != 0)
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
	s32 cfd;
    s32 ret;
	u32 num;
	dirent_t *list;
    char contentpath[ISFS_MAXPATH];
    char path[ISFS_MAXPATH];
    int cnt = 0;
	
	u8 LZ77_0x10 = 0x10;
    u8 LZ77_0x11 = 0x11;
	u8 *decompressed;
	u8 *compressed;
	u32 size_out = 0;
	u32 decomp_size = 0;
	
    u8 *buffer = memalign(32, 32);
	if (buffer == NULL)
	{
		Print("Out of memory\n");
		return -1;
	}
	
    u8 check[6] = {0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
 
    sprintf(contentpath, "/title/%08x/%08x/content", TITLE_UPPER(titleid), TITLE_LOWER(titleid));
    ret = getdir(contentpath, &list, &num);
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
			memset(buffer, 0x00, 32);
            sprintf(path, "/title/%08x/%08x/content/%s", TITLE_UPPER(titleid), TITLE_LOWER(titleid), list[cnt].name);
  
            cfd = ISFS_Open(path, ISFS_OPEN_READ);
            if (cfd < 0)
			{
	    	    Print("ISFS_Open for %s failed %d\n", path, cfd);
				continue; 
			}

            ret = ISFS_Read(cfd, buffer, 32);
	        if (ret < 0)
	        {
	    	    Print("ISFS_Read for %s failed %d\n", path, ret);
		        ISFS_Close(cfd);
				continue;
	        }

            ISFS_Close(cfd);	

			if (buffer[0] == LZ77_0x10 || buffer[0] == LZ77_0x11)
			{
                if (buffer[0] == LZ77_0x10)
				{
					Print("Found LZ77 0x10 compressed content --> %s\n", list[cnt].name);
				} else
				{
					Print("Found LZ77 0x11 compressed content --> %s\n", list[cnt].name);
				}
				Print("This is most likely the main DOL, decompressing for checking\n");
				ret = read_file(path, &compressed, &size_out);
				if (ret < 0)
				{
					Print("Reading file failed\n");
					free(list);
					free(buffer);
					return ret;
				}
				Print("read file\n");
				ret = decompressLZ77content(compressed, 32, &decompressed, &decomp_size);
				if (ret < 0)
				{
					Print("Decompressing failed\n");
					free(list);
					free(buffer);
					return ret;
				}				
				memcpy(buffer, decompressed, 8);
 			}
			
	        ret = memcmp(buffer, check, 6);
            if(ret == 0)
            {
				Print("Found DOL --> %s\n", list[cnt].name);
				sprintf(out, "%s", path);
				free(buffer);
				free(list);
				return 0;
            } 
        }
    }
	
	free(buffer);
	free(list);
	
	Print("No .dol found\n");
	return -1;
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
			
			if (videopatchoption != 0)
			{
				search_video_modes(dolchunkoffset[i], dolchunksize[i]);
				patch_video_modes_to(rmode, videopatchoption);
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
	Print("BSS: %08x, size = %08x(%u)\n", dolfile->bss_start, dolfile->bss_size, dolfile->bss_size);

	memset((void *)dolfile->bss_start, 0, dolfile->bss_size);
	DCFlushRange((void *)dolfile->bss_start, dolfile->bss_size);
	
    Print("BSS cleared\n");
	
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

		Print("Moving text section %u from %08x to %08x-%08x...", i, dolfile->text_pos[i], dolfile->text_start[i], dolfile->text_start[i]+dolfile->text_size[i]);
		fflush(stdout);
			
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
			ICInvalidateRange ((void *)memoffset, size);
			memcpy((void *)memoffset, (void *)doloffset, size);
			DCFlushRange((void *)memoffset, size);
			
			doloffset += size;
			memoffset += size;
		}

		Print("done\n");
		fflush(stdout);			
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

		Print("Moving data section %u from %08x to %08x-%08x...", i, dolfile->data_pos[i], dolfile->data_start[i], dolfile->data_start[i]+dolfile->data_size[i]);
		fflush(stdout);
			
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
			ICInvalidateRange ((void *)memoffset, size);
			memcpy((void *)memoffset, (void *)doloffset, size);
			DCFlushRange((void *)memoffset, size);
			
			doloffset += size;
			memoffset += size;
		}

		Print("done\n");
		fflush(stdout);			
	} 
	return dolfile->entry_point;
}


s32 search_and_read_dol(u64 titleid, u8 **contentBuf, u32 *contentSize, bool skip_bootcontent)
{
	char filepath[ISFS_MAXPATH] ATTRIBUTE_ALIGN(0x20);
	int ret;
	u16 bootindex;
	u16 bootcontent;
	bool bootcontent_loaded;
	
	u8 *tmdBuffer = NULL;
	u32 tmdSize;
	tmd_content *p_cr;

	u32 pressed;
	u32 pressedGC;

	Print("Reading TMD...");

	sprintf(filepath, "/title/%08x/%08x/content/title.tmd", TITLE_UPPER(titleid), TITLE_LOWER(titleid));
	ret = read_file(filepath, &tmdBuffer, &tmdSize);
	if (ret < 0)
	{
		Print("Reading TMD failed\n");
		return ret;
	}
	Print("done\n");
	
	bootindex = ((tmd *)SIGNATURE_PAYLOAD((signed_blob *)tmdBuffer))->boot_index;
	p_cr = TMD_CONTENTS(((tmd *)SIGNATURE_PAYLOAD((signed_blob *)tmdBuffer)));
	bootcontent = p_cr[bootindex].cid;

	free(tmdBuffer);

	// Write bootcontent to filepath and overwrite it in case another .dol is found
	sprintf(filepath, "/title/%08x/%08x/content/%08x.app", TITLE_UPPER(titleid), TITLE_LOWER(titleid), bootcontent);

	if (skip_bootcontent)
	{
		bootcontent_loaded = false;
		Print("Searching for main DOL...\n");
			
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
	
    Print("Loading DOL: %s\n", filepath);
	
	ret = read_file(filepath, contentBuf, contentSize);
	if (ret < 0)
	{
		Print("Reading .dol failed\n");
		return ret;
	}
	
	if (isLZ77compressed(*contentBuf))
	{
		u8 *decompressed;
		ret = decompressLZ77content(*contentBuf, *contentSize, &decompressed, contentSize);
		if (ret < 0)
		{
			Print("Decompression failed\n");
			free(*contentBuf);
			return ret;
		}
		free(*contentBuf);
		*contentBuf = decompressed;
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
	
	ret = search_and_read_dol(titleid, &dolbuffer, &dolsize, (bootmethodoption == 0));
	if (ret < 0)
	{
		Print(".dol loading failed\n");
		return;
	}
	bootcontentloaded = (ret == 1);

	determineVideoMode(titleid);
	
	entryPoint = load_dol(dolbuffer);
	
	free(dolbuffer);

	Print(".dol loaded\n");

	ret = identify(titleid, &requested_ios);
	if (ret < 0)
	{
		Print("Identify failed\n");
		return;
	}
	
	ISFS_Deinitialize();
	
	// Set the clock
	settime(secs_to_ticks(time(NULL) - 946684800));

	if (entryPoint != 0x3400)
	{
		Print("Setting bus speed\n");
		*(u32*)0x800000F8 = 0x0E7BE2C0;
		Print("Setting cpu speed\n");
		*(u32*)0x800000FC = 0x2B73A840;

		DCFlushRange((void*)0x800000F8, 0xFF);
	}
	
	// Remove 002 error
	Print("Fake IOS Version(%u)\n", requested_ios);
	*(u16 *)0x80003140 = requested_ios;
	*(u16 *)0x80003142 = 0xffff;
	*(u16 *)0x80003188 = requested_ios;
	*(u16 *)0x8000318A = 0xffff;
	
	DCFlushRange((void*)0x80003140, 4);
	DCFlushRange((void*)0x80003188, 4);
	
	ret = ES_SetUID(titleid);
	if (ret < 0)
	{
		Print("ES_SetUID failed %d", ret);
		return;
	}	
	Print("ES_SetUID successful\n");
	
	if (hooktypeoption != 0)
	{
		do_codes(titleid);
	}
	
	patch_dol(bootcontentloaded);

	Print("Loading complete, booting...\n");

	appJump = (entrypoint)entryPoint;

	if (!get_silent())
	{
		sleep(3);
	}

	setVideoMode();
	green_fix();
	
	WPAD_Shutdown();
	SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);

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
	ISFS_Initialize();
	
	int i;
	u32 pressed;
	u32 pressedGC;
	int ret;

	int selection = 0;
	u32 optioncount[menuitems] = { 1, 1, 8, 4, 11, 8, 3, 3, 2 };

	u32 optionselected[menuitems] = { 0 , 0, videooption, videopatchoption, languageoption+1, hooktypeoption, ocarinaoption, debuggeroption, bootmethodoption };

	char *start[1] = { "Start" };
	char *videooptions[8] = { "Default Video Mode", "Force NTSC480i", "Force NTSC480p", "Force PAL480i", "Force PAL480p", "Force PAL576i", "Force MPAL480i", "Force MPAL480p" };
	char *videopatchoptions[4] = { "No Video patches", "Smart Video patching", "More Video patching", "Full Video patching" };
	char *languageoptions[11] = { "Default Language", "Japanese", "English", "German", "French", "Spanish", "Italian", "Dutch", "S. Chinese", "T. Chinese", "Korean" };
	char *hooktypeoptions[8] = { "No Ocarina&debugger", "Hooktype: VBI", "Hooktype: KPAD", "Hooktype: Joypad", "Hooktype: GXDraw", "Hooktype: GXFlush", "Hooktype: OSSleepThread", "Hooktype: AXNextFrame" };
	char *ocarinaoptions[3] = { "No Ocarina", "Ocarina from SD", "Ocarina from USB" };
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
		
		waitforbuttonpress(&pressed, &pressedGC);
		
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
		
		if (pressed == WPAD_BUTTON_B || pressed == WPAD_CLASSIC_BUTTON_B || pressedGC == PAD_BUTTON_B)
		{
			free(TitleIds);
			free(TitleNames);
			Print("Exiting...\n");
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
					return;
				}
				
				show_menu();
				return;
			}
		}
		
		if (pressed == WPAD_BUTTON_B || pressed == WPAD_CLASSIC_BUTTON_B || pressedGC == PAD_BUTTON_B)
		{
			Print("Exiting...\n");
			return;
		}	
	}	
}


int main(int argc, char* argv[])
{
	int ret;

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
			0 No Ocarina	1 Ocarina from SD	2 Ocarina from USB" };
			0 Normal boot method	1 Load apploader
		*/

		Print("Loading cIOS...\n");
		
		IOS_ReloadIOS(strtol(argv[1],NULL,10));

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
		
		ISFS_Initialize();

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

	printheadline();

	IOS_ReloadIOS(249);

	Power_Flag = false;
	Reset_Flag = false;
	SYS_SetPowerCallback (power_cb);
    SYS_SetResetCallback (reset_cb);

	PAD_Init();
	WPAD_Init();
	WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC_IR);					

	Set_Config_to_Defaults();
	
	if (IOS_GetVersion() == 249 && IOS_GetRevision() >= 14)
	{
		show_nand_menu();
	} else
	{
		show_menu();
	}
	
	Print("Press any button\n");
	waitforbuttonpress(NULL, NULL);
	
	reboot();
	
	return 0;
}
