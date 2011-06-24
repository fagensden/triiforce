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

typedef struct _iosinfo_t {
	u32 magicword; //0x1ee7c105
	u32 magicversion; // 1
	u32 version; // Example: 5
	u32 baseios; // Example: 56
	char name[0x10]; // Example: d2x
	char versionstring[0x10]; // Example: beta2
} __attribute__((packed)) iosinfo_t;


bool Power_Flag;
bool Reset_Flag;

void *allocate_memory(u32 size);
s32 identify(u64 titleid, u32 *ios);
void set_highlight(bool highlight);
void waitforbuttonpress(u32 *out, u32 *outGC);
void printheadline();
void set_silent(bool value);
bool get_silent();
void Print(const char *text, ...);
void tell_cIOS_to_return_to_channel();

u64 old_title_id;
