#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lib_cfg/build_config.h"
#include "cstone/platform.h"

#include "FreeRTOS.h"
#include "semphr.h"

#include "cstone/console.h"
#include "cstone/term_color.h"

#include "util/mempool.h"
#include "cstone/blocking_io.h"

#include "bsd/string.h"
#include "util/getopt_r.h"
#include "util/intmath.h"
#include "util/range_strings.h"
#include "util/string_ops.h"
#include "util/glob.h"
#include "evfs.h"
#include "cmds_filesys.h"

extern mpPoolSet g_pool_set; // FIXME: Use sys pool


static int32_t cmd_cd(uint8_t argc, char *argv[], void *eval_ctx) {
  if(argc < 2)
    return 2;

  int status = evfs_set_cur_dir(argv[1]);

  return status;
}


static int32_t cmd_pwd(uint8_t argc, char *argv[], void *eval_ctx) {
  char *cwd = (char *)mp_alloc(&g_pool_set, EVFS_MAX_PATH, NULL);
  if(!cwd)
    return 1;

  StringRange cwd_r;
  range_init(&cwd_r, cwd, EVFS_MAX_PATH);

  evfs_get_cur_dir(&cwd_r);
  puts(cwd);

  mp_free(&g_pool_set, cwd);
  return 0;
}


// Convert an integer to fixed point reduced by the appropriate SI power
static unsigned scale_to_si(unsigned value, unsigned scale, char *si_prefix) {
  static const char s_si_prefix[] = " KMGTPE";

  const char *prefix_pos = s_si_prefix;
  unsigned long min_val = 1;
  unsigned long max_val;

  // Find the correct prefix
  while(*prefix_pos != '\0') {
    max_val = min_val * 1024;

    if((unsigned long)value < max_val || prefix_pos[1] == '\0')
      break;

    prefix_pos++;
    min_val = max_val;
  }

  *si_prefix = (*prefix_pos == ' ') ? '\0' : *prefix_pos;

  // Convert to fixed point with mul by scale then reduce to range of the selected prefix.
  // We add an additional 2x factor so we can preserve a portion of the truncated
  // fractional part for rounding.
  unsigned value_2x = ((unsigned long)value * 2ul * scale) / min_val;
  return (value_2x + 1) / 2; // Round up truncated fraction into last place and correct the scale
}


// Config settings for cmd_ls print functions
struct LsConfig {
  char *path_buf;   // Buffer for constructing paths
  const char *glob; // Optional glob match pattern
  const char *dir_name; // Optional subdirectory name
  int   term_width; // Width of the terminal
  int   size_width; // Width of the file size field
  int   name_width; // Width of the longest file name
  int   name_count; // Number of files to list
  size_t total_name_len;  // Total length of all file names
  bool  have_mtime; // mtime is available from the filesystem
  bool  need_stat;  // evfs_stat() needs to be called for mtime or file size
  bool  si_sizes;   // Show size with SI units
};


static inline bool file__glob(const char *pattern, EvfsInfo *info) {
  return glob_match(pattern, info->name, "/\\");
}


static void ls__print_detail(EvfsDir *dh, struct LsConfig *cfg) {
  EvfsInfo info;
  EvfsInfo stat;

  StringRange path_r;
  range_init(&path_r, cfg->path_buf, strlen(cfg->path_buf));
  StringRange file_name_r;
  StringRange file_path_r;
  range_init(&file_path_r, cfg->path_buf, EVFS_MAX_PATH);


  // Print file data
  evfs_dir_rewind(dh);
  while(evfs_dir_read(dh, &info) != EVFS_DONE) {
    if(cfg->glob && !file__glob(cfg->glob, &info))
      continue;

    if(cfg->need_stat) {
      range_init(&file_name_r, info.name, strlen(info.name));
      evfs_path_join(&path_r, &file_name_r, &file_path_r);
      evfs_stat(file_path_r.start, &stat);
    }

    // Print file size
    if(info.type & EVFS_FILE_DIR) { // Directory
      bprintf("D %*s", cfg->size_width, "");
    } else { // File
      evfs_off_t file_size = cfg->need_stat ? stat.size : info.size;

      if(cfg->si_sizes) {  // Print human friendly size value with SI units
        char si_prefix;

        // Convert to fixed point with SI exponent
        unsigned scaled_size = scale_to_si(file_size, 10, &si_prefix);
        if(scaled_size >= 10*10 && si_prefix != '\0') { // Remove fraction if integer portion >= 10
          scaled_size += 10;  // Ceiling rather than round with +0.5 to approximate GNU ls behavior
          scaled_size = (scaled_size / 10) * 10;
        }

        char buf[10];
        AppendRange rng = RANGE_FROM_ARRAY(buf);
        unsigned pad_digits = cfg->size_width + 1;
        if(si_prefix == '\0') // No prefix so use additional character for padding
          pad_digits++;

        // Show tenths unless the value is a round integer
        unsigned frac_places = 1;
        if((scaled_size / 10) * 10 == scaled_size)  // No fractional part
          frac_places = 0;

        // Format fixed point into string
        range_cat_fixed_padded(&rng, scaled_size, 10, frac_places, pad_digits);
        if(si_prefix != '\0')
          range_cat_char(&rng, si_prefix);
        fputs(buf, stdout);

      } else {  // Full integer value
        bprintf("  %*u", cfg->size_width, file_size);
      }
    }

    // Print mod time if available
    if(cfg->have_mtime) {
      char buf[18];
      time_t mtime = cfg->need_stat ? stat.mtime : info.mtime;
      struct tm local;
      localtime_r(&mtime, &local);
      strftime(buf, sizeof(buf), " %Y-%m-%d %H:%M", &local);
      fputs(buf, stdout);
    }

    // Print file name
    if(info.type & EVFS_FILE_DIR) {
      printf(" " A_BBLU "%s" A_NONE "\n", info.name);
    } else {
      putc(' ', stdout);
      puts(info.name);
    }
  }

}


static int sort_names_cmp(const void *a, const void *b) {
  return stricmp(*(const char**)a, *(const char**)b);
}


static bool ls__print_columns(EvfsDir *dh, struct LsConfig *cfg) {
  bool rval = true;

  char *file_name = NULL;
  char *name_buf = NULL;
  char **sorted_names = NULL;
  char *name_buf_pos;
  int sorted_pos, cols, stride;

  // Get buffer for formatted file name
  int col_width = cfg->name_width;
  if(cfg->dir_name && cfg->glob) // Include dir name on each file
    col_width += strlen(cfg->dir_name) + 1; // Dir + separator

  file_name = (char *)mp_alloc(mp_sys_pools(), col_width+1, NULL);
  if(!file_name) {
    rval = false;
    goto cleanup;
  }

  // Build array of file names with attribute byte
  //    [attr][name][\0][attr][name][\0]...
  name_buf = (char *)malloc(cfg->total_name_len + cfg->name_count*2);
  if(!name_buf) {
    rval = false;
    goto cleanup;
  }

  sorted_names = (char **)malloc(cfg->name_count * sizeof(char *));
  if(!sorted_names) {
    rval = false;
    goto cleanup;
  }

  name_buf_pos = name_buf;
  sorted_pos = 0;

  EvfsInfo info;

  evfs_dir_rewind(dh);
  while(evfs_dir_read(dh, &info) != EVFS_DONE) {
    if(cfg->glob && !file__glob(cfg->glob, &info))
      continue;

    size_t name_len = strlen(info.name);
    // Set attribute byte
    *name_buf_pos = (info.type & EVFS_FILE_DIR) ? 1 : 0;
    name_buf_pos++;

    // Copy name
    memcpy(name_buf_pos, info.name, name_len+1);

    sorted_names[sorted_pos++] = name_buf_pos;
    name_buf_pos += name_len+1;
  }

  qsort(sorted_names, cfg->name_count, sizeof(char *), sort_names_cmp);


  // Get layout geometry
  cols = cfg->term_width / (col_width + 2);
  if(cols == 0)
    cols = 1;

  stride = (cfg->name_count + cols-1) / cols;

  // Print names
  sorted_pos = 0;
  for(int r = 0; r < stride; r++) {
    for(int c = 0; c < cols; c++) {
      int name_ix = r + c*stride;
      if(name_ix >= cfg->name_count) { // Out of range, last column ended
        putnl();
        break;
      }

      // Build name with optional dir prefix
      AppendRange fn_r;
      range_init(&fn_r, file_name, col_width+1);
      if(cfg->dir_name && cfg->glob) {  // Show dir prefix
        range_cat_str(&fn_r, cfg->dir_name);
        range_cat_char(&fn_r, '/');
      }
      range_cat_str(&fn_r, sorted_names[name_ix]);

      range_init(&fn_r, file_name, col_width+1);
      range_pad_right((StringRange *)&fn_r, ' '); // Fill out column


      char attr = sorted_names[name_ix][-1];

      if(attr)
        fputs(A_BBLU, stdout);

      if(c == 0)
        bfputs(file_name, stdout);
      else
        fputs(file_name, stdout);

      if(attr)
        fputs(A_NONE, stdout);


      // Column separator or newline
      if(c < cols-1)
        fputs("  ", stdout);
      else
        putnl();
    }
  }

cleanup:
  if(file_name)     mp_free(mp_sys_pools(), file_name);
  if(name_buf)      free(name_buf);
  if(sorted_names)  free(sorted_names);

  return rval;
}


static int32_t cmd_ls(uint8_t argc, char *argv[], void *eval_ctx) {
  GetoptState state = {0};
  state.report_errors = true;

  int c;
  bool show_detail = false;
  bool si_sizes = false;

  while((c = getopt_r(argv, "lh", &state)) != -1) {
    switch(c) {
    case 'l': show_detail = true; break;
    case 'h': si_sizes = true; break;

    default:
    case ':':
    case '?':
      return -2;
      break;
    }
  }

  struct LsConfig cfg = {0};
  cfg.si_sizes = si_sizes;

  StringRange file_name_r;

  if(state.optind < argc) {
    evfs_path_basename(argv[state.optind], &file_name_r);
    if(is_glob(file_name_r.start)) {
      if(file_name_r.start != argv[state.optind]) {
        // Split into directory and glob portions
        cfg.dir_name = argv[state.optind];
        *(char *)&file_name_r.start[-1] = '\0'; // Overwrite dir sep
        cfg.glob = file_name_r.start;
      } else { // Just a glob
        cfg.glob = argv[state.optind];
      }

    } else { // Just a directory
      cfg.dir_name = argv[state.optind];
    }
  }

  char *path = (char *)mp_alloc(&g_pool_set, EVFS_MAX_PATH, NULL);
  if(!path)
    return 1;

  StringRange path_r;
  range_init(&path_r, path, EVFS_MAX_PATH); // Start with path_r covering whole buffer

  evfs_get_cur_dir(&path_r);
  if(cfg.dir_name) {  // path --> path/dir_name
    range_init(&file_name_r, cfg.dir_name, strlen(cfg.dir_name));
    evfs_path_join(&path_r, &file_name_r, &path_r);
  }

  range_init(&path_r, path, strlen(path));  // Reduce path_r range to base directory

  EvfsDir *dh;
  int status = evfs_open_dir(path, &dh);
  if(status == EVFS_OK) {
    EvfsInfo info;
    EvfsInfo stat;

    // Get filesystem capabilities
    unsigned stat_fields;
    unsigned dir_fields;
    evfs_vfs_ctrl(EVFS_CMD_GET_STAT_FIELDS, &stat_fields);
    evfs_vfs_ctrl(EVFS_CMD_GET_DIR_FIELDS, &dir_fields);

    bool have_mtime = (dir_fields & EVFS_INFO_MTIME) || (stat_fields & EVFS_INFO_MTIME);
    cfg.have_mtime  = have_mtime;
    cfg.need_stat   = !(dir_fields & EVFS_INFO_SIZE) || (have_mtime && !(dir_fields & EVFS_INFO_MTIME));


    StringRange file_path_r; // Constructed path to dir entries
    range_init(&file_path_r, path, EVFS_MAX_PATH);

    if(si_sizes)  // Fixed column width for sizes with SI values
      cfg.size_width = 4+1;

    // Scan through files to find width of name and size fields
    evfs_off_t max_size = 0;
    while(evfs_dir_read(dh, &info) != EVFS_DONE) {
      if(cfg.glob && !file__glob(cfg.glob, &info))
        continue;

      int name_len = strlen(info.name);
      cfg.total_name_len += name_len;
      cfg.name_count++;
      if(name_len > cfg.name_width)
        cfg.name_width = name_len;

      if(cfg.size_width == 0) { // Look for largest file
        if(dir_fields & EVFS_INFO_SIZE) {
          if(info.size > max_size)
            max_size = info.size;
        } else { // Have to stat files for size
          range_init(&file_name_r, info.name, strlen(info.name));
          evfs_path_join(&path_r, &file_name_r, &file_path_r);
          evfs_stat(path, &stat);
          if(stat.size > max_size)
            max_size = stat.size;
        }
      }
    }

    if(cfg.size_width == 0)
      cfg.size_width = base10_digits((uint32_t)max_size);


    range_set_len(&file_path_r, range_size(&path_r)); // Strip off joined file
    cfg.path_buf    = path;

    Console *con = active_console();
    cfg.term_width = con->term_size.cols;

    bool success = false;
    if(!show_detail)
      success = ls__print_columns(dh, &cfg);

    // Show detail print if chosen by option or columnar print failed
    if(!success)
      ls__print_detail(dh, &cfg);

    evfs_dir_close(dh);
  }

  mp_free(&g_pool_set, path);

  return status;
}


const ConsoleCommandDef g_filesystem_cmd_set[] = {
#ifndef PLATFORM_EMBEDDED
  CMD_DEF("cd",       cmd_cd,         "Change directory"),
  CMD_DEF("ls",       cmd_ls,         "List directory"),
  CMD_DEF("pwd",      cmd_pwd,        "Current directory"),
#endif
  CMD_END
};
