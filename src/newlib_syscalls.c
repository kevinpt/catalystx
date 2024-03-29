#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <malloc.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "build_config.h"

#if USE_NEWLIB_NANO
#  if defined PLATFORM_STM32F1
#    include "stm32f1xx_hal.h"  // For CMSIS __BKPT()
#  else
#    include "stm32f4xx_hal.h"  // For CMSIS __BKPT()
#  endif
#endif

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "cstone/console.h"
#include "cstone/rtc_device.h"

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
int _gettimeofday(struct timeval *tp, void *tzvp);


#ifdef USE_NEWLIB_NANO

//#include <_ansi.h>
//#include "swi.h"

int _getpid(void);

int _getpid(void) { // Called by _getpid_r
  return -1;
}

//__attribute__((__noreturn__))
//int _kill_shared(int, int status, int reason);

//void _exit(int status);

void _exit(int status) {  // Called by abort()
//  _kill_shared (-1, status, ADP_Stopped_ApplicationExit);
  __BKPT(0);
  while(1) {};
}


int _kill (int pid, int sig);

int _kill (int pid, int sig) {  // Called by _kill_r()
//  errno = ENOSYS;
//  return -1;

//  if (sig == SIGABRT)
//    _kill_shared (pid, sig, ADP_Stopped_RunTimeError);
//  else
//    _kill_shared (pid, sig, ADP_Stopped_ApplicationExit);
  __BKPT(0);
  return -1;
}

#endif



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


int _gettimeofday(struct timeval *tp, void *tzvp) {
  struct timezone *zone = tzvp;

  if(tp) {
    time_t now = rtc_get_time(rtc_sys_device());
    tp->tv_sec = now; //millis() / 1000;
    tp->tv_usec = 0;
  }

  if(zone) {
    zone->tz_minuteswest = 0;
    zone->tz_dsttime = 0;
  }
  return 0;
}

