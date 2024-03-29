/* GNU ld linker script for STM32F429ZITx */


/* Code entry point (See startup_stm32f429xx.s)*/
ENTRY(Reset_Handler)

/* Reserve space for C lib heap and main stack.
   Stack only needs to be big enough for init code before FreeRTOS scheduler runs.
   Note that this bloats the RAM usage reported by the linker.
*/
_reserved_heap_size  = 7K + 400;
_reserved_stack_size = 512;

/* Physical memory regions */
MEMORY {
  ISR_VECTOR (rx) : ORIGIN = 0x8000000,  LENGTH = 1K /* 268 bytes padded to page size */
  STORAGE0   (rw) : ORIGIN = ORIGIN(ISR_VECTOR)+LENGTH(ISR_VECTOR), LENGTH = 4K
  FLASH      (rx) : ORIGIN = ORIGIN(STORAGE0)+LENGTH(STORAGE0), LENGTH = 128K - LENGTH(ISR_VECTOR) - LENGTH(STORAGE0)
  RAM       (xrw) : ORIGIN = 0x20000000, LENGTH = 20K
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
  } >FLASH

  /* Readonly constant data */
  .rodata : {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  /* AAPCS table for stack unwinding, exception handling, etc. */
  .ARM.extab : {
    *(.ARM.extab* .gnu.linkonce.armextab.*)
  } >FLASH

  /* Index into extab */
  .ARM : {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  /* Table of functions called before .init_array */
  .preinit_array : {
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } >FLASH

  /* Table of functions in .init section */
  .init_array : {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
  } >FLASH

  /* Table of functions in .fini section */
  .fini_array : {
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } >FLASH


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
  } >RAM AT> FLASH


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

