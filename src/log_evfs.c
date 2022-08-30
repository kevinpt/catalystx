#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "cstone/log_db.h"
#include "cstone/log_info.h"
#include "log_evfs.h"

#include "util/minmax.h"

#include "evfs.h"

// ******************** Log DB EVFS callbacks ********************


void log_evfs_erase_sector(void *ctx, size_t sector_start, size_t sector_size) {
  EvfsFile *fh = (EvfsFile *)ctx;

  if(evfs_file_seek(fh, (evfs_off_t)sector_start, EVFS_SEEK_TO) != EVFS_OK)
    return;

  uint8_t buf[64];
  size_t write_bytes;

  memset(buf, 0xFF, sizeof(buf));

  while(sector_size > 0) {
    write_bytes = min(sizeof(buf), sector_size);
    write_bytes = evfs_file_write(fh, buf, write_bytes);
    if(write_bytes <= 0)
      break;
    sector_size -= write_bytes;
  }

  evfs_file_sync(fh);
}


bool log_evfs_read_block(void *ctx, size_t block_start, uint8_t *dest, size_t block_size) {
  EvfsFile *fh = (EvfsFile *)ctx;

  if(evfs_file_seek(fh, (evfs_off_t)block_start, EVFS_SEEK_TO) != EVFS_OK)
    return false;

  ptrdiff_t read_bytes = evfs_file_read(fh, dest, block_size);
  if(read_bytes <= 0)
    return false;

  return true;
}


bool log_evfs_write_block(void *ctx, size_t block_start, uint8_t *src, size_t block_size) {
  EvfsFile *fh = (EvfsFile *)ctx;

  if(evfs_file_seek(fh, (evfs_off_t)block_start, EVFS_SEEK_TO) != EVFS_OK)
    return false;

  ptrdiff_t write_bytes = evfs_file_write(fh, src, block_size);
  if(write_bytes <= 0)
    return false;

  evfs_file_sync(fh);
  return true;
}



