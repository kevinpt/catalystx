import os
import sys
import tempfile
import subprocess
#import pdb

import invoke.vendor.yaml3 as yaml
from invoke import task, Collection
from invoke.exceptions import Exit

# Change to project root if we're in a subdirectory
os.chdir(os.path.dirname(os.path.abspath(__file__)))

# Load user specific configuration
#ns = Collection.from_module(sys.modules[__name__])

def init_user(ns):
  user_cfg_path = 'user.yaml'
  if os.path.isfile(user_cfg_path):
    with open(user_cfg_path) as fh:
      user_cfg = yaml.safe_load(fh)
      print(user_cfg)
#      pdb.set_trace()
      ns.configure(user_cfg)

#init_user(ns)


def get_fixture(c):
  if c.user.fixture in c.user.fixtures:
    return c.user.fixtures[c.user.fixture]

  raise Exit('No fixture defined')


def report_config(c, board_name=None):
  if board_name is None:
    board_name = c.proj.board
  print(f'Board:  \t{board_name}')

  if 'fixture' in c.user:
    print(f'Fixture:\t{c.user.fixture}')
    fixture = get_fixture(c)
    if fixture and 'probe' in fixture:
      print(f'Probe:  \t{fixture.probe}')

#  print('##', c.user.fixture)


@task
def init(c):
  '''Prepare a fresh repository clone'''
  c.run('git submodule update --init --recursive')

@task
def clean(c):
  c.run(f'cmake --build {c.proj.build_dir} --target clean')


def add_platform_submodules(c, platform):
  '''Add platform specific submodules'''
  platform_def = c.platforms[platform]
  if platform_def is None or 'submodules' not in platform_def:
    return False

  # Get current submodules
  r = c.run('git config --file .gitmodules --get-regexp path', hide='stdout')
  cur_submodules = set(l.split()[-1] for l in r.stdout.strip().split('\n'))

  submodules = c.platforms[platform].submodules

  added = False
  for sm, sm_uri in submodules.items():
    if sm not in cur_submodules:
      print('Missing submodule:', sm)
      c.run(f'git submodule add --depth 1 {sm_uri} {sm}')
      c.run(f'git config -f .gitmodules submodule.{sm}.shallow true')
      added = True

  return added


@task(iterable=['option'], help={'debug':'Debug build', 'release':'Release build',
      'board':'Select target board', 'option':'Build options', 'list_boards':'List available boards',
      'list_options':'List available options'})
def configure(c, debug=False, release=False, board=None, option=None, list_boards=False,
                list_options=False):
  '''Configure project settings

Combined debug and release options will select a CMake RelWithDebInfo build type

'''

  if list_boards:
    boards = sorted(c.boards.keys())
    print('Supported boards:')
    for b in boards:
      print('\t' + b)
    return

  if list_options:
    options = sorted(c.proj.options)
    print('Supported options:')
    for o in options:
      print('\t' + o)
    return

  # Determine CMake build type
  if debug and release:
    build_type = 'RelWithDebInfo'
  elif release:
    build_type = 'Release'
  else:
    build_type = 'Debug'

  # Validate board selection
  if board is None:
    board = c.proj.board
  if board not in c.boards.keys():
    raise Exit(f'Invalid board "{board}"')
  board_name = board
  board = c.boards[board_name]

  report_config(c, board_name)

  add_platform_submodules(c, board.platform)

  toolchain = ''  # No toolchain for hosted build
  if 'toolchain' in board:
    toolchain = f'--toolchain {c.cmake.toolchains[board.toolchain]} '

  # Validate build options
  proj_opts = set(c.proj.options)
  for opt in option:
    if opt not in proj_opts:
      raise Exit(f'"{opt}" not in project options')

  build_opts = set(option)

  # Add mandatory build options for the board
  if 'options' in board:
    build_opts = build_opts | set(board.options)

  print('Options:')
  for opt in sorted(list(build_opts)):
    print(f'\t{opt}')

  cmake_defs = []
  for opt in proj_opts:
    cmake_defs.append('-D{}={}'.format(opt, 'on' if opt in build_opts else 'off'))

  if 'linker_script' in board:
    cmake_defs.append(f'-DLINKER_SCRIPT={board.linker_script}')

  cmd = f'cmake -S {c.proj.source_dir} -B {c.proj.build_dir} {toolchain}' + \
        f'-DCMAKE_BUILD_TYPE={build_type} ' + \
        f'-DBUILD_BOARD={board_name} -DBUILD_PLATFORM={board.platform} '

  cmd = cmd + ' '.join(sorted(cmake_defs))
  c.run(cmd)


@task(help={'target':'CMake build target'})
def build(c, target=None):
  '''Compile project'''
  report_config(c)

  if target is None:  # Use first target as default
    target = c.proj.targets[0]

  nproc = len(os.sched_getaffinity(0))
  nproc = nproc-1 if nproc > 1 else 1
  cmd = f'cmake --build {c.proj.build_dir} --parallel {nproc} --target {target}'
#  c.run(cmd)

  # Exec cmake instead of calling run() so that we can get color output
  os.execvp('cmake', cmd.split())


@task(help={'probe':'Type of probe to program target device', 'target':'CMake build target'})
def program(c, probe=None, target=None):
  '''
  Program a target device
  '''
  report_config(c)
  fixture = get_fixture(c)

  if probe is None:
    if fixture is None or 'probe' not in fixture:
      raise Exit('ERROR: Fixture has no probe definition')
    probe = fixture.probe

  if target is None:  # Use first target as default
    target = c.proj.targets[0]

  print('Probe:', probe);
  if probe == 'blackmagic':
    prog_blackmagic(c, target)
  elif probe == 'stlink':
    prog_stlink(c, target)
  elif probe == 'stlink_openocd':
    prog_stlink_openocd(c, target)
  else:
    raise Exit(f'ERROR: Unsupported probe "{probe}"')


def prog_blackmagic(c, target):
  '''Program image using a Blackmagic Probe'''
  probe_cfg = c.probes.blackmagic
  tpwr = 'enable' if probe_cfg.tpwr else 'disable'
  elf_image = os.path.join(c.proj.build_dir, f'{target}.elf')

  if not os.path.isfile(elf_image):
    raise Exit(f'ERROR: "{elf_image}" does not exist')

  gdb_script = \
f'''target extended-remote {probe_cfg.device}
monitor tpwr {tpwr}
monitor swdp_scan
attach 1
load
kill
'''
  fh = tempfile.NamedTemporaryFile(prefix='gdb_')
  fh.write(gdb_script.encode('utf-8'))
  fh.flush()
  os.fsync(fh.fileno())

  c.run(f'gdb-multiarch {elf_image} --batch -x {fh.name}')
  fh.close()


def prog_stlink(c, target):
  '''Program image using a ST-Link'''
  probe_cfg = c.probes.stlink
  bin_image = os.path.join(c.proj.build_dir, f'{target}.bin')

  if not os.path.isfile(bin_image):
    raise Exit(f'ERROR: "{bin_image}" does not exist')

  c.run(f'st-flash write {bin_image} {probe_cfg.address}')


def prog_stlink_openocd(c, target):
  '''Program image using a ST-Link via OpenOCD'''
  probe_cfg = c.probes.stlink_openocd
  elf_image = os.path.join(c.proj.build_dir, f'{target}.elf')

  if not os.path.isfile(elf_image):
    raise Exit(f'ERROR: "{elf_image}" does not exist')

  c.run(f'openocd -f {probe_cfg.board_cfg} -c "program {elf_image} verify reset exit"')


@task(help={'target':'CMake build target'})
def debug(c, target=None):
  '''Launch debugger'''

  if target is None:  # Use first target as default
    target = c.proj.targets[0]
  elf_image = os.path.join(c.proj.build_dir, f'{target}.elf')

  c.run(f'gdb-multiarch {elf_image}')


@task
def console(c, device=None, baud=None):
  if device is None:
    device = get_fixture(c).console.device
  if baud is None:
    baud = get_fixture(c).console.baud

  cmd = f'screen -T VT100 {device} {baud}'
  os.execvp('screen', cmd.split())


