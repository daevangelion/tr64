/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "engine/save/internal.h"
static int device=-1;
int tr64_save_platform_init(void)
{
 if(device>=0)return TR64_SAVE_OK;
 if(!tr64_save_host_path)return TR64_SAVE_UNAVAILABLE;
 int created=0,fd=open(tr64_save_host_path,O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC,0600);
 if(fd>=0)created=1;
 else if(errno==EEXIST)fd=open(tr64_save_host_path,O_RDWR|O_CLOEXEC);
 if(fd<0)return TR64_SAVE_IO;
 if(created) {
  uint8_t block[4096];memset(block,0xff,sizeof(block));
  for(uint32_t offset=0;offset<TR64_SAVE_BYTES;offset+=sizeof(block)) {
   if(pwrite(fd,block,sizeof(block),offset)!=(ssize_t)sizeof(block)) {
    close(fd);unlink(tr64_save_host_path);return TR64_SAVE_IO;
   }
  }
  if(fsync(fd)) { close(fd);unlink(tr64_save_host_path);return TR64_SAVE_IO; }
 }
 struct stat info;
 if(fstat(fd,&info) || info.st_size!=TR64_SAVE_BYTES) { close(fd);return TR64_SAVE_IO; }
 device=fd;return TR64_SAVE_OK;
}
static int valid(uint32_t offset,const void *data,uint32_t bytes)
{
 return device>=0 && data && bytes && !(offset&1) && !(bytes&1) &&
  !((uintptr_t)data&15) && offset<=TR64_SAVE_BYTES && bytes<=TR64_SAVE_BYTES-offset;
}
int tr64_save_platform_read(uint32_t offset,void *data,uint32_t bytes)
{
 if(!valid(offset,data,bytes))return TR64_SAVE_ARGUMENT;
 return pread(device,data,bytes,offset)==(ssize_t)bytes?TR64_SAVE_OK:TR64_SAVE_IO;
}
int tr64_save_platform_write(uint32_t offset,const void *data,uint32_t bytes)
{
 if(!valid(offset,data,bytes))return TR64_SAVE_ARGUMENT;
 if(pwrite(device,data,bytes,offset)!=(ssize_t)bytes || fsync(device))return TR64_SAVE_IO;
 return TR64_SAVE_OK;
}
