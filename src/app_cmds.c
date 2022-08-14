#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "cstone/console.h"
#include "app_cmds.h"

#include "util/getopt_r.h"
#include "util/range_strings.h"


int32_t cmd_demo(uint8_t argc, char *argv[], void *eval_ctx) {
  return 0;
}

const ConsoleCommandDef g_app_cmd_set[] = {
  CMD_DEF("demo",     cmd_demo,  "Demo app command"),
  CMD_END
};


