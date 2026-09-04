SECTION_DATA_PROLOGUE(.picolibc_heap, (NOLOAD),)
{
    . = ALIGN(16);
    __heap_start = .;
    . = ORIGIN(RAM) + LENGTH(RAM);
    __heap_end = .;
} GROUP_LINK_IN(RAM)