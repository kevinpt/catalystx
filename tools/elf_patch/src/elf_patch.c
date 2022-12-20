/* SPDX-License-Identifier: MIT
Copyright 2022 Kevin Thibedeau
(kevin 'period' thibedeau 'at' gmail 'punto' com)

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice (including the next
paragraph) shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

/*
------------------------------------------------------------------------------
Utility program to apply and verify CRCs in STM32 ELF files built from
Catalyst codebase
------------------------------------------------------------------------------
*/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <libelf.h>
#include <gelf.h>

#include "build_config.h"

#include "util/getopt_r.h"
#include "util/glob.h"
#include "util/range_strings.h"
#include "util/crc16.h"
#include "util/crc32.h"
#include "util/hex_dump.h"

#include "cstone/obj_metadata.h"
#include "cstone/term_color.h"


typedef struct {
  Addr32  lma_s;
  Addr32  vma_s;
  size_t  size;
} ProgSegment;

typedef struct {
  size_t count;
  ProgSegment segments[];
} SegmentIndex;


enum Error {
  ERR_BAD_ARG = 1,
  ERR_MISSING_INPUT,
  ERR_FILE_ACCESS,
  ERR_ALLOC,
  ERR_VERIFY
};


void path_basename(const char *path, StringRange *tail) {
  const char *end = (char *)path + strlen(path);
  const char *pos = end - 1;

#define PATH_SEPS "/\\"

  // Look for rightmost delimiter
  while(pos > path && !char_match(*pos, PATH_SEPS)) // Skip basename
    pos--;

  if(char_match(*pos, PATH_SEPS))
    pos++;

  range_init(tail, pos, end-pos);
}


void path_extname(const char *path, StringRange *ext) {
  // Get the file name
  path_basename(path, ext);
  const char *pos = ext->end;

  if(range_size(ext) > 0) {
    // Scan back for first period
    while(pos > ext->start) {
      if(*pos == '.')
        break;
      pos--;
    }
  }

  if(pos > ext->start) { // Extension found
    ext->start = pos;
  } else { // Return empty range
    ext->start = ext->end;
  }
}


bool file_exists(const char *path) {
  struct stat fs;
  return stat(path, &fs) == 0;
}



bool copy_file(const char *src, const char *dest) {
  int sfd = open(src, O_RDONLY);
  if(!sfd) return false;

  int dfd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if(!dfd) {
    close(sfd);
    return false;
  }

  bool rval = true;
  char buf[1024*16];
  ssize_t nread;
  while((nread = read(sfd, buf, sizeof buf)) > 0) {
    if(write(dfd, buf, nread) <= 0) {
      rval = false;
      break;
    }
  }

  close(sfd);
  close(dfd);
  return rval;
}


const char *prog_hdr_type(size_t p_type) {
#define PH_NAME(name)   case PT_##name: return #name; break
  switch(p_type) {
    PH_NAME(NULL);
    PH_NAME(LOAD);
    PH_NAME(DYNAMIC);
    PH_NAME(INTERP);
    PH_NAME(NOTE);
    PH_NAME(SHLIB);
    PH_NAME(PHDR);
    PH_NAME(TLS);
    PH_NAME(SUNWBSS);
    PH_NAME(SUNWSTACK);
    default:  return "Unknown"; break;
  }
}


Elf_Scn *get_named_section(Elf *elf, const char *name) {
  size_t string_table_ix;
  elf_getshdrstrndx(elf, &string_table_ix);

  Elf_Scn *scn = NULL;  // Begin at first section
  GElf_Shdr shdr;
  char *sec_name;

  while((scn = elf_nextscn(elf, scn)) != NULL) {
    if(gelf_getshdr(scn, &shdr) == &shdr) {
      if((sec_name = elf_strptr(elf, string_table_ix, shdr.sh_name)) != NULL) {
        if(!strcmp(sec_name, name))
          return scn;
      }
    }
  }

  return NULL;
}


bool firmware_crc_from_bin(const char *bin_file, ObjMemRegion *regions, uint32_t *crc) {
  *crc = 0;

  int fd = open(bin_file, O_RDONLY);
  if(fd <= 0) return false;
  uint8_t *bin_data;
  struct stat fs;
  fstat(fd, &fs);

  bin_data = mmap(NULL, fs.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if(!bin_data) return false;

  uint32_t elf_crc = crc32_init();
  for(int i = 0; i < OBJ_MAX_REGIONS; i++) {
    ObjMemRegion *cur = &regions[i];
    if(cur->end - cur->start == 0)
      break;

#define STM32_FLASH_OFF   0x8000000
//    printf("## cur: %08X - %08X\n", cur->start, cur->end);
    uint8_t *block = &bin_data[cur->start - STM32_FLASH_OFF];
    elf_crc = crc32_update_small_stm32(elf_crc, block, cur->end - cur->start);
//    printf("## elf_crc: 0x%08X\n", elf_crc);
  }
  
  *crc = crc32_finish(elf_crc);

  munmap(bin_data, fs.st_size);
  return true;
}



void firmware_version(uint32_t encoded, int *major, int *minor, int *patch) {
  *patch = encoded % 100;
  encoded /= 100;
  *minor = encoded % 100;
  *major = encoded / 100;
}


size_t build_segment_index(Elf *elf, SegmentIndex **seg_index) {
  // Index program headers to map VMA to LMA
  size_t prog_hdr_count;
  elf_getphdrnum(elf, &prog_hdr_count);

  SegmentIndex *si = malloc(sizeof(SegmentIndex) + prog_hdr_count * sizeof(ProgSegment));

  for(size_t i = 0; i < prog_hdr_count; i++) {
    GElf_Phdr phdr;
    if(gelf_getphdr(elf, i, &phdr) == &phdr) {
      si->segments[i] = (ProgSegment){.lma_s= phdr.p_paddr, .vma_s=phdr.p_vaddr,
                                      .size=phdr.p_memsz};
    }
  }

  si->count = prog_hdr_count;
  *seg_index = si;
  return prog_hdr_count;
}


// Convert VMA to LMA using data from program header
Addr32 get_lma(Addr32 vma, SegmentIndex *seg_index) {
  for(size_t i = 0; i < seg_index->count; i++) {
    ProgSegment *cur = &seg_index->segments[i];
    Addr32 vma_e = cur->vma_s + cur->size;
    if(cur->vma_s <= vma && vma < vma_e)
      return (vma - cur->vma_s) + cur->lma_s;
  }

  return 0;
}


// Get section name that contains an LMA
const char *find_lma_section(Elf *elf, Addr32 lma, SegmentIndex *seg_index, size_t *offset) {
  size_t string_table_ix;
  elf_getshdrstrndx(elf, &string_table_ix);


  // Read sections
  Elf_Scn *scn = NULL;
  GElf_Shdr shdr;
  char *name;
  while((scn = elf_nextscn(elf, scn)) != NULL) {
    if(gelf_getshdr(scn, &shdr) == &shdr) {
      if((name = elf_strptr(elf, string_table_ix, shdr.sh_name)) != NULL) {
        Addr32 sec_lma = get_lma(shdr.sh_addr, seg_index);
        if(shdr.sh_size > 0 && sec_lma <= lma && lma < (sec_lma + shdr.sh_size)) {
          *offset = lma - sec_lma;
          return name;
        }
      }
    }
  }

  *offset = 0;
  return NULL;
}


// Get pointer to ELF section data for an LMA
uint8_t *get_lma_block(Elf *elf, Addr32 lma, SegmentIndex *seg_index, size_t *block_size) {

  size_t offset = 0;

  const char *sname = find_lma_section(elf, lma, seg_index, &offset);
  if(!sname) {
    *block_size = 0;
    return NULL;
  }

  Elf_Scn *scn = get_named_section(elf, sname);

  Elf_Data *scn_data = NULL;
  scn_data = elf_getdata(scn, scn_data);
  
  uint8_t *block = &((uint8_t *)scn_data->d_buf)[offset];
  *block_size = scn_data->d_size - offset;
  return block;
}


bool firmware_crc_from_elf(Elf *elf, ObjMemRegion *regions, SegmentIndex *seg_index, uint32_t *crc) {
  Addr32 mr_lma_pos;
  size_t mr_remain;
  uint32_t elf_crc = crc32_init();

  *crc = 0;

  for(int i = 0; i < OBJ_MAX_REGIONS; i++) {
    ObjMemRegion *cur = &regions[i];
    if(cur->end - cur->start == 0)  // No more regions
      break;

    mr_lma_pos = cur->start;
    mr_remain = cur->end - cur->start;
    while(mr_lma_pos < cur->end) {
      size_t block_size;
      uint8_t *block = get_lma_block(elf, mr_lma_pos, seg_index, &block_size);
      if(!block)
        return false;
      size_t crc_block_size = mr_remain < block_size ? mr_remain : block_size;
      elf_crc = crc32_update_small_stm32(elf_crc, block, crc_block_size);
      mr_remain -= block_size;
      mr_lma_pos += block_size;
    }

//    printf("## cur: %08X - %08X\n", cur->start, cur->end);
//    printf("## elf_crc: 0x%08X\n", elf_crc);
  }

  *crc = crc32_finish(elf_crc);
  return true;
}


int generate_crcs(const char *elf_file, const char *bin_file, bool verbose, bool apply_update,
                  bool strip_crc, bool colorize) {
  int rval = 0;

  int fd = open(elf_file, apply_update ? O_RDWR : O_RDONLY);
  if(!fd) return ERR_FILE_ACCESS;

  Elf *elf = elf_begin(fd, apply_update ? ELF_C_RDWR_MMAP : ELF_C_READ_MMAP, NULL);
  if(!elf) {
    close(fd);
    return ERR_FILE_ACCESS;
  }

  elf_flagelf(elf, ELF_C_SET, ELF_F_LAYOUT);  // https://stackoverflow.com/questions/35949369/libelf-corrupts-arm-elf-binaries

  Elf_Scn *meta_scn = get_named_section(elf, ".metadata");
  if(!meta_scn) { rval = ERR_FILE_ACCESS; goto cleanup; }

  Elf_Data *meta_data = elf_getdata(meta_scn, NULL);
  if(!meta_data) { rval = ERR_FILE_ACCESS; goto cleanup; }

  ObjectMetadata *obj_meta = (ObjectMetadata *)meta_data->d_buf;

  // Check firmware CRC
  uint32_t obj_crc;
  if(bin_file) {
    firmware_crc_from_bin(bin_file, obj_meta->mem_regions, &obj_crc);

  } else {  // Generate CRC directly from ELF sections
    SegmentIndex *seg_index;
    build_segment_index(elf, &seg_index);
    firmware_crc_from_elf(elf, obj_meta->mem_regions, seg_index, &obj_crc);
    free(seg_index);
  }

  // Check metadata CRC
#define FIELD_SIZE(t, f)  sizeof(((t *)0)->f)
  uint16_t meta_crc = crc16_init();
  // Skip over initial CRCs in metadata struct
  size_t meta_offset = offsetof(ObjectMetadata, meta_crc) + FIELD_SIZE(ObjectMetadata, meta_crc);
  meta_crc = crc16_update_block(meta_crc, meta_data->d_buf + meta_offset,
                                meta_data->d_size - meta_offset);
  meta_crc = crc16_finish(meta_crc);


  if(apply_update) {
    if(!strip_crc) {
      printf("Updating CRCs in %s\n", elf_file);
      obj_meta->obj_crc = obj_crc;
      obj_meta->meta_crc = meta_crc;

    } else {
      printf("Removing CRCs in %s\n", elf_file);
      obj_meta->obj_crc = 0;
      obj_meta->meta_crc = 0;
    }

    elf_flagdata(meta_data, ELF_C_SET, ELF_F_DIRTY);
    elf_end(elf); // Commit updates

    // Reread the metadata block for confirmation
    elf = elf_begin(fd, ELF_C_READ_MMAP, NULL);

    meta_scn = get_named_section(elf, ".metadata");
    meta_data = elf_getdata(meta_scn, NULL);
    obj_meta = (ObjectMetadata *)meta_data->d_buf;
  }


  if(verbose) {
    puts("Metadata:");
    dump_array((uint8_t *)obj_meta, meta_data->d_size);
  }

  // Report info from metadata
  int major, minor, patch;
  firmware_version(obj_meta->obj_version, &major, &minor, &patch);
  printf("\nApplication: %s  %d.%d.%d  Git SHA: %08x", obj_meta->obj_name,
          major, minor, patch, obj_meta->git_sha);

#define COLORIZE(str, color, enable)  (enable) ? (color str A_NONE) : (str)
  printf("%s\n", obj_meta->debug_build ? COLORIZE("  DEBUG", A_RED, colorize) : "");

#define CHAR_PASS   u8"✓"
#define CHAR_FAIL   u8"✗"

  printf("   App CRC: 0x%08X", obj_crc);
  printf(" %s\n", obj_crc == obj_meta->obj_crc ? COLORIZE(CHAR_PASS, A_BGRN, colorize) :
                                                 COLORIZE(CHAR_FAIL, A_BRED, colorize));
//  printf("\tGot: 0x%08X\n", obj_meta->obj_crc);

  printf("  Meta CRC: 0x%04X", meta_crc);
  printf(" %s\n", meta_crc == obj_meta->meta_crc ? COLORIZE(CHAR_PASS, A_BGRN, colorize) :
                                                 COLORIZE(CHAR_FAIL, A_BRED, colorize));
//  printf("\tGot: 0x%04X\n", obj_meta->meta_crc);

  if(obj_crc != obj_meta->obj_crc || meta_crc != obj_meta->meta_crc) rval = ERR_VERIFY;

cleanup:
  elf_end(elf);
  close(fd);
  return rval;
}

void show_help(char *argv[]) {
  StringRange app_name;
  path_basename(argv[0], &app_name);
  printf(A_BOLD "Usage: " A_NONE
        "%"PRISR" -i <ELF file> [-o <ELF file>] [-b <BIN file>] [-c] [-s] [-v] [-V] [-h]\n",
        RANGE_FMT(&app_name));
  puts(" Add CRCs to an ELF firmware image");
  puts("  -i <ELF file>   Input firmware image");
  puts("  -o <ELF file>   Output image");
  puts("  -b <BIN file>   Binary firmware image");
  puts("  -c              Check ELF file only");
  puts("  -s              Strip CRCs in output");
  puts("  -v              Verbose output");
  puts("  -V              Report version");
  puts("  -h              Show help");

  printf("\n" A_BOLD "Example:\n" A_NONE
         "  %"PRISR" -i firmware.elf                # Generate CRCs in firmware.elf\n",
          RANGE_FMT(&app_name));
  printf("  %"PRISR" -i firmware.elf -o fw_crc.elf  # Same with new image \n", RANGE_FMT(&app_name));
  printf("  %"PRISR" -i firmware.elf -c -v          # Only check CRCs with detailed metadata\n", RANGE_FMT(&app_name));
}

int main(int argc, char *argv[]) {
  int status = 0;

  if(argc == 1) {
    show_help(argv);
    return 0;
  }

  GetoptState state = {0};
  state.report_errors = true;
  int c;

  char *bin_file_buf = NULL;
  const char *bin_file = NULL;
  const char *elf_file = NULL;
  const char *out_file = NULL;
  bool check_elf = false;
  bool strip_crc = false;
  bool verbose = false;
  bool show_version = false;

  while((c = getopt_r(argv, "b:i:o:csvVh", &state)) != -1) {
    switch(c) {
    case 'b': bin_file = state.optarg; break;
    case 'i': elf_file = state.optarg; break;
    case 'o': out_file = state.optarg; break;
    case 'c': check_elf = true; break;
    case 's': strip_crc = true; break;
    case 'v': verbose = true; break;
    case 'V': show_version = true; break;

    case 'h':
      show_help(argv);
      return 0;
      break;

    default:
    case ':':
    case '?':
      return ERR_BAD_ARG;
      break;
    }
  }
  
  if(show_version) {
    printf("%s\n", APP_VERSION);
    return 0;
  }

  if(!elf_file) {
    puts("Missing required input file");
    return ERR_MISSING_INPUT;
  }

#if 0
  if(!bin_file) {
    size_t buf_len = strlen(elf_file)+4+1;
    bin_file_buf = malloc(buf_len);
    if(!bin_file_buf)
      return ERR_ALLOC;

    // Replace .elf extension with .bin
    strcpy(bin_file_buf, elf_file);

    StringRange ext;
    path_extname(bin_file_buf, &ext);
    AppendRange bin_r;
    range_init(&bin_r, (char *)ext.start, buf_len - (ext.start - bin_file_buf));
    range_cat_str(&bin_r, ".bin");

    bin_file = bin_file_buf;
  }
#endif

  if(out_file && strcmp(elf_file, out_file) != 0 && !check_elf) { // Copy input file
    if(!copy_file(elf_file, out_file))
      return ERR_FILE_ACCESS;
    elf_file = out_file;
  }


  if(!file_exists(elf_file)) {
    printf("Missing ELF file '%s'\n", elf_file);
    return ERR_MISSING_INPUT;
  }

  if(!bin_file || !file_exists(bin_file))
    bin_file = NULL;

  printf("Using %s for CRC\n", bin_file ? "BIN" : "ELF");

  elf_version(EV_CURRENT);  // Prepare libelf


  // Color output for TTYs only
  bool colorize = isatty(1);

  char *no_color = getenv("NO_COLOR");
  if(no_color && no_color[0] != '\0')
    colorize = false;

  status = generate_crcs(elf_file, bin_file, verbose, /*apply_update*/!check_elf, strip_crc, colorize);


  if(bin_file_buf)
    free(bin_file_buf);

  return status;
}
