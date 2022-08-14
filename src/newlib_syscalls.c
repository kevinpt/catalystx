#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <malloc.h>
#include <sys/unistd.h>
#include <sys/stat.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "cstone/console.h"

#ifdef DEBUG_CTRL_PRINT
#  include <ctype.h>
#  include "cstone/term_color.h"
#endif

#ifndef COUNT_OF
#  define COUNT_OF(a) (sizeof(a) / sizeof(*(a)))
#endif


// Prototypes to satisfy -Wmissing-prototypes
int _read(int file, char *data, int len);
int _write(int file, char *data, int len);
int _close(int file);
int _lseek(int file, int ptr, int dir);
int _fstat(int file, struct stat *st);
int _isatty(int file);
void *_sbrk(int incr);


int _read(int file, char *data, int len) {
  if(file != STDIN_FILENO) {
    errno = EBADF;
    return -1;
  }

  Console *con = active_console();
  if(con)
    return console_rx_unqueue(con, (uint8_t *)data, len);
  else
    return 0;
}


int _write(int file, char *data, int len) {
  if((file != STDOUT_FILENO) && (file != STDERR_FILENO)) {
    errno = EBADF;
    return -1;
  }

  // Copy to queue
  Console *con = active_console();
  if(!con)
    return len; // Report everything was written

#ifndef DEBUG_CTRL_PRINT

  console_send(con, (uint8_t *)data, len);
  return len; // Report everything was written

#else
  // For debugging non-printable byte output
  for(int i = 0; i < len; i++) {
    char ch = data[i];
    if(isprint(ch) || ch == '\n' || ch == '\r' || ch == '\t' || ch == 033) {
      console_send(con, (uint8_t *)&data[i], 1);

    } else { // Unsupported control chars (Including UTF-8 bytes)
      static const char ctrl_pfx[] = A_BRED "[";
      static const char ctrl_sfx[] = "]" A_NONE;

      // Prefix
      for(size_t j = 0; j < COUNT_OF(ctrl_pfx)-1; j++)
        console_send(con, (uint8_t *)&ctrl_pfx[i], 1);

      // Upper nibble
      char hex = (ch >> 4);
      hex = (hex < 10)  ? hex + 0x30 : hex - 10 + 0x61;
      console_send(con, (uint8_t *)&hex, 1);

      // Lower nibble
      hex = ch & 0x0F;
      hex = (hex < 10)  ? hex + 0x30 : hex - 10 + 0x61;
      console_send(con, (uint8_t *)&hex, 1);

      // Suffix
      for(size_t j = 0; j < COUNT_OF(ctrl_sfx)-1; j++)
        console_send(con, (uint8_t *)&ctrl_sfx[i], 1);
    }
  }
  return len;
#endif
}


int _close(int file) {
  return -1;
}

int _lseek(int file, int ptr, int dir) {
  return 0;
}

int _fstat(int file, struct stat *st) {
  st->st_mode = S_IFCHR;
  return 0;
}

int _isatty(int file) {
  if((file == STDOUT_FILENO) || (file == STDIN_FILENO) || (file == STDERR_FILENO)) {
    return 1;
  }

  errno = EBADF;
  return 0;
}

// NOTE: We only need malloc() locking when the scheduler is running.
void __malloc_lock(struct _reent *r)   {
  if(xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    vTaskSuspendAll();
  //taskENTER_CRITICAL();
};

void __malloc_unlock(struct _reent *r) {
  if(xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    xTaskResumeAll();
  //taskEXIT_CRITICAL();
};



void *_sbrk(int incr) {
  // Linker symbols
  extern char __heap_start;
  extern char __heap_end;

  static char *heap_break = NULL;
  char        *next_block;

  if(!heap_break)
    heap_break = &__heap_start;

  next_block = heap_break;
  heap_break += incr;

  if(heap_break >= &__heap_end) {
    errno = ENOMEM;
    return (void *)-1;
  }

  return (void *)next_block;
}


