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
#include <stdarg.h>
#include <gccore.h>
#include <ogc/tpl.h>
#include <math.h>
#include <fat.h>
#include <ogc/gx.h>
#include <ogc/lwp_watchdog.h>


#include <wiiuse/wpad.h>

#include "tools.h"
#include "lz77.h"
#include "u8.h"
#include "config.h"

#define DIRENT_T_FILE 0
#define DIRENT_T_DIR 1

#define DEFAULT_FIFO_SIZE	(256*1024)
#define Vector guVector

void *xfb[2] = { NULL, NULL};
static u32 *xfb2 = NULL;
GXRModeObj *rmode;
Mtx GXmodelView2D;
int whichfb = 0;
void *gp_fifo = NULL;

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

void videoInit_()
{
	VIDEO_Init();
	rmode = VIDEO_GetPreferredMode(0);
   xfb2 = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	//xfb[0] = (u32 *)MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	//xfb[1] = (u32 *)MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(xfb2);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();
 	
    int x = 24, y = 32, w, h;
    w = rmode->fbWidth - (32);
    h = rmode->xfbHeight - (48);

	CON_InitEx(rmode, x, y, w, h);

	VIDEO_ClearFrameBuffer(rmode, xfb2, COLOR_BLACK);
	printf("video Init\n");
	sleep(5);
}
void videoInit()
{
	f32 yscale;
	u32 xfbHeight;
	Mtx perspective;
	
	rmode = VIDEO_GetPreferredMode(NULL);

	if (CONF_GetAspectRatio() == CONF_ASPECT_16_9)
	{
		rmode->viWidth = 678;
		rmode->viXOrigin = (VI_MAX_WIDTH_NTSC - 678)/2;
	}

	VIDEO_Configure (rmode);

	xfb[0] = (u32 *)MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	xfb[1] = (u32 *)MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	VIDEO_ClearFrameBuffer (rmode, xfb[0], COLOR_BLACK);
	VIDEO_ClearFrameBuffer (rmode, xfb[1], COLOR_BLACK);

	VIDEO_SetNextFramebuffer(xfb[whichfb]);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();

	gp_fifo = memalign(32,DEFAULT_FIFO_SIZE);
	memset(gp_fifo,0,DEFAULT_FIFO_SIZE);

	GX_Init (gp_fifo, DEFAULT_FIFO_SIZE);

	GXColor background = { 21, 35, 40, 0xff };
	GX_SetCopyClear (background, 0x00ffffff);

	yscale = GX_GetYScaleFactor(rmode->efbHeight,rmode->xfbHeight);
	xfbHeight = GX_SetDispCopyYScale(yscale);
	GX_SetScissor(0,0,rmode->fbWidth,rmode->efbHeight);
	GX_SetDispCopySrc(0,0,rmode->fbWidth,rmode->efbHeight);
	GX_SetDispCopyDst(rmode->fbWidth,xfbHeight);
	GX_SetCopyFilter(rmode->aa,rmode->sample_pattern,GX_TRUE,rmode->vfilter);
	GX_SetFieldMode(rmode->field_rendering,((rmode->viHeight==2*rmode->xfbHeight)?GX_ENABLE:GX_DISABLE));

	if (rmode->aa){
		GX_SetPixelFmt(GX_PF_RGB565_Z16, GX_ZC_LINEAR);
	}
	else{
		GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
	}

	GX_SetDispCopyGamma(GX_GM_1_0);

	GX_ClearVtxDesc();
	GX_InvVtxCache ();
	GX_InvalidateTexAll();

	GX_SetVtxDesc(GX_VA_TEX0, GX_NONE);
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc (GX_VA_CLR0, GX_DIRECT);

	GX_SetVtxAttrFmt (GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt (GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
	GX_SetZMode (GX_FALSE, GX_LEQUAL, GX_TRUE);

	GX_SetNumChans(1);
	GX_SetNumTexGens(1);
	GX_SetTevOp (GX_TEVSTAGE0, GX_PASSCLR);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);

	guMtxIdentity(GXmodelView2D);
	guMtxTransApply (GXmodelView2D, GXmodelView2D, 0.0F, 0.0F, -50.0F);
	GX_LoadPosMtxImm(GXmodelView2D,GX_PNMTX0);

	guOrtho(perspective,0,479,0,639,0,300);
	GX_LoadProjectionMtx(perspective, GX_ORTHOGRAPHIC);

	GX_SetViewport(0,0,rmode->fbWidth,rmode->efbHeight,0,1);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	GX_SetAlphaUpdate(GX_TRUE);
	
	GX_SetCullMode(GX_CULL_NONE);
}

void gfx_drawtile(f32 xpos, f32 ypos, u16 width, u16 height, u8 data[], float degrees, float scaleX, f32 scaleY, u8 alpha, f32 frame,f32 maxframe )
//---------------------------------------------------------------------------------
{
	GXTexObj texObj;
	f32 s1= frame/maxframe;
	f32 s2= (frame+1)/maxframe;
	f32 t1=0;
	f32 t2=1;
	
	GX_InitTexObj(&texObj, data, width*maxframe,height, GX_TF_RGBA8,GX_CLAMP, GX_CLAMP,GX_FALSE);
	GX_InitTexObjLOD(&texObj, GX_NEAR, GX_NEAR, 0.0f, 0.0f, 0.0f, 0, 0, GX_ANISO_1);
	GX_LoadTexObj(&texObj, GX_TEXMAP0);

	GX_SetTevOp (GX_TEVSTAGE0, GX_MODULATE);
  	GX_SetVtxDesc (GX_VA_TEX0, GX_DIRECT);

	Mtx m,m1,m2, mv;
	width *=.5;
	height*=.5;
	guMtxIdentity (m1);
	guMtxScaleApply(m1,m1,scaleX,scaleY,1.0);
	Vector axis =(Vector) {0 , 0, 1 };
	guMtxRotAxisDeg (m2, &axis, degrees);
	guMtxConcat(m2,m1,m);
	guMtxTransApply(m,m, xpos+width,ypos+height,0);
	guMtxConcat (GXmodelView2D, m, mv);
	GX_LoadPosMtxImm (mv, GX_PNMTX0);
	GX_Begin(GX_QUADS, GX_VTXFMT0,4);
  	GX_Position3f32(-width, -height,  0);
  	GX_Color4u8(0xFF,0xFF,0xFF,alpha);
  	GX_TexCoord2f32(s1, t1);
  
  	GX_Position3f32(width, -height,  0);
 	GX_Color4u8(0xFF,0xFF,0xFF,alpha);
  	GX_TexCoord2f32(s2, t1);
  
  	GX_Position3f32(width, height,  0);
	GX_Color4u8(0xFF,0xFF,0xFF,alpha);
  	GX_TexCoord2f32(s2, t2);
  
  	GX_Position3f32(-width, height,  0);
	GX_Color4u8(0xFF,0xFF,0xFF,alpha);
  	GX_TexCoord2f32(s1, t2);
	GX_End();
	GX_LoadPosMtxImm (GXmodelView2D, GX_PNMTX0);

	GX_SetTevOp (GX_TEVSTAGE0, GX_PASSCLR);
  	GX_SetVtxDesc (GX_VA_TEX0, GX_NONE);
}

void gfx_printf(u8 *font,s32 x,s32 y,u8 alpha, char *fmt, ...)
//---------------------------------------------------------------------------------
{
	int i;
	char buf[1024];
	int len;

	va_list ap;
	va_start(ap, fmt);
	len = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
  
	for(i=0;i<len; i++, x+=10)
	{
		if(( buf[i] < 33 ) || ( buf[i] > 126 )){
			continue;
		}

		gfx_drawtile( x, y, 10, 24, font, 0, 1.0f, 1.0f, alpha, buf[i]-33, 96 );
	}
}

void gfx_draw_image(f32 xpos, f32 ypos, u16 width, u16 height, GXTexObj texObj, float degrees, float scaleX, f32 scaleY, u8 alpha )
//---------------------------------------------------------------------------------
{	
	//GXTexObj texObj;
	//GX_InitTexObj(&texObj, data, width,height, GX_TF_RGBA8,GX_CLAMP, GX_CLAMP,GX_FALSE);
	GX_LoadTexObj(&texObj, GX_TEXMAP0);

	GX_SetTevOp (GX_TEVSTAGE0, GX_MODULATE);
  	GX_SetVtxDesc (GX_VA_TEX0, GX_DIRECT);

	Mtx m,m1,m2, mv;
	width *=.5;
	height*=.5;
	guMtxIdentity (m1);
	guMtxScaleApply(m1,m1,scaleX,scaleY,1.0);
	Vector axis =(Vector) {0 , 0, 1 };
	guMtxRotAxisDeg (m2, &axis, degrees);
	guMtxConcat(m2,m1,m);

	guMtxTransApply(m,m, xpos+width,ypos+height,0);
	guMtxConcat (GXmodelView2D, m, mv);
	GX_LoadPosMtxImm (mv, GX_PNMTX0);
	
	GX_Begin(GX_QUADS, GX_VTXFMT0,4);
  	GX_Position3f32(-width, -height,  0);
  	GX_Color4u8(0xFF,0xFF,0xFF,alpha);
  	GX_TexCoord2f32(0, 0);
  
  	GX_Position3f32(width, -height,  0);
 	GX_Color4u8(0xFF,0xFF,0xFF,alpha);
  	GX_TexCoord2f32(1, 0);
  
  	GX_Position3f32(width, height,  0);
	GX_Color4u8(0xFF,0xFF,0xFF,alpha);
  	GX_TexCoord2f32(1, 1);
  
  	GX_Position3f32(-width, height,  0);
	GX_Color4u8(0xFF,0xFF,0xFF,alpha);
  	GX_TexCoord2f32(0, 1);
	GX_End();
	GX_LoadPosMtxImm (GXmodelView2D, GX_PNMTX0);

	GX_SetTevOp (GX_TEVSTAGE0, GX_PASSCLR);
  	GX_SetVtxDesc (GX_VA_TEX0, GX_NONE);
}

void gfx_render_direct()
//---------------------------------------------------------------------------------
{
    GX_DrawDone ();
	whichfb ^= 1;		// flip framebuffer
	GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
	GX_SetColorUpdate(GX_TRUE);
	GX_CopyDisp(xfb[whichfb],GX_TRUE);
	VIDEO_SetNextFramebuffer(xfb[whichfb]);
 	VIDEO_Flush();
 	VIDEO_WaitVSync();
}


s32 __FileCmp(const void *a, const void *b)
{
	dirent_t *hdr1 = (dirent_t *)a;
	dirent_t *hdr2 = (dirent_t *)b;
	
	if (hdr1->type == hdr2->type)
	{
		return strcmp(hdr1->name, hdr2->name);
	} else
	{
		return 0;
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

	char *nbuf = (char *)allocate_memory((ISFS_MAXPATH + 1) * num);
	char ebuf[ISFS_MAXPATH + 1];

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
		if (memcmp(list[i].name, "48", 2) != 0 && memcmp(list[i].name, "55", 2) != 0) // Ignore channels starting with H (Channels) and U (Loadstructor channels)
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
        if(strstr(list[cnt].name, ".app") != NULL || strstr(list[cnt].name, ".APP") != NULL) 
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
				ret = read_file(path, &compressed, &size_out);
				if (ret < 0)
				{
					printf("Reading file failed\n");
					return ret;
				}
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

void patch_dol(u8 *buffer, s32 size)
{
	s32 ret;

	if (languageoption != -1)
	{
		ret = patch_language(buffer, size, languageoption);
	}
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


void setVideoMode(u64 titleid, int title)
{
    char Region = (char)((u32)titleid % 256);
	
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
		u32 low;
		low = TITLE_LOWER(titleid);
		if (*(char *)&low != 'W') // Don't overwrite video mode for WiiWare
		{
			switch (Region & 0xFF) 
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

bool check_text(char *s) 
{
    int i = 0;
    for(i=0; i < strlen(s); i++)
    {
        if (s[i] < 32 || s[i] > 165)
		{
			return false;
		}
	}  

	return true;
}

char *read_name_from_banner_app(u64 titleid)
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
				
				return out;
			}
			    
        }
    }
	
	free(buffer);
	free(list);
	
	return NULL;
}

char *read_name_from_banner_bin(u64 titleid)
{
	s32 cfd;
    s32 ret;
    char path[ISFS_MAXPATH];
	int i;
    int length;
	char *out;
	u8 *buffer = allocate_memory(160);
   
	// Try to read from banner.bin first
	sprintf(path, "/title/%08x/%08x/data/banner.bin", TITLE_UPPER(titleid), TITLE_LOWER(titleid));
  
	cfd = ISFS_Open(path, ISFS_OPEN_READ);
	if (cfd < 0)
	{
		//printf("ISFS_OPEN for %s failed %d\n", path, cfd);
		return NULL;
	} else
	{
	    ret = ISFS_Read(cfd, buffer, 160);
	    if (ret < 0)
	    {
			printf("ISFS_Read for %s failed %d\n", path, ret);
		    ISFS_Close(cfd);
			free(buffer);
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

		return out;		
	}
 	
	free(buffer);
	
	return NULL;
}

char *get_name(u64 titleid)
{
	char *temp;
	temp = read_name_from_banner_bin(titleid);
	if (temp == NULL || !check_text(temp))
	{
		temp = read_name_from_banner_app(titleid);
	}
	
	if (temp != NULL)
	{
		u32 low;
		low = TITLE_LOWER(titleid);
		if (*(char *)&low == 'W')
		{
			return temp;
		}
		switch(low & 0xFF)
		{
			case 'E':
				memcpy(temp+strlen(temp), " (NTSC-U)", 9);
				break;
			case 'P':
				memcpy(temp+strlen(temp), " (PAL)", 6);
				break;
			case 'J':
				memcpy(temp+strlen(temp), " (NTSC-J)", 9);
				break;	
			case 'L':
				memcpy(temp+strlen(temp), " (PAL)", 6);
				break;	
			case 'N':
				memcpy(temp+strlen(temp), " (NTSC-U)", 9);
				break;		
			case 'M':
				memcpy(temp+strlen(temp), " (PAL)", 6);
				break;
			case 'K':
				memcpy(temp+strlen(temp), " (NTSC)", 7);
				break;
			default:
				break;
				
		}
	}
	return temp;
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
	
	// Set the clock
	settime(secs_to_ticks(time(NULL) - 946684800));

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
	
	setVideoMode(titleid, videooption);
	
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
        TitleNames[i] = get_name(TitleIds[i]);		
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



u32 get_tpl_vc(GXTexObj *TexObj, unsigned short *heighttemp, unsigned short *widthtemp)
{
u8 *app;
u8 *banner;
u8 *decompressed_banner;
u8 *tpl;

int app_size;
int banner_size;
int tpl_size;
u32 decompressed_banner_size;

app_size = read_sd("sd:/00000000.app", &app);
do_U8_archive(app+0x640, "sd:/u8");
banner_size = read_sd("sd:/u8/meta/banner.bin", &banner);
decompressLZ77content(banner+0x24, banner_size, &decompressed_banner, &decompressed_banner_size);
do_U8_archive(decompressed_banner, "sd:/u8/extracted");

printf("\n\nTPL Stuff...");
sleep(5);
/*
//tpl_size = read_sd("sd:/u8/extracted/timg/BannerImage.tpl", &tpl);

 FILE *tplfp = fopen("sd:/u8/extracted/timg/BannerImage.tpl","rb");


                //unsigned short heighttemp = 0;
                //unsigned short widthtemp = 0;

             //   fseek(tplfp , 0x14, SEEK_SET);
             //   fread((void*)&heighttemp,1,2,tplfp);
             //   fread((void*)&widthtemp,1,2,tplfp);
                fseek (tplfp , 0 , SEEK_END);
                tpl_size = ftell (tplfp);
                rewind (tplfp);
				fread(tpl, 1, tpl_size, tplfp);
				fclose(tplfp);
	*/
tpl_size = read_sd(	"sd:/u8/extracted/timg/BannerImage.tpl", &tpl);		


TPLFile tplfile;
        int ret;
		printf("Open memory\n");
		sleep(2);

        ret = TPL_OpenTPLFromMemory(&tplfile, tpl, tpl_size);
        if(ret < 0) {
            free(tpl);
            tpl = NULL;
            return;
        }
		printf("Get texture\n");
		sleep(2);
        ret = TPL_GetTexture(&tplfile,0,TexObj);
        if(ret < 0) {
            free(tpl);
            tpl = NULL;
            return;
        }
		printf("close\n");
		sleep(2);
        TPL_CloseTPLFile(&tplfile);
printf("done\n");		
return tpl_size;		
		
}		




int main(int argc, char* argv[])
{
	videoInit_();
	videoInit();
	Set_Config_to_Defaults();

	printheadline();

	IOS_ReloadIOS(249);
	fatInitDefault();
	GXTexObj TexObj;
	unsigned short heighttemp = 0;
   unsigned short widthtemp = 0;
	get_tpl_vc(&TexObj, &heighttemp, &widthtemp);
	printf("Drawing TPD\n");
	sleep(5);
	gfx_draw_image(100, 100, 256, 192, TexObj, 0, 1, 1, 0xff);
	gfx_render_direct();
	sleep(5);
	gfx_draw_image(200, 200, 256, 192, TexObj, 0, 1, 1, 0x00);
	gfx_render_direct();
	sleep(5);
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
