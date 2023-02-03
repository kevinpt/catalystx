/* GNU ld linker script for STM32F401CCUx */


/* Code entry point (See startup_stm32f401xx.s)*/
ENTRY(Reset_Handler)

/* Reserve space for C lib heap and main stack.
   Stack only needs to be big enough for init code before FreeRTOS scheduler runs.
   Note that this bloats the RAM usage reported by the linker.
*/
_reserved_heap_size  = 16K;
_reserved_stack_size = 1K;

/* Physical memory regions */
MEMORY {
  /* ISR Vector = 404 bytes */
  ISR_VECTOR (rx) : ORIGIN = 0x8000000, LENGTH = 512
  METADATA   (r)  : ORIGIN = ORIGIN(ISR_VECTOR) + LENGTH(ISR_VECTOR), LENGTH = 256
  FLASH0     (rx) : ORIGIN = ORIGIN(METADATA) + LENGTH(METADATA),
                    LENGTH = 16K - LENGTH(ISR_VECTOR) - LENGTH(METADATA)
  STORAGE0   (rw) : ORIGIN = ORIGIN(FLASH0)+LENGTH(FLASH0), LENGTH = 48K  /* Sectors 1-3 */
  FLASH1     (rx) : ORIGIN = ORIGIN(STORAGE0)+LENGTH(STORAGE0),
                    LENGTH = 256K - LENGTH(ISR_VECTOR) - LENGTH(METADATA) - LENGTH(FLASH0) - LENGTH(STORAGE0)
  RAM        (xrw) : ORIGIN = 0x20000000, LENGTH = 64K
}

/* Stack pointer initialized pointing one byte past end of RAM */
_estack = ORIGIN(RAM) + LENGTH(RAM);


/* Map output sections to memory regions */
SECTIONS {
  /* ******************** FLASH ******************** */

  /* Vector table goes first */
  .isr_vector : {
    . = ALIGN(4);
    _sivec = .;
    KEEP(*(.isr_vector))
    . = ALIGN(4);
    _eivec = .;
  } >ISR_VECTOR

    /* Metadata at end of flash*/
  /*
  .metadata ORIGIN(FLASH1) + LENGTH(FLASH1) - 128: {
    KEEP(*(.metadata))
    . = ALIGN(128);
  } >FLASH1 = 0x1234
  */

  .metadata : {
    _smeta = .;   /* Mark start of metadata block */
    KEEP(*(.metadata))
    . = ALIGN(4);
    _emeta = .;
  } >METADATA = 0xFF



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
    _sflash1 = .;
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
    _eflash1 = .;
  } >FLASH1



  /* AAPCS table for stack unwinding, exception handling, etc. */
  .ARM.extab : {
    . = ALIGN(4);
    _sflash0 = .;
    *(.ARM.extab* .gnu.linkonce.armextab.*)
  } >FLASH0

  /* Index into extab */
  .ARM : {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH0

  /* Table of functions called before .init_array */
  .preinit_array : {
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } >FLASH0

  /* Table of functions in .init section */
  .init_array : {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
  } >FLASH0

  /* Table of functions in .fini section */
  .fini_array : {
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } >FLASH0


  /* ******************** RAM ******************** */

  _sidata = LOADADDR(.data);  /* ROM address for initialized data */

  /* Initialized data to be copied from flash */
  .data : {
    . = ALIGN(4);
    _sdata = .;     /* Mark start of data */
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;     /* Mark end of initialized data*/
    _eflash0 = LOADADDR(.data) + SIZEOF(.data);
  } >RAM AT> FLASH0


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

