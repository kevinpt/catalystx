#ifndef LOG_EVFS_H
#define LOG_EVFS_H


#ifdef __cplusplus
extern "C" {
#endif

void log_evfs_erase_sector(void *ctx, size_t sector_start, size_t sector_size);
bool log_evfs_read_block(void *ctx, size_t block_start, uint8_t *dest, size_t block_size);
bool log_evfs_write_block(void *ctx, size_t block_start, uint8_t *src, size_t block_size);

#ifdef __cplusplus
}
#endif

#endif // LOG_EVFS_H
