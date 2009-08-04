#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include <stdlib.h>
#include <gccore.h>
#include <wiiuse/wpad.h>

#include "tools.h"
#include "lz77.h"
#include "u8.h"
#include "config.h"

#define DIRENT_T_FILE 0
#define DIRENT_T_DIR 1

static u32 *xfb = NULL;
static GXRModeObj *rmode = NULL;

// Prevent IOS36 loading at startup
s32 __IOS_LoadStartupIOS()
{
	return 0;
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


typedef struct _dirent
{
	char name[ISFS_MAXPATH + 1];
	int type;
} dirent_t;


void videoInit()
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
 	
    int x = 24, y = 32, w, h;
    w = rmode->fbWidth - (32);
    h = rmode->xfbHeight - (48);

	CON_InitEx(rmode, x, y, w, h);

	VIDEO_ClearFrameBuffer(rmode, xfb, COLOR_BLACK);
}


/*
s32 getTmdInfo(u64 titleid, u16 *bootcontent)
{
	printf("Loading TMD...\n");
	char tmdPath[ISFS_MAXPATH];
	u8 *tmdBuffer;
	u32 tmdSize;
	
	sprintf(tmdPath, "/title/%08x/%08x/content/title.tmd", TITLE_UPPER(titleid), TITLE_LOWER(titleid));
	read_file(tmdPath, &tmdBuffer, &tmdSize);
	
	*bootcontent = ((tmd *)SIGNATURE_PAYLOAD((signed_blob *)tmdBuffer))->boot_index;
	
	free(tmdBuffer);

	return 0;
}
*/


s32 __FileCmp(const void *a, const void *b)
{
	dirent_t *hdr1 = (dirent_t *)a;
	dirent_t *hdr2 = (dirent_t *)b;
	
	if (hdr1->type == hdr2->type)
	{
		return strcmp(hdr1->name, hdr2->name);
	} else
	{
		if (hdr1->type == DIRENT_T_DIR)
		{
			return -1;
		} else
		{
			return 1;
		}
	}
}

s32 getdir(char *path, dirent_t **ent, u32 *cnt)
{
	s32 res;
	u32 num = 0;

	int i, j, k;
	
	res = ISFS_ReadDir(path, NULL, &num);
	if(res != ISFS_OK)
	{
		printf("Error: could not get dir entry count! (result: %d)\n", res);
		return -1;
	}

	//sleep(2);

	char *nbuf = (char *)allocate_memory((ISFS_MAXPATH + 1) * num);
	char ebuf[ISFS_MAXPATH + 1];
	//char pbuf[ISFS_MAXPATH + 1];

	if(nbuf == NULL)
	{
		printf("Error: could not allocate buffer for name list!\n");
		return -2;
	}

	res = ISFS_ReadDir(path, nbuf, &num);
	if(res != ISFS_OK)
	{
		printf("Error: could not get name list! (result: %d)\n", res);
		return -3;
	}
	
	*cnt = num;
	
	*ent = allocate_memory(sizeof(dirent_t) * num);

	for(i = 0, k = 0; i < num; i++)
	{	    
		for(j = 0; nbuf[k] != 0; j++, k++)
			ebuf[j] = nbuf[k];
		ebuf[j] = 0;
		k++;

		strcpy((*ent)[i].name, ebuf);
		
		//if(strcmp(path, "/") != 0)
		//	sprintf(pbuf, "%s/%s", path, ebuf);
		//else
		//	sprintf(pbuf, "/%s", ebuf);
		
		//(*ent)[i].type = ((isdir(pbuf) == 1) ? DIRENT_T_DIR : DIRENT_T_FILE);

	}
	
	qsort(*ent, *cnt, sizeof(dirent_t), __FileCmp);
	
	free(nbuf);
	return 0;
}

s32 get_game_list(char ***TitleIds, u32 *num)
{
	int ret;
	u32 maxnum;
	u32 tempnum = 0;
	u32 number;
	dirent_t *list;
    char path[ISFS_MAXPATH];
    sprintf(path, "/title/00010001");
    ret = getdir(path, &list, &maxnum);
    if (ret < 0)
	{
		printf("Reading folder /title/00010001 failed\n");
		return ret;
	}

	char **temp = allocate_memory(maxnum*4);
	if (temp == NULL)
	{
		printf("Out of memory\n");
		return -1;
	}

	int i;
	for (i = 0; i < maxnum; i++)
	{	
		if (memcmp(list[i].name, "48", 2) != 0)
		{
			sprintf(path, "/title/00010001/%s/content", list[i].name);
			ret = ISFS_ReadDir(path, NULL, &number);	
			if (number > 1) // 1 == tmd only
			{
				temp[tempnum] = allocate_memory(10);
				memset(temp[tempnum], 0x00, 10);
				memcpy(temp[tempnum], list[i].name, 8);	
				tempnum++;		
			}
		}
	}

	*TitleIds = temp;
	*num = tempnum;
	return 0;
}


s32 check_dol(u64 titleid, char *out)
{
	s32 cfd;
    s32 ret;
	u32 num;
	dirent_t *list;
    char contentpath[ISFS_MAXPATH];
    char path[ISFS_MAXPATH];
    int cnt = 0;
	bool found = false;
	
	u8 LZ77_0x10 = 0x10;
    u8 LZ77_0x11 = 0x11;
	u8 *decompressed;
	u8 *compressed;
	u32 size_out = 0;
	u32 decomp_size = 0;

    u8 *buffer = allocate_memory(8);
	
    u8 check1[6] = {0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
    u8 check2[1] = {0x00};

    sprintf(contentpath, "/title/%08x/%08x/content", TITLE_UPPER(titleid), TITLE_LOWER(titleid));
    ret = getdir(contentpath, &list, &num);
    if (ret < 0)
	{
		printf("Reading folder of the title failed\n");
		free(buffer);
		return ret;
	}
	
	for(cnt=0; cnt < num; cnt++)
    {        
        if(strstr(list[cnt].name, ".app") != NULL) 
        {
			memset(buffer, 0x00, 8);
            sprintf(path, "/title/%08x/%08x/content/%s", TITLE_UPPER(titleid), TITLE_LOWER(titleid), list[cnt].name);
  
            cfd = ISFS_Open(path, ISFS_OPEN_READ);
            if (cfd < 0)
			{
	    	    printf("ISFS_Open for %s failed %d\n", path, cfd);
				continue; 
			}

            ret = ISFS_Read(cfd, buffer, 7);
	        if (ret < 0)
	        {
	    	    printf("ISFS_Read for %s failed %d\n", path, ret);
		        ISFS_Close(cfd);
				continue;
	        }

            ISFS_Close(cfd);	

			if (buffer[0] == LZ77_0x10 || buffer[0] == LZ77_0x11)
			{
                if (buffer[0] == LZ77_0x10)
				{
					printf("Found LZ77 0x10 compressed content --> %s\n", list[cnt].name);
				} else
				{
					printf("Found LZ77 0x11 compressed content --> %s\n", list[cnt].name);
				}
				printf("This is most likely the main DOL, decompressing for checking\n");
				read_file(path, &compressed, &size_out);
				printf("read file\n");
				ret = decompressLZ77content(compressed, 32, &decompressed, &decomp_size);
				if (ret < 0)
				{
					printf("Decompressing failed\n");
					free(buffer);
					free(list);
					return ret;
				}				
				memcpy(buffer, decompressed, 8);
 			}
			
	        ret = memcmp(buffer, check1, 6);
            if(ret == 0)
            {
                ret = memcmp(&buffer[6], check2, 1);
                if(ret != 0)
                {
                    printf("Found DOL --> %s\n", list[cnt].name);
                    sprintf(out, "%s", path);
					found = true;
                    break;
               } 
            } 
        }
    }
	
	free(buffer);
	free(list);
	
	if (!found)
	{
		printf("No .dol found\n");
		return -1;
	}
	return 0;
}


u32 load_dol(u8 *buffer)
{
	dolheader *dolfile;
	dolfile = (dolheader *)buffer;
	
	printf("Entrypoint: %08x\n", dolfile->entry_point);
	printf("BSS: %08x, size = %08x(%u)\n", dolfile->bss_start, dolfile->bss_size, dolfile->bss_size);

	memset((void *)dolfile->bss_start, 0, dolfile->bss_size);
	DCFlushRange((void *)dolfile->bss_start, dolfile->bss_size);
	
    printf("BSS cleared\n");
	
	u32 doloffset;
	u32 memoffset;
	u32 restsize;
	u32 size;

	int i;
	for (i = 0; i < 7; i++)
	{	
		if(dolfile->text_pos[i] < sizeof(dolheader))
			continue;
	    
		doloffset = (u32)buffer + dolfile->text_pos[i];
		memoffset = dolfile->text_start[i];
		restsize = dolfile->text_size[i];

		printf("Moving text section %u from %08x to %08x-%08x...", i, dolfile->text_pos[i], dolfile->text_start[i], dolfile->text_start[i]+dolfile->text_size[i]);
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

		printf("done\n");
		fflush(stdout);			
	}

	for(i = 0; i < 11; i++)
	{
		if(dolfile->data_pos[i] < sizeof(dolheader))
			continue;
		
		doloffset = (u32)buffer + dolfile->data_pos[i];
		memoffset = dolfile->data_start[i];
		restsize = dolfile->data_size[i];

		printf("Moving data section %u from %08x to %08x-%08x...", i, dolfile->data_pos[i], dolfile->data_start[i], dolfile->data_start[i]+dolfile->data_size[i]);
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

		printf("done\n");
		fflush(stdout);			
	} 
	return dolfile->entry_point;
}

void patch_dol(u8 *buffer, u32 size)
{


}

u32 loadAndRelocate(u64 titleid)
{
	u8 *contentBuf;
	u32 contentSize;
	u32 entry_point;
	int ret;
	
	char contentPath[ISFS_MAXPATH] ATTRIBUTE_ALIGN(32); 
	printf("Searching for main DOL...\n");
	ret = check_dol(titleid, contentPath);
	if (ret < 0)
	{
		printf("Searching for main.dol failed\n");
		return 0;
	}
	
    printf("Loading DOL: %s\n", contentPath);
	
	ret = read_file(contentPath, &contentBuf, &contentSize);
	if (ret < 0)
	{
		printf("Reading .dol failed\n");
		return 0;
	}
	
	if (isLZ77compressed(contentBuf))
	{
		u8 *decompressed;
		ret = decompressLZ77content(contentBuf, contentSize, &decompressed, &contentSize);
		if (ret < 0)
		{
			printf("Decompression failed\n");
			free(contentBuf);
			return ret;
		}
		free(contentBuf);
		contentBuf = decompressed;
	}

	patch_dol(contentBuf, contentSize);

	entry_point = load_dol(contentBuf);
	free(contentBuf);

	return entry_point;
}


void setVideoMode(char Region, int title)
{
    GXRModeObj *vmode = NULL;
	// Get vmode and Video_Mode for system settings first
	u32 tvmode = CONF_GetVideo();

	// Attention: This returns &TVNtsc480Prog for all progressive video modes
    vmode = VIDEO_GetPreferredMode(0);
	
	u8 Video_Mode;

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

	// Overwrite vmode and Video_Mode when disc region video mode is selected and Wii region doesn't match disc region
	if (title > 0)
	{
		switch (Region & 0xFF) 
		{
			case 'P':
				if (CONF_GetVideo() != CONF_VIDEO_PAL)
				{
					Video_Mode = VI_PAL;

					if (CONF_GetProgressiveScan() > 0 && VIDEO_HaveComponentCable())
					{
						vmode = &TVNtsc480Prog; // This seems to be correct!
					}
					else
					{
						vmode = &TVEurgb60Hz480IntDf;
					}				
				}
				break;

			case 'E':
			case 'J':
			default:
				if (CONF_GetVideo() != CONF_VIDEO_NTSC)
				{
					Video_Mode = VI_NTSC;

					if (CONF_GetProgressiveScan() > 0 && VIDEO_HaveComponentCable())
					{
						vmode = &TVNtsc480Prog;
					}
					else
					{
						vmode = &TVNtsc480IntDf;
					}				
				}
		}
	}
	 *(u32 *)0x800000CC = Video_Mode;
    DCFlushRange((void*)0x800000CC, sizeof(u32));
 
    VIDEO_Configure(vmode);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
 
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
}

s32 check_ascii(char *s) 
{
    int i = 0;
    s32 ret = 0;
    for(i=0; i < sizeof(s); i++)
    {
        if(s[i] < 0x20)
        { 
            ret = -1;
        break;
      }  
        if(s[i] > 0x7E) 
      {
          ret = -2;
        break;
      }  
  }  
  
  return ret;
}

char *read_name2(u64 titleid)
{
	s32 cfd;
    s32 ret;
	u32 num;
	dirent_t *list;
    char contentpath[ISFS_MAXPATH];
    char path[ISFS_MAXPATH];
	int i;
    int length;
    u32 cnt = 0;
	char *out;
	u8 *buffer = allocate_memory(800);
	
   
	sprintf(contentpath, "/title/%08x/%08x/content", TITLE_UPPER(titleid), TITLE_LOWER(titleid));
	
    ret = getdir(contentpath, &list, &num);
    if (ret < 0)
	{
		printf("Reading folder of the title failed\n");
		free(buffer);
		return NULL;
	}
	
	u8 imet[4] = {0x49, 0x4D, 0x45, 0x54};
	for(cnt=0; cnt < num; cnt++)
    {        
        if(strstr(list[cnt].name, ".app") != NULL) 
        {
			memset(buffer, 0x00, 800);
            sprintf(path, "/title/%08x/%08x/content/%s", TITLE_UPPER(titleid), TITLE_LOWER(titleid), list[cnt].name);
  
            cfd = ISFS_Open(path, ISFS_OPEN_READ);
            if (cfd < 0)
			{
	    	    printf("ISFS_OPEN for %s failed %d\n", path, cfd);
				continue;
			}
			
            ret = ISFS_Read(cfd, buffer, 800);
	        if (ret < 0)
	        {
	    	    printf("ISFS_Read for %s failed %d\n", path, ret);
		        ISFS_Close(cfd);
				continue;
	        }

            ISFS_Close(cfd);	
              
			if(memcmp((buffer+0x80), imet, 4) == 0)
			{
				length = 0;
				i = 0;
				while(buffer[0xF1 + i*2] != 0x00)
				{
					length++;
					i++;
				}
				
				out = allocate_memory(length+10);
				if(out == NULL)
				{
					printf("Allocating memory for buffer failed\n");
					free(buffer);
					return NULL;
				}
				memset(out, 0x00, length+10);
				
				i = 0;
				while(buffer[0xF1 + i*2] != 0x00)
				{
					out[i] = (char) buffer[0xF1 + i*2];
					i++;
				}
				
				
				free(buffer);
				free(list);
				
				u32 low;
		        low = TITLE_LOWER(titleid);
		        switch(low & 0xFF)
		        {
		            case 'E':
			            memcpy(out+i, " (NTSC-U)", 9); break;
			        case 'P':
			            memcpy(out+i, " (PAL)", 6); break;
			        case 'J':
			            memcpy(out+i, " (NTSC-J)", 9); break;	
			        case 'L':
			           memcpy(out+i, " (PAL)", 6); break;	
                   case 'N':
			            memcpy(out+i, " (NTSC-U)", 9); break;	
                   case 'M':
			            memcpy(out+i, " (PAL)", 6); break;
                    case 'K':
			            memcpy(out+i, " (NTSC)", 7); break;
                    default:
                        break;
                }	

				return out;
			}
			    
        }
    }
	
	free(buffer);
	free(list);
	
	return NULL;
}

char *read_name(u64 titleid)
{
	s32 cfd;
    s32 ret;
	dirent_t *list = NULL;
    char path[ISFS_MAXPATH];
	int i;
    int length;
	char *out;
	u8 *buffer = allocate_memory(800);
	
	
   
	// Try to read from banner.bin first
	sprintf(path, "/title/%08x/%08x/data/banner.bin", TITLE_UPPER(titleid), TITLE_LOWER(titleid));
  
	cfd = ISFS_Open(path, ISFS_OPEN_READ);
	if (cfd < 0)
	{
		//printf("ISFS_OPEN for %s failed %d\n", path, cfd);
	} else
	{
	    ret = ISFS_Read(cfd, buffer, 800);
	    if (ret < 0)
	    {
			printf("ISFS_Read for %s failed %d\n", path, ret);
		    ISFS_Close(cfd);
			return NULL;
		}

		ISFS_Close(cfd);	

		length = 0;
		i = 0;
		while(buffer[0x21 + i*2] != 0x00)
		{
			length++;
			i++;
		}
		out = allocate_memory(length+10);
		if(out == NULL)
		{
			printf("Allocating memory for buffer failed\n");
			free(buffer);
			return NULL;
		}
		memset(out, 0x00, length+10);
		
		i = 0;
		while (buffer[0x21 + i*2] != 0x00)
		{
			out[i] = (char) buffer[0x21 + i*2];
			i++;
		}
		
		free(buffer);
		u32 low;
		low = TITLE_LOWER(titleid);
		switch(low & 0xFF)
		{
		    case 'E':
			    memcpy(out+i, " (NTSC-U)", 9); break;
			case 'P':
			    memcpy(out+i, " (PAL)", 6); break;
			case 'J':
			    memcpy(out+i, " (NTSC-J)", 9); break;	
			case 'L':
			    memcpy(out+i, " (PAL)", 6); break;	
            case 'N':
			    memcpy(out+i, " (NTSC-U)", 9); break;	
            case 'M':
			    memcpy(out+i, " (PAL)", 6); break;
            case 'K':
			    memcpy(out+i, " (NTSC)", 7); break;
            default:
                break;
        }	
        ret = check_ascii(out);
		if(ret == 0)
        {	
        return out;
		} else
		{
		out = read_name2(titleid);
		return out;
		}
	}
   

	
	free(buffer);
	free(list);
	
	return NULL;
}

void bootTitle(u64 titleid)
{
	entrypoint appJump;
	int ret;
	u16 requested_ios;
	
	u32 entryPoint = loadAndRelocate(titleid);
	if (entryPoint == 0)
	{
		printf(".dol loading failed\n");
		return;
	}

	printf(".dol loaded\n");

	ret = identify(titleid, &requested_ios);
	if (ret < 0)
	{
		printf("Identify failed\n");
		return;
	}
	
	ISFS_Deinitialize();
	
	printf("Setting bus speed\n");
	*(u32*)0x800000F8 = 0x0E7BE2C0;
	printf("Setting cpu speed\n");
	*(u32*)0x800000FC = 0x2B73A840;

	DCFlushRange((void*)0x800000F8, 0xFF);
	
	// Remove 002 error
	printf("Fake IOS Version(%u)\n", requested_ios);
	*(u16 *)0x80003140 = requested_ios;
	*(u16 *)0x80003142 = 0xffff;
	*(u16 *)0x80003188 = requested_ios;
	*(u16 *)0x8000318A = 0xffff;
	
	DCFlushRange((void*)0x80003140, 4);
	DCFlushRange((void*)0x80003188, 4);
	
	ret = ES_SetUID(titleid);
	if (ret < 0)
	{
		printf("ES_SetUID failed %d", ret);
		return;
	}
	printf("ES_SetUID successful\n");
	printf("Loading complete, booting...\n");

	sleep(5);
	
	appJump = (entrypoint)entryPoint;
	
	setVideoMode(titleid, 1);
	
	WPAD_Shutdown();
	SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);

	appJump();	
}

#define menuitems 4

void show_menu()
{
	int i;
	u32 pressed;
	u32 pressedGC;
	int ret;

	int selection = 0;
	u32 optioncount[menuitems] = { 1, 1, 2, 11 };

	u32 optionselected[menuitems] = { 0 , 0, videooption, languageoption+1};

	char *start[1] = { "Start" };
	char *regionoptions[2] = { "Wii region", "Channel region" };
	char *languageoptions[11] = { "Default Language", "Japanese", "English", "German", "French", "Spanish", "Italian", "Dutch", "S. Chinese", "T. Chinese", "Korean" };

	u64 TitleIds[255];
	char *TitleNames[255];

	char **TitleStrings;
	u32 Titlecount;
	
	printf("\nLoading...");

	ret = get_game_list(&TitleStrings, &Titlecount);
	if (ret < 0)
	{
		printf("Error getting the title list\n");
		return;
	}
	if (Titlecount == 0)
	{
		printf("No titles found\n");
		return;
	}
	printf("...");
	
	optioncount[1] = Titlecount;
	char **optiontext[menuitems] = { start, TitleNames, regionoptions, languageoptions };

	for (i = 0; i < Titlecount; i++)
	{
	    TitleIds[i] = TITLE_ID(0x00010001, strtol(TitleStrings[i],NULL,16));
        TitleNames[i] = read_name(TitleIds[i]);
		printf(".");
	}	

	while (true)
	{
		printf("\x1b[J");
		
		printheadline();
		printf("\n");
		
		for (i = 0; i < menuitems; i++)
		{
			set_highlight(selection == i);
			if (optiontext[i][optionselected[i]] == NULL)
            {
                printf("???\n");
            } else
			{
				printf("%s\n", optiontext[i][optionselected[i]]);
            }
			set_highlight(false);
		}
		printf("\n");
		
		waitforbuttonpress(&pressed, &pressedGC);
		
		if (pressed == WPAD_BUTTON_UP || pressedGC == PAD_BUTTON_UP)
		{
			if (selection > 0)
			{
				selection--;
			} else
			{
				selection = menuitems-1;
			}
		}

		if (pressed == WPAD_BUTTON_DOWN || pressedGC == PAD_BUTTON_DOWN)
		{
			if (selection < menuitems-1)
			{
				selection++;
			} else
			{
				selection = 0;
			}
		}

		if (pressed == WPAD_BUTTON_LEFT || pressedGC == PAD_BUTTON_LEFT)
		{	
			if (optionselected[selection] > 0)
			{
				optionselected[selection]--;
			} else
			{
				optionselected[selection] = optioncount[selection]-1;
			}
		}

		if (pressed == WPAD_BUTTON_RIGHT || pressedGC == PAD_BUTTON_RIGHT)
		{	
			if (optionselected[selection] < optioncount[selection]-1)
			{
				optionselected[selection]++;
			} else
			{
				optionselected[selection] = 0;
			}
		}

		if (pressed == WPAD_BUTTON_A || pressedGC == PAD_BUTTON_A)
		{
			if (selection == 0)
			{
				videooption = optionselected[2];
				languageoption = optionselected[3]-1;				
				
				bootTitle(TitleIds[optionselected[1]]);
				printf("Press any button to contine\n");
				waitforbuttonpress(NULL, NULL);
			}
		}
		
		if (pressed == WPAD_BUTTON_B || pressedGC == PAD_BUTTON_B)
		{
			printf("Exiting...\n");
			return;
		}	
	}	
}

int main(int argc, char* argv[])
{
	videoInit();

	printheadline();

	IOS_ReloadIOS(249);

	PAD_Init();
	WPAD_Init();
	WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC_IR);					

/*	if ((IOS_GetVersion() != 249) || IOS_GetRevision() < 14)
	{
		printf("You need at least revision 14\n");
		sleep(15);
		exit(1);
	}
*/
	ISFS_Initialize();

	Set_Config_to_Defaults();
	
	show_menu();
	
	printf("Press any button\n");
	waitforbuttonpress(NULL, NULL);
	
	return 0;
}
