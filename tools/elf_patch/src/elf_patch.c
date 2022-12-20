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


#define CHAR_PASS   A_BGRN u8"✓" A_NONE
#define CHAR_FAIL   A_BRED u8"✗" A_NONE

int generate_crcs(const char *elf_file, const char *bin_file, bool verbose, bool apply_update) {
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
    puts("Updating CRCs");
    obj_meta->obj_crc = obj_crc;
    obj_meta->meta_crc = meta_crc;
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
  printf("\nApplication: %s  %d.%d.%d  Git SHA: %08x%s\n", obj_meta->obj_name,
          major, minor, patch, obj_meta->git_sha,
          obj_meta->debug_build ? A_RED "  DEBUG" A_NONE : "");

  printf("   App CRC: 0x%08X %s\n", obj_crc, obj_crc == obj_meta->obj_crc ? CHAR_PASS : CHAR_FAIL);
//  printf("\tGot: 0x%08X\n", obj_meta->obj_crc);

  printf("  Meta CRC: 0x%04X %s\n", meta_crc, meta_crc == obj_meta->meta_crc ? CHAR_PASS : CHAR_FAIL);
//  printf("\tGot: 0x%04X\n", obj_meta->meta_crc);

  if(obj_crc != obj_meta->obj_crc || meta_crc != obj_meta->meta_crc) rval = ERR_VERIFY;

cleanup:
  elf_end(elf);
  close(fd);
  return rval;
}


int main(int argc, char *argv[]) {
  int status = 0;

  GetoptState state = {0};
  state.report_errors = true;
  int c;

  char *bin_file_buf = NULL;
  const char *bin_file = NULL;
  const char *elf_file = NULL;
  const char *out_file = NULL;
  bool check_elf = false;
  bool verbose = false;
  bool show_version = false;

  while((c = getopt_r(argv, "b:i:o:cvVh", &state)) != -1) {
    switch(c) {
    case 'b': bin_file = state.optarg; break;
    case 'i': elf_file = state.optarg; break;
    case 'o': out_file = state.optarg; break;
    case 'c': check_elf = true; break;
    case 'v': verbose = true; break;
    case 'V': show_version = true; break;

    case 'h': {
      StringRange app_name;
      path_basename(argv[0], &app_name);
      printf("Usage: %"PRISR" -i <ELF file> [-o <ELF file>] [-b <BIN file>] [-c] [-v] [-V] [-h]\n",
            RANGE_FMT(&app_name));
      puts(" Add CRCs to an ELF firmware image");
      puts("  -i <ELF file>   Input firmware image");
      puts("  -o <ELF file>   Output image");
      puts("  -b <BIN file>   Binary firmware image");
      puts("  -c              Check ELF file only");
      puts("  -v              Verbose output");
      puts("  -V              Report version");
      puts("  -h              Show help");
      return 0;
      }
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


  if(out_file && strcmp(elf_file, out_file) != 0) { // Copy input file
    if(!copy_file(elf_file, out_file))
      return ERR_FILE_ACCESS;
    elf_file = out_file;
  }


  if(!file_exists(elf_file)) {
    printf("Missing ELF file '%s'\n", elf_file);
    return ERR_MISSING_INPUT;
  }

  if(!file_exists(bin_file)) {
    printf("No BIN file '%s'. Using ELF for CRC.\n", bin_file);
    bin_file = NULL;
    //return ERR_MISSING_INPUT;
  }


  elf_version(EV_CURRENT);  // Prepare libelf


#if 0
  int fd = open(elf_file, O_RDWR);
  if(!fd)
    return ERR_FILE_ACCESS;


  Elf *elf = elf_begin(fd, ELF_C_RDWR_MMAP, NULL);
  if(elf) {
    elf_flagelf(elf, ELF_C_SET, ELF_F_LAYOUT);  // https://stackoverflow.com/questions/35949369/libelf-corrupts-arm-elf-binaries

#  if 0    
    puts("Got ELF");
    Elf_Kind k = elf_kind(elf);
    printf("Kind: %d\n", k);
    if(k == ELF_K_ELF) {
      size_t section_count, string_table_ix;
      elf_getshdrnum(elf,  &section_count);
      elf_getshdrstrndx(elf, &string_table_ix);
      printf("Sections: %d\n", section_count);
      printf("String table: %d\n", string_table_ix);
      
      size_t prog_hdr_count;
      elf_getphdrnum(elf, &prog_hdr_count);
      for(size_t i = 0; i < prog_hdr_count; i++) {
        GElf_Phdr phdr;
        if(gelf_getphdr(elf, i, &phdr) == &phdr) {
          printf("PHDR: %d %s\n", i, prog_hdr_type(phdr.p_type));
#define PRINT_FIELD(field)  printf("\t%s: 0x%jX  (%d)\n", #field, phdr.field, phdr.field);
          PRINT_FIELD(p_type);
          PRINT_FIELD(p_vaddr);
          PRINT_FIELD(p_paddr);
          PRINT_FIELD(p_filesz);
          PRINT_FIELD(p_memsz);
          fputs("\tflags: ", stdout);
          if(phdr.p_flags & PF_X) fputs("X", stdout);
          if(phdr.p_flags & PF_R) fputs("R", stdout);
          if(phdr.p_flags & PF_W) fputs("W", stdout);
          puts("");
        }
      }

      // Read sections
      Elf_Scn *scn = NULL;
      GElf_Shdr shdr;
      char *name;
      //Elf_Data *data;
      while((scn = elf_nextscn(elf, scn)) != NULL) {
        if(gelf_getshdr(scn, &shdr) == &shdr) {
          if((name = elf_strptr(elf, string_table_ix, shdr.sh_name)) != NULL) {
            printf("Section %4jd %s\tsize= %d\n" , ( uintmax_t )elf_ndxscn(scn), name, shdr.sh_size);
          }
        }
      }
#  endif
      Elf_Scn *meta_scn = get_named_section(elf, ".metadata");
      if(meta_scn) {
//        GElf_Shdr meta_hdr;
//        gelf_getshdr(meta_scn, &meta_hdr);
//        printf(".metadata size= %d\n", meta_hdr.sh_size);
        Elf_Data *meta_data = NULL;
//        while((meta_data = elf_getdata(meta_scn, meta_data))) {
//          dump_array(meta_data->d_buf, meta_data->d_size);
//        }

//        meta_data = NULL;
        meta_data = elf_getdata(meta_scn, meta_data);
        ObjectMetadata *obj_meta = (ObjectMetadata *)meta_data->d_buf;
        //memcpy(&obj_meta, meta_data->d_buf, meta_data->d_size);
        //dump_array((uint8_t *)obj_meta, meta_data->d_size);

/*
        int major, minor, patch;
        firmware_version(obj_meta->obj_version, &major, &minor, &patch);
        printf("Obj name: %s, ver: %d.%d.%d sz: %d\n", obj_meta->obj_name,
                major, minor, patch, sizeof obj_meta);
        puts("Regions:");
        int region = 0;
        for(int i = 0; i < OBJ_MAX_REGIONS; i++) {
          if(obj_meta->mem_regions[i].end - obj_meta->mem_regions[i].start == 0)
            break;
            
          printf("\ts: %08X,  e: %08X,  sz: %d\n", obj_meta->mem_regions[i].start,
                obj_meta->mem_regions[i].end,
                obj_meta->mem_regions[i].end - obj_meta->mem_regions[i].start);
        }
*/
        if(!firmware_crc(bin_file, obj_meta->mem_regions, &obj_meta->obj_crc))
          status = ERR_FILE_ACCESS;
        printf("CRC32: 0x%08X\n", obj_meta->obj_crc);

        uint16_t meta_crc = crc16_init();
        // Skip over initial CRCs in metadata struct
        size_t meta_offset = offsetof(ObjectMetadata, meta_crc) + FIELD_SIZE(ObjectMetadata, meta_crc);
        meta_crc = crc16_update_block(meta_crc, meta_data->d_buf + meta_offset,
                                      meta_data->d_size - meta_offset);
        meta_crc = crc16_finish(meta_crc);
        printf("CRC16: 0x%04X\n", meta_crc);
        obj_meta->meta_crc = meta_crc;
        elf_flagdata(meta_data, ELF_C_SET, ELF_F_DIRTY);
      }
    }



    elf_end(elf);
//  } else {
//    puts(elf_errmsg(-1));
//  }

  close(fd);

#else
  status = generate_crcs(elf_file, bin_file, verbose, /*apply_update*/!check_elf);
#endif

#if 0
  int fd = open(elf_file, O_RDWR);
  Elf *elf = elf_begin(fd, ELF_C_RDWR_MMAP, NULL);
  elf_flagelf(elf, ELF_C_SET, ELF_F_LAYOUT);
  Elf_Scn *meta_scn = get_named_section(elf, ".metadata");
  Elf_Data *meta_data = elf_getdata(meta_scn, NULL);
  ObjectMetadata *obj_meta = (ObjectMetadata *)meta_data->d_buf;


  // Index program headers to map VMA to LMA
  SegmentInfo *seg_index;
  size_t prog_hdr_count = build_segment_index(elf, &seg_index);


  puts("Regions:");
  int region = 0;
  for(int i = 0; i < OBJ_MAX_REGIONS; i++) {
    if(obj_meta->mem_regions[i].end - obj_meta->mem_regions[i].start == 0)  // No more regions
      break;

    ObjMemRegion *mr = &obj_meta->mem_regions[i];
    size_t offset;
    const char *sname = find_lma_section(elf, mr->start, seg_index, prog_hdr_count, &offset);

    printf("\ts: %08X,  e: %08X,  sz: %d,  %s\n", obj_meta->mem_regions[i].start,
          obj_meta->mem_regions[i].end,
          obj_meta->mem_regions[i].end - obj_meta->mem_regions[i].start,
          sname);
  }


  puts("Sections:");
  size_t string_table_ix;
  elf_getshdrstrndx(elf, &string_table_ix);

  // Read sections
  Elf_Scn *scn = NULL;
  GElf_Shdr shdr;
  char *name;
  while((scn = elf_nextscn(elf, scn)) != NULL) {
    if(gelf_getshdr(scn, &shdr) == &shdr) {
      if((name = elf_strptr(elf, string_table_ix, shdr.sh_name)) != NULL) {
        Addr32 lma = get_lma(shdr.sh_addr, seg_index, prog_hdr_count);
        size_t offset;
        const char *sname = find_lma_section(elf, lma, seg_index, prog_hdr_count, &offset);
        printf("%4jd  %16s  size= %6d %08X - %08X   %08X - %08X  %s\n" ,
              (uintmax_t)elf_ndxscn(scn), name, shdr.sh_size,
              shdr.sh_addr, shdr.sh_addr + shdr.sh_size, lma, lma + shdr.sh_size, sname);
      }
    }
  }
  
  free(seg_index);
#endif


#if 0
  for(size_t i = 0; i < prog_hdr_count; i++) {
    GElf_Phdr phdr;
    if(gelf_getphdr(elf, i, &phdr) == &phdr) {
      printf("PHDR: %d %s\n", i, prog_hdr_type(phdr.p_type));
#define PRINT_FIELD(field)  printf("\t%s: 0x%jX  (%d)\n", #field, phdr.field, phdr.field);
      PRINT_FIELD(p_type);
      PRINT_FIELD(p_vaddr);
      PRINT_FIELD(p_paddr);
      PRINT_FIELD(p_filesz);
      PRINT_FIELD(p_memsz);
      fputs("\tflags: ", stdout);
      if(phdr.p_flags & PF_X) fputs("X", stdout);
      if(phdr.p_flags & PF_R) fputs("R", stdout);
      if(phdr.p_flags & PF_W) fputs("W", stdout);
      puts("");
    }
  }

  elf_end(elf);
  close(fd);
#endif

  if(bin_file_buf)
    free(bin_file_buf);


  return status;
}
