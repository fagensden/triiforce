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
#include "nand.h"
#include "sha1.h"
#include "isfs.h"

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

#define info_number 35

static u32 hashes[info_number][5] = {
{0x20e60607, 0x4e02c484, 0x2bbc5758, 0xee2b40fc, 0x35a68b0a},		// cIOSrev13a
{0x620c57c7, 0xd155b67f, 0xa451e2ba, 0xfb5534d7, 0xaa457878}, 		// cIOSrev13b
{0x3c968e54, 0x9e915458, 0x9ecc3bda, 0x16d0a0d4, 0x8cac7917},		// cIOS37 rev18
{0xe811bca8, 0xe1df1e93, 0x779c40e6, 0x2006e807, 0xd4403b97},		// cIOS38 rev18
{0x697676f0, 0x7a133b19, 0x881f512f, 0x2017b349, 0x6243c037},		// cIOS57 rev18
{0x34ec540b, 0xd1fb5a5e, 0x4ae7f069, 0xd0a39b9a, 0xb1a1445f},		// cIOS60 rev18
{0xd98a4dd9, 0xff426ddb, 0x1afebc55, 0x30f75489, 0x40b27ade},		// cIOS70 rev18
{0x0a49cd80, 0x6f8f87ff, 0xac9a10aa, 0xefec9c1d, 0x676965b9},		// cIOS37 rev19
{0x09179764, 0xeecf7f2e, 0x7631e504, 0x13b4b7aa, 0xca5fc1ab},		// cIOS38 rev19
{0x6010d5cf, 0x396415b7, 0x3c3915e9, 0x83ded6e3, 0x8f418d54},		// cIOS57 rev19
{0x589d6c4f, 0x6bcbd80a, 0xe768f258, 0xc53a322c, 0xd143f8cd},		// cIOS60 rev19
{0x8969e0bf, 0x7f9b2391, 0x31ecfd88, 0x1c6f76eb, 0xf9418fe6},		// cIOS70 rev19
{0x30aeadfe, 0x8b6ea668, 0x446578c7, 0x91f0832e, 0xb33c08ac},		// cIOS36 rev20
{0xba0461a2, 0xaa26eed0, 0x482c1a7a, 0x59a97d94, 0xa607773e},		// cIOS37 rev20
{0xb694a33e, 0xf5040583, 0x0d540460, 0x2a450f3c, 0x69a68148},		// cIOS38 rev20
{0xf6058710, 0xfe78a2d8, 0x44e6397f, 0x14a61501, 0x66c352cf},		// cIOS53 rev20
{0xfa07fb10, 0x52ffb607, 0xcf1fc572, 0xf94ce42e, 0xa2f5b523},		// cIOS55 rev20
{0xe30acf09, 0xbcc32544, 0x490aec18, 0xc276cee6, 0x5e5f6bab},		// cIOS56 rev20
{0x595ef1a3, 0x57d0cd99, 0x21b6bf6b, 0x432f6342, 0x605ae60d},		// cIOS57 rev20
{0x687a2698, 0x3efe5a08, 0xc01f6ae3, 0x3d8a1637, 0xadab6d48},		// cIOS60 rev20
{0xea6610e4, 0xa6beae66, 0x887be72d, 0x5da3415b, 0xa470523c},		// cIOS61 rev20
{0x64e1af0e, 0xf7167fd7, 0x0c696306, 0xa2035b2d, 0x6047c736},		// cIOS70 rev20
{0x0df93ca9, 0x833cf61f, 0xb3b79277, 0xf4c93cd2, 0xcd8eae17},		// cIOS80 rev20
{0x074dfb39, 0x90a5da61, 0x67488616, 0x68ccb747, 0x3a5b59b3}, 		// cIOS36 rev21
{0x6956a016, 0x59542728, 0x8d2efade, 0xad8ed01e, 0xe7f9a780}, 		// cIOS37 rev21
{0xdc8b23e6, 0x9d95fefe, 0xac10668a, 0x6891a729, 0x2bdfbca0}, 		// cIOS38 rev21
{0xaa2cdd40, 0xd628bc2e, 0x96335184, 0x1b51404c, 0x6592b992}, 		// cIOS53 rev21
{0x4a3d6d15, 0x014f5216, 0x84d65ffe, 0x6daa0114, 0x973231cf}, 		// cIOS55 rev21
{0xca883eb0, 0x3fe8e45c, 0x97cc140c, 0x2e2d7533, 0x5b369ba5}, 		// cIOS56 rev21
{0x469831dc, 0x918acc3e, 0x81b58a9a, 0x4493dc2c, 0xaa5e57a0}, 		// cIOS57 rev21
{0xe5af138b, 0x029201c7, 0x0c1241e7, 0x9d6a5d43, 0x37a1456a}, 		// cIOS58 rev21
{0x0fdee208, 0xf1d031d3, 0x6fedb797, 0xede8d534, 0xd3b77838}, 		// cIOS60 rev21
{0xaf588570, 0x13955a32, 0x001296aa, 0x5f30e37f, 0x0be91316}, 		// cIOS61 rev21
{0x50deaba2, 0x9328755c, 0x7c2deac8, 0x385ecb49, 0x65ea3b2b}, 		// cIOS70 rev21
{0x811b6a0b, 0xe26b9419, 0x7ffd4930, 0xdccd6ed3, 0x6ea2cdd2}, 		// cIOS80 rev21

};

static char infos[info_number][24] = {
{"cIOS rev13a"},
{"cIOS rev13b"},
{"cIOS37rev18"},
{"cIOS38rev18"},
{"cIOS57rev18"},
{"cIOS60rev18"},
{"cIOS70rev18"},
{"cIOS37rev19"},
{"cIOS38rev19"},
{"cIOS57rev19"},
{"cIOS60rev19"},
{"cIOS70rev19"},
{"cIOS36rev20"},
{"cIOS37rev20"},
{"cIOS38rev20"},
{"cIOS53rev20"},
{"cIOS55rev20"},
{"cIOS56rev20"},
{"cIOS57rev20"},
{"cIOS60rev20"},
{"cIOS61rev20"},
{"cIOS70rev20"},
{"cIOS80rev20"},
{"cIOS36rev21"},
{"cIOS37rev21"},
{"cIOS38rev21"},
{"cIOS53rev21"},
{"cIOS55rev21"},
{"cIOS56rev21"},
{"cIOS57rev21"},
{"cIOS58rev21"},
{"cIOS60rev21"},
{"cIOS61rev21"},
{"cIOS70rev21"},
{"cIOS80rev21"},
};	

s32 brute_tmd(tmd *p_tmd) 
{
	u16 fill;
	for(fill=0; fill<65535; fill++) 
	{
		p_tmd->fill3=fill;
		sha1 hash;
		SHA1((u8 *)p_tmd, TMD_SIZE(p_tmd), hash);;
		  
		if (hash[0]==0) 
		{
			return 0;
		}
	}
	return -1;
}	

void identify_IOS(u8 ios_slot, u8 *ios_base, u32 *ios_revision, char *ios_string)
{
	char filepath[ISFS_MAXPATH] ATTRIBUTE_ALIGN(0x20);
	u8 *buffer = NULL;
	u32 filesize;
	signed_blob *TMD = NULL;
	tmd *t = NULL;
	u32 TMD_size = 0;
	u32 i;
	iosinfo_t *iosinfo = NULL;

	u8 temp_ios_base = 0;
	u32 temp_ios_revision = 0;
	
	// Backup in case the other methods fail
	if (ios_string != NULL)
	{
		if (ios_slot == IOS_GetVersion())
		{
			sprintf(ios_string, "IOS%u (Rev %u)", IOS_GetVersion(), IOS_GetRevision());
		} else
		{
			sprintf(ios_string, "IOS%u", ios_slot);
		}
	}

	sprintf(filepath, "/title/%08x/%08x/content/title.tmd", 0x00000001, ios_slot);
	s32 ret = read_full_file_from_nand(filepath, (void *)(&TMD), &TMD_size);
	
	if (ret >= 0)
	{
		// Try to identify the cIOS by the info put in by the installer/ModMii
		sprintf(filepath, "/title/%08x/%08x/content/%08x.app", 0x00000001, ios_slot, *(u8 *)((u32)TMD+0x1E7));
		ret = read_full_file_from_nand(filepath, &buffer, &filesize);
		
		iosinfo = (iosinfo_t *)(buffer);
		if (ret >= 0 && iosinfo != NULL && iosinfo->magicword == 0x1ee7c105 && iosinfo->magicversion == 1)
		{
			temp_ios_base = iosinfo->baseios;
			temp_ios_revision = iosinfo->version;
	
			if (ios_string != NULL)
			{
				sprintf(ios_string, "%s%uv%u%s (%u)", iosinfo->name, iosinfo->baseios, iosinfo->version, iosinfo->versionstring, ios_slot);				
				// Example: "d2x56v5beta2 (249)"
			}
			if (buffer != 0)
			{
				free(buffer);
			}
		} else
		{	
			// Crappy hash method
			t = (tmd*)SIGNATURE_PAYLOAD(TMD);
			t->title_id = ((u64)(1) << 32) | 249;	// The hashes were made with the cIOS installed as IOS249
			brute_tmd(t);		

			sha1 hash;
			SHA1((u8 *)TMD, TMD_size, hash);;

			for (i = 0;i < info_number;i++)
			{
				if (memcmp((void *)hash, (u32 *)&hashes[i], sizeof(sha1)) == 0)
				{
					switch (i)
					{
						case 0:
						case 1:
						case 3:						
						case 8:						
						case 14:						
						case 25:						
							temp_ios_base = 38;
						break;
						
						case 2:
						case 7:
						case 13:
						case 24:
							temp_ios_base = 37;
						break;

						case 4:
						case 9:
						case 18:
						case 29:
							temp_ios_base = 57;
						break;

						case 5:
						case 10:
						case 19:
						case 31:
							temp_ios_base = 60;
						break;
						
						case 6:
						case 11:
						case 21:
						case 33:
							temp_ios_base = 70;
						break;
						
						case 12:
						case 23:
							temp_ios_base = 36;
						break;

						case 15:
						case 26:
							temp_ios_base = 53;
						break;

						case 16:
						case 27:
							temp_ios_base = 55;
						break;

						case 17:
						case 28:
							temp_ios_base = 56;
						break;

						case 20:
						case 32:
							temp_ios_base = 61;
						break;
						
						case 22:
						case 34:
							temp_ios_base = 80;
						break;

						case 30:
							temp_ios_base = 58;
						break;
					}
					
					if (ios_string != NULL)
					{
						sprintf(ios_string, "%s (%u)", (char *)&infos[i], ios_slot);				
					}
				}
			}
		}
		free(TMD);
	}
	
	if (ios_base != NULL)
	{
		*ios_base = temp_ios_base;
	}
	
	if (ios_revision != NULL)
	{
		*ios_revision = temp_ios_revision;		// Only gets a value if a cIOS is identified
	}
}


char ios_info[64];
int console_cols = 0;

void printheadline()
{
	int rows;
	static bool first_run = true;
	
	// Only access the tmd once...
	if (first_run)
	{	
		identify_IOS(249, NULL, NULL, ios_info);
		
		CON_GetMetrics(&console_cols, &rows);
		first_run = false;
	}

	Print("TriiForce r93");
	s32 nand_device = get_nand_device();
	switch (nand_device)
	{
		case 0:
			Print(" (using real NAND)");
		break;
		case 1:
			Print(" (using SD-NAND)");
		break;
		case 2:
			Print(" (using USB-NAND)");
		break;
	}
	
	// Print the IOS info
	Print("\x1B[%d;%dH", 0, console_cols-strlen(ios_info)-1);	
	Print("%s\n", ios_info);
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
	void *temp;
	temp = memalign(32, (size+31)&(~31) );
	memset(temp, 0, (size+31)&(~31) );
	return temp;
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
		pressed = WPAD_ButtonsDown(0) | WPAD_ButtonsDown(1) | WPAD_ButtonsDown(2) | WPAD_ButtonsDown(3);

		PAD_ScanPads();
		pressedGC = PAD_ButtonsDown(0) | PAD_ButtonsDown(1) | PAD_ButtonsDown(2) | PAD_ButtonsDown(3);

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

s32 Identify_GenerateTik(signed_blob **outbuf, u32 *outlen)
{
	signed_blob *buffer   = NULL;

	sig_rsa2048 *signature = NULL;
	tik         *tik_data  = NULL;

	u32 len;

	/* Set ticket length */
	len = STD_SIGNED_TIK_SIZE;

	/* Allocate memory */
	buffer = (signed_blob *)memalign(32, len);
	if (!buffer)
		return -1;

	/* Clear buffer */
	memset(buffer, 0, len);

	/* Generate signature */
	signature       = (sig_rsa2048 *)buffer;
	signature->type = ES_SIG_RSA2048;

	/* Generate ticket */
	tik_data  = (tik *)SIGNATURE_PAYLOAD(buffer);

	strcpy(tik_data->issuer, "Root-CA00000001-XS00000003");
	memset(tik_data->cidx_mask, 0xFF, 32);

	/* Set values */
	*outbuf = buffer;
	*outlen = len;

	return 0;
}


s32 identify(u64 titleid, u32 *ios)
{
	char filepath[ISFS_MAXPATH] ATTRIBUTE_ALIGN(32);
	u8 *tmdBuffer = NULL;
	u32 tmdSize;
	signed_blob *tikBuffer = NULL;
	u32 tikSize;
	u8 *certBuffer = NULL;
	u32 certSize;
	
	int ret;

	Print("Reading TMD...");
	fflush(stdout);
	
	sprintf(filepath, "/title/%08x/%08x/content/title.tmd", TITLE_UPPER(titleid), TITLE_LOWER(titleid));
	ret = read_full_file_from_nand(filepath, &tmdBuffer, &tmdSize);
	if (ret < 0)
	{
		Print("Reading TMD failed\n");
		return ret;
	}
	Print("done\n");

	*ios = (u32)(tmdBuffer[0x18b]);

	Print("Generating fake ticket...");
	fflush(stdout);
/*
	sprintf(filepath, "/ticket/%08x/%08x.tik", TITLE_UPPER(titleid), TITLE_LOWER(titleid));
	ret = read_file(filepath, &tikBuffer, &tikSize);
	if (ret < 0)
	{
		Print("Reading ticket failed\n");
		free(tmdBuffer);
		return ret;
	}*/
	Identify_GenerateTik(&tikBuffer,&tikSize);
	Print("done\n");

	Print("Reading certs...");
	fflush(stdout);

	sprintf(filepath, "/sys/cert.sys");
	ret = read_full_file_from_nand(filepath, &certBuffer, &certSize);
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

	ret = ES_Identify((signed_blob*)certBuffer, certSize, (signed_blob*)tmdBuffer, tmdSize, tikBuffer, tikSize, NULL);
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


void tell_cIOS_to_return_to_channel()
{
    if (TITLE_UPPER(old_title_id) > 1 && TITLE_LOWER(old_title_id) > 2)	// Don't change anything for system menu or no title id
	{
		static u64 sm_title_id  ATTRIBUTE_ALIGN(32);
		sm_title_id = old_title_id; // title id to be launched in place of the system menu

		int ret;
		
		//TODO if the return to channel is not auto, then it needs to be checked if the title exists,
		//but that's a bit complicated when using emulated nand and returning to real nand
		/*
		signed_blob *buf = NULL;
		u32 filesize;

		ret = GetTMD(sm_title_id, &buf, &filesize);
		if (buf != NULL)
		{
			free(buf);
		}

		if (ret < 0)
		{
			return;
		}*/
		
		static ioctlv vector[0x08] ATTRIBUTE_ALIGN(32);

		vector[0].data = &sm_title_id;
		vector[0].len = 8;

		int es_fd = IOS_Open("/dev/es", 0);
		if (es_fd < 0)
		{
			Print("Couldn't open ES module(2)\n");
			return;
		}
		
		ret = IOS_Ioctlv(es_fd, 0xA1, 1, 0, vector);

		IOS_Close(es_fd);
		
		if (ret < 0)
		{
			//Print("ret = %d\n", ret);
		}
	}
}

