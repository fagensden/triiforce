#include <gccore.h>


typedef struct _dirent
{
	char name[ISFS_MAXPATH + 1];
	int type;
} dirent_t;



s32 read_file(char *filepath, u8 **buffer, u32 *filesize);
s32 getdircount(char *path, u32 *cnt);
s32 getdir(char *path, dirent_t **ent, u32 *cnt);
