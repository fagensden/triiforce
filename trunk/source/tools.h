/*******************************************************************************
 * tools.h
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

#define TITLE_UPPER(x)		((u32)((x) >> 32))
#define TITLE_LOWER(x)		((u32)(x))
#define TITLE_ID(x,y)		(((u64)(x) << 32) | (y))

void *allocate_memory(u32 size);
s32 read_file(char *filepath, u8 **buffer, u32 *filesize);
s32 identify(u64 titleid, u16 *ios);
void set_highlight(bool highlight);
void waitforbuttonpress(u32 *out, u32 *outGC);
void printheadline();
s32 parser(u8 *file, u32 size, u8 *value, u32 valuesize, u8 *patch, u32 patchsize, u32 offset);


