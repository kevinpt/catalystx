/* GNU ld linker script for STM32F429ZITx */


/* Code entry point (See startup_stm32f429xx.s)*/
ENTRY(Reset_Handler)

/* Reserve space for C lib heap and main stack.
   Stack only needs to be big enough for init code before FreeRTOS scheduler runs.
   Note that this bloats the RAM usage reported by the linker.
*/
_reserved_heap_size  = 36K;
_reserved_stack_size = 4K;

/* Physical memory regions */
MEMORY {
  /* ISR Vector = ??? bytes */
  ISR_VECTOR (rx) : ORIGIN = 0x8000000, LENGTH = 512
  METADATA   (r)  : ORIGIN = ORIGIN(ISR_VECTOR) + LENGTH(ISR_VECTOR), LENGTH = 256
  FLASH0     (rx) : ORIGIN = ORIGIN(METADATA) + LENGTH(METADATA),
                    LENGTH = 16K - LENGTH(ISR_VECTOR) - LENGTH(METADATA)
  STORAGE0   (rw) : ORIGIN = ORIGIN(FLASH0)+LENGTH(FLASH0), LENGTH = 48K  /* Sectors 1-3 */
  FLASH1   (rx)  : ORIGIN = ORIGIN(STORAGE0)+LENGTH(STORAGE0),
                  LENGTH = 2048K - LENGTH(ISR_VECTOR) - LENGTH(METADATA) - LENGTH(FLASH0) - LENGTH(STORAGE0)
  RAM     (xrw) : ORIGIN = 0x20000000, LENGTH = 192K
  CCMRAM  (rw)  : ORIGIN = 0x10000000, LENGTH = 64K
  SDRAM   (rw)  : ORIGIN = 0xD0000000, LENGTH = 8M
}

/* Stack pointer initialized pointing one byte past end of RAM */
_estack = ORIGIN(RAM) + LENGTH(RAM);


/* Map output sections to memory regions */
SECTIONS {
  /* ******************** FLASH ******************** */

  /* Vector table goes first */
  .isr_vector : {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >ISR_VECTOR

  /* Non-volatile flash filesystem */
  .storage0 (NOLOAD) : {
    . = ALIGN(4);
    _sstorage0 = .;   /* Mark start of flash storage */
    *(.storage0)
    *(.storage0*)
    . = ALIGN(4);
    _estorage0 = .;   /* Mark end of flash storage */
  } >STORAGE0

  /* Executable code */
  .text : {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)      /* ARM to Thumb interworking */
    *(.glue_7t)     /* Thumb to ARM interworking */
    *(.eh_frame)    /* Exception handling */
    KEEP(*(.init))  /* Global constructors */
    KEEP(*(.fini))  /* Global destructors */
    . = ALIGN(4);
    _etext = .;     /* Mark end of code */
  } >FLASH1

  /* Readonly constant data */
  .rodata : {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH1

  /* AAPCS table for stack unwinding, exception handling, etc. */
  .ARM.extab : {
    *(.ARM.extab* .gnu.linkonce.armextab.*)
  } >FLASH1

  /* Index into extab */
  .ARM : {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH1

  /* Table of functions called before .init_array */
  .preinit_array : {
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } >FLASH1

  /* Table of functions in .init section */
  .init_array : {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
  } >FLASH1

  /* Table of functions in .fini section */
  .fini_array : {
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } >FLASH1


  /* ******************** RAM ******************** */

  _sidata = LOADADDR(.data);  /* Move to start of region */

  /* Initialized data to be copied from flash */
  .data : {
    . = ALIGN(4);
    _sdata = .;     /* Mark start of data */
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;     /* Mark end of initialized data*/
  } >RAM AT> FLASH1


  /* Uninitialized data, zeroed in startup */
  .bss : {
    . = ALIGN(4);
    _sbss = .;      /* Mark start of BSS */
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;     /* Mark end of BSS */
    __bss_end__ = _ebss;
  } >RAM

  /* Uninitialized data, untouched by startup */
  .noinit (NOLOAD) : {
    . = ALIGN(4);
    _snoinit = .;      /* Mark start of noinit */
    *(.noinit)
    *(.noinit*)
    . = ALIGN(4);
    _enoinit = .;     /* Mark end of noinit */
  } >RAM

  /* Add reserved space for heap and stack
     Note that the stack is actually located at the end of RAM and there will
     be a gap between heap and stack if any memory is left unallocated. */
  ._heap_and_stack : {
    . = ALIGN(4);
    PROVIDE( end = . ); /* Mark end of data / start of heap */
    PROVIDE( _end = . );
    /* Establish heap boundaries for Newlib _sbrk() syscall */
    __heap_start = .;
    . = . + _reserved_heap_size;
    __heap_end = .;
    /* Add space for stack to force link error if not enough RAM */
    . = . + _reserved_stack_size;
    . = ALIGN(4);
  } >RAM


  /* ******************** SDRAM ******************** */
  _sisdram = LOADADDR(.sdram);  /* Move to start of region */

  .sdram (NOLOAD) : {
    . = ALIGN(4);
    _ssdram = .;      /* Mark start of SDRAM */
    *(.sdram)
    *(.sdram*)
    . = ALIGN(4);
    _esdram = .;     /* Mark end of SDRAM */
  } >SDRAM


  /* ******************** CCMRAM ******************** */
  _siccmram = LOADADDR(.ccmram);  /* Move to start of region */

  /* NO INIT DATA HERE UNLESS STARTUP CODE IS UPDATED */
  .ccmram (NOLOAD) : {
    . = ALIGN(4);
    _sccmram = .;   /* Mark start of CCM */
    *(.ccmram)
    *(.ccmram*)
    . = ALIGN(4);
    _eccmram = .;   /* Mark end of CCM */
  } >CCMRAM


  /* ******************** Misc ******************** */

  /* Ignore any other sections from std libs */
  /DISCARD/ : {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  /* ARM build attributes */
  .ARM.attributes 0 : {
    *(.ARM.attributes)
  }
}

