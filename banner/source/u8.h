#ifndef _U8_MODULE
#define _U8_MODULE
 
//void do_U8_archive(u8 *buffer, char *path);
s32 do_file_U8_archive(u8 *buffer, char *filename, u8 **data_out, u32* out_size);
s32 print_names_in_u8(u8 *buffer);
u16 be16(const u8 *p);
u32 be32(const u8 *p);

 
#endif

