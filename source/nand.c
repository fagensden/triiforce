#include <stdio.h>
#include <ogcsys.h>
#include <string.h>

#include "nand.h"
#include "tools.h"

/* Buffer */
static u32 inbuf[8] ATTRIBUTE_ALIGN(32);

static nandDevice ndevList[] = {
	{ "Disable",				0,	0x00,	0x00 },
	{ "SD/SDHC Card",			1,	0xF0,	0xF1 },
	{ "USB 2.0 Mass Storage Device",	2,	0xF2,	0xF3 },
};


s32 Nand_Mount(nandDevice *dev)
{
	s32 fd, ret;

	/* Open FAT module */
	fd = IOS_Open("fat", 0);
	if (fd < 0)
		return fd;

	// TODO Tell the cIOS which partition to use
	
	/* Mount device */
	ret = IOS_Ioctlv(fd, dev->mountCmd, 0, 0, NULL);

	/* Close FAT module */
	IOS_Close(fd);

	return ret;
}

s32 Nand_Unmount(nandDevice *dev)
{
	s32 fd, ret;

	/* Open FAT module */
	fd = IOS_Open("fat", 0);
	if (fd < 0)
		return fd;

	/* Unmount device */
	ret = IOS_Ioctlv(fd, dev->umountCmd, 0, 0, NULL);

	/* Close FAT module */
	IOS_Close(fd);

	return ret;
}

s32 Nand_Enable(nandDevice *dev)
{
	s32 fd, ret;

	/* Open /dev/fs */
	fd = IOS_Open("/dev/fs", 0);
	if (fd < 0)
		return fd;

	memset(inbuf, 0, sizeof(inbuf));

	/* Set input buffer */
	if (IOS_GetRevision() >= 20)
	{
		// New method, fully enable full emulation
		inbuf[0] = dev->mode | 0x100;
	} else
	{
		// Old method
		inbuf[0] = dev->mode;
	}

	/* Enable NAND emulator */
	ret = IOS_Ioctl(fd, 100, inbuf, sizeof(inbuf), NULL, 0);

	/* Close /dev/fs */
	IOS_Close(fd);

	return ret;
} 

s32 Nand_Disable(void)
{
	s32 fd, ret;

	/* Open /dev/fs */
	fd = IOS_Open("/dev/fs", 0);
	if (fd < 0)
		return fd;

	/* Set input buffer */
	inbuf[0] = 0;

	/* Disable NAND emulator */
	ret = IOS_Ioctl(fd, 100, inbuf, sizeof(inbuf), NULL, 0);

	/* Close /dev/fs */
	IOS_Close(fd);

	return ret;
} 

static int mounted = 0;

s32 get_nand_device()
{
	return mounted;
}

s32 Enable_Emu(int selection)
{
	if (mounted != 0) return -1;
	if (selection == 0) return 0;
	
	s32 ret;
	nandDevice *ndev = NULL;
	ndev = &ndevList[selection];
	
	ret = Nand_Mount(ndev);
	if (ret < 0) 
	{
		Print(" ERROR Mount! (ret = %d)\n", ret);
		return ret;
	}

	ret = Nand_Enable(ndev);
	if (ret < 0) 
	{
		Print(" ERROR Enable! (ret = %d)\n", ret);
		Nand_Unmount(ndev);
		return ret;
	}
	
	u32 temp = 0;
	ret = ISFS_ReadDir("/", NULL, &temp);
	if (ret < 0)
	{
		Print("ISFS_ReadDir('/') failed ret = %d. No FAT partition?", ret);
		Nand_Disable();
		Nand_Unmount(ndev);
		return ret;
	}

	mounted = selection;
	return 0;
}	

s32 Disable_Emu()
{
	if (mounted == 0) return 0;
	
	nandDevice *ndev = NULL;
	ndev = &ndevList[mounted];
	
	Nand_Disable();
	Nand_Unmount(ndev);
	
	mounted = 0;
	
	return 0;	
}	
