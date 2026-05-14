# encoding: utf-8

"""
WAF tool for Zephyr / West builds (AP_HAL_Zephyr).

WAF compiles ArduPilot → static libraries (.a), then
Zephyr's CMake build links them with the Zephyr kernel to produce zephyr.elf.

Pre-build phase
---------------
1. cmake configure for AP_HAL_Zephyr/app — generates all Zephyr headers
   (autoconf.h, devicetree_generated.h, …).
2. cmake builds the 'showinc' target → writes zephyr_build/includes.list
   containing every Zephyr include directory.
3. load_generated_includes task reads includes.list and prepends the paths
   to bld.env.INCLUDES before any WAF C++ compilation task runs.

Post-link phase
---------------
4. zephyr_firmware feature hook: after WAF links the vehicle static library
   (libarducopter.a etc.), cmake --build zephyr_build --target all is run.
   cmake links the WAF .a files with the Zephyr kernel → zephyr.elf.

"""

import glob
import os
import re
import sys
from collections import OrderedDict

from waflib import Task
from waflib.TaskGen import before_method, after_method, feature

import hal_common


@feature('zephyr_ap_library', 'zephyr_ap_program')
@before_method('process_source')
def zephyr_dynamic_env(self):
    hal_common.common_dynamic_env(self)


def configure(cfg):
    """Called by boards.zephyr.configure_env() via cfg.load('zephyr')."""
    bldnode = cfg.bldnode.make_node(cfg.variant)
    env = cfg.env

    ardupilot_root = cfg.srcnode.abspath()

    zephyr_base = os.environ.get('ZEPHYR_BASE', '')
    if not zephyr_base:
        auto = cfg.srcnode.make_node('modules/zephyr').abspath()
        if os.path.isdir(auto):
            zephyr_base = auto
    if not zephyr_base:
        cfg.fatal(
            'ZEPHYR_BASE not set and modules/zephyr submodule not found at '
            + cfg.srcnode.make_node('modules/zephyr').abspath()
        )
    env.ZEPHYR_BASE = zephyr_base

    # Locate the AP_HAL_Zephyr cmake application.
    app_node = cfg.srcnode.find_dir('libraries/AP_HAL_Zephyr/app')
    if app_node is None:
        cfg.fatal(
            'Could not locate libraries/AP_HAL_Zephyr/app in the ArduPilot '
            'source tree. Expected at: '
            + cfg.srcnode.make_node('libraries/AP_HAL_Zephyr/app').abspath()
        )
    env.ZEPHYR_APP_DIR = app_node.abspath()

    # Build output directory for the inner CMake / West build.
    env.ZEPHYR_BUILD_DIR = bldnode.make_node('zephyr_build').abspath()

    board_name = cfg.env.BOARD
    hwdef_path = os.path.join(
        ardupilot_root,
        'libraries/AP_HAL_Zephyr/hwdef',
        board_name,
        'hwdef.dat',
    )
    if not os.path.exists(hwdef_path):
        cfg.fatal('AP_HAL_Zephyr: hwdef.dat not found at %s' % hwdef_path)
    env.ZEPHYR_BOARD = ''
    with open(hwdef_path) as fh:
        for line in fh:
            m = re.match(r'\s*define\s+ZEPHYR_BOARD\s+(\S+)', line)
            if m:
                env.ZEPHYR_BOARD = m.group(1)
                break
    if not env.ZEPHYR_BOARD:
        cfg.fatal(
            'AP_HAL_Zephyr: %s must contain `define ZEPHYR_BOARD <target>` '
            '(Zephyr board target, e.g. `nucleo_h743zi/stm32h743xx` or '
            '`mpfs_icicle/polarfire/u54`)' % hwdef_path
        )

    boards_root = cfg.srcnode.make_node('libraries/AP_HAL_Zephyr/boards').abspath()
    _matches = glob.glob(os.path.join(boards_root, '*', board_name))
    if len(_matches) != 1:
        cfg.fatal(
            'AP_HAL_Zephyr: expected exactly one boards/*/%s directory, '
            'found %d: %r' % (board_name, len(_matches), _matches)
        )
    env.ZEPHYR_BOARD_HAL_DIR = _matches[0]

    # DTS overlay — lives in the board HAL dir.  Exactly one .overlay must
    # be present.
    _overlays = glob.glob(os.path.join(env.ZEPHYR_BOARD_HAL_DIR, '*.overlay'))
    if len(_overlays) != 1:
        cfg.fatal(
            'AP_HAL_Zephyr: expected exactly one *.overlay in %s, found %d: %r'
            % (env.ZEPHYR_BOARD_HAL_DIR, len(_overlays), _overlays)
        )
    env.DTC_OVERLAY_FILE = _overlays[0]

    # Board-specific Kconfig fragment — same base name, .conf extension.
    board_conf = env.DTC_OVERLAY_FILE[:-len('.overlay')] + '.conf'
    env.ZEPHYR_BOARD_CONF = board_conf if os.path.exists(board_conf) else ''

    env.CMAKE_PROJECT_NAME   = 'ardupilot-zephyr'
    env.Zephyr_DIR           = os.path.join(ardupilot_root, 'modules/zephyr/share/zephyr-package/cmake')
    _workspace_root = os.path.dirname(ardupilot_root)
    _candidates = [
        os.environ.get('ZEPHYR_MODULES', ''),
        os.path.join(ardupilot_root, 'modules/modules'),
        os.path.join(_workspace_root, 'modules/modules'),
    ]
    env.ZEPHYR_MODULES = ''
    for _c in _candidates:
        if _c and os.path.isdir(_c):
            env.ZEPHYR_MODULES = _c
            break
    _module_yml = os.path.join(env.ZEPHYR_BOARD_HAL_DIR, 'zephyr', 'module.yml')
    env.ZEPHYR_EXTRA_MODULES = env.ZEPHYR_BOARD_HAL_DIR if os.path.exists(_module_yml) else ''

    _cross = os.environ.get('CROSS_COMPILE', '')
    if not _cross:
        cfg.fatal(
            'CROSS_COMPILE env var must be set '
            '(e.g. export CROSS_COMPILE=riscv-none-elf-  or  '
            'export CROSS_COMPILE=arm-zephyr-eabi-)'
        )
    env.ZEPHYR_TOOLCHAIN_VARIANT = 'cross-compile'
    env.CROSS_COMPILE = _cross
    _tc_path = os.environ.get('CROSS_COMPILE_TOOLCHAIN_PATH',
                              os.path.dirname(os.path.dirname(_cross.rstrip('/'))))
    env.CROSS_COMPILE_TOOLCHAIN_PATH = _tc_path
    _venv_py = os.path.join(_workspace_root, 'sdk/.venv/bin/python3')
    env.Python3_EXECUTABLE = os.environ.get(
        'PYTHON3', _venv_py if os.path.isfile(_venv_py) else sys.executable
    )

    env.CMAKE_TRY_COMPILE_TARGET_TYPE = 'STATIC_LIBRARY'
    env.CMAKE_C_STANDARD_LIBRARIES    = ''
    env.CMAKE_CXX_STANDARD_LIBRARIES  = ''
    env.AP_PROGRAM_FEATURES += ['zephyr_ap_program']

    cfg.load('cmake')

    cfg.msg('ZEPHYR_BASE',               env.ZEPHYR_BASE)
    cfg.msg('ZEPHYR_APP_DIR',            env.ZEPHYR_APP_DIR)
    cfg.msg('ZEPHYR_BUILD_DIR',          env.ZEPHYR_BUILD_DIR)
    cfg.msg('ZEPHYR_BOARD',              env.ZEPHYR_BOARD)
    cfg.msg('ZEPHYR_BOARD_HAL_DIR',      env.ZEPHYR_BOARD_HAL_DIR)
    cfg.msg('ZEPHYR_TOOLCHAIN_VARIANT',  env.ZEPHYR_TOOLCHAIN_VARIANT)
    cfg.msg('CROSS_COMPILE',             env.CROSS_COMPILE)
    cfg.msg('DTC_OVERLAY_FILE',          env.DTC_OVERLAY_FILE)
    cfg.msg('Zephyr_DIR',                env.Zephyr_DIR)
    cfg.msg('ZEPHYR_MODULES',            env.ZEPHYR_MODULES)


def pre_build(bld):
    cmake_vars = OrderedDict()
    cmake_vars['BOARD']                          = bld.env.ZEPHYR_BOARD
    cmake_vars['ZEPHYR_BASE']                    = bld.env.ZEPHYR_BASE
    cmake_vars['ZEPHYR_TOOLCHAIN_VARIANT']       = bld.env.ZEPHYR_TOOLCHAIN_VARIANT
    cmake_vars['CROSS_COMPILE']                  = bld.env.CROSS_COMPILE
    cmake_vars['CROSS_COMPILE_TOOLCHAIN_PATH']   = bld.env.CROSS_COMPILE_TOOLCHAIN_PATH
    cmake_vars['DTC_OVERLAY_FILE']               = bld.env.DTC_OVERLAY_FILE
    cmake_vars['Zephyr_DIR']                     = bld.env.Zephyr_DIR
    cmake_vars['Python3_EXECUTABLE']             = bld.env.Python3_EXECUTABLE
    if bld.env.ZEPHYR_MODULES:
        cmake_vars['ZEPHYR_MODULES']             = bld.env.ZEPHYR_MODULES
    if bld.env.ZEPHYR_EXTRA_MODULES:
        cmake_vars['ZEPHYR_EXTRA_MODULES']       = bld.env.ZEPHYR_EXTRA_MODULES
    cmake_vars['ZEPHYR_BOARD_HAL_DIR']           = bld.env.ZEPHYR_BOARD_HAL_DIR
    cmake_vars['CMAKE_TRY_COMPILE_TARGET_TYPE']  = bld.env.CMAKE_TRY_COMPILE_TARGET_TYPE
    cmake_vars['CMAKE_C_STANDARD_LIBRARIES']     = bld.env.CMAKE_C_STANDARD_LIBRARIES
    cmake_vars['CMAKE_CXX_STANDARD_LIBRARIES']   = bld.env.CMAKE_CXX_STANDARD_LIBRARIES
    if bld.env.ZEPHYR_BOARD_CONF:
        cmake_vars['OVERLAY_CONFIG']             = bld.env.ZEPHYR_BOARD_CONF
    cmake_vars['ARDUPILOT_LIB']  = bld.bldnode.find_or_declare('lib/').abspath()
    cmake_vars['ARDUPILOT_BIN']  = bld.bldnode.find_or_declare('lib/bin').abspath()
    _cmd = bld.cmd
    if _cmd == 'build':
        _cmd = 'arducopter'
    cmake_vars['ARDUPILOT_CMD']  = _cmd

    app_src_node = bld.root.find_dir(bld.env.ZEPHYR_APP_DIR)
    if app_src_node is None:
        bld.fatal('ZEPHYR_APP_DIR not found: %s' % bld.env.ZEPHYR_APP_DIR)

    zephyr_cmake = bld.cmake(
        name='ardupilot-zephyr',
        cmake_src=app_src_node,
        cmake_bld='zephyr_build',
        cmake_vars=cmake_vars,
    )

    showinc_tg = zephyr_cmake.build('showinc', target='zephyr_build/includes.list')
    showinc_tg.post()
    showdefs_tg = zephyr_cmake.build('showdefs', target='zephyr_build/compile_defs.list')
    showdefs_tg.post()
    # ABI flags (-march / -mabi / -mcmodel / -mcpu / -mfpu / -mfloat-abi / -mthumb)
    # are declared by the board HAL CMakeLists.txt via AP_BOARD_ABI_CFLAGS.

    class load_generated_includes(Task.Task):
        always_run = True

        def run(tsk):
            bld = tsk.generator.bld
            inc_node = bld.bldnode.find_or_declare('zephyr_build/includes.list')
            includes = [p for p in inc_node.read().split() if p]
            bld.env.prepend_value('INCLUDES', includes)
            defs_node = bld.bldnode.find_or_declare('zephyr_build/compile_defs.list')
            dflags = ['-D' + d for d in defs_node.read().split() if d]
            bld.env.prepend_value('CXXFLAGS', dflags)
            bld.env.prepend_value('CFLAGS', dflags)
            rsp_node = bld.bldnode.find_or_declare('zephyr_build/ap_board_abi.rsp')
            rsp_arg = '@' + rsp_node.abspath()
            bld.env.prepend_value('CFLAGS',   [rsp_arg])
            bld.env.prepend_value('CXXFLAGS', [rsp_arg])

    tsk = load_generated_includes(env=bld.env)
    tsk.set_inputs([
        bld.bldnode.find_or_declare('zephyr_build/includes.list'),
        bld.bldnode.find_or_declare('zephyr_build/compile_defs.list'),
        bld.bldnode.find_or_declare('zephyr_build/ap_board_abi.rsp'),
    ])
    bld.add_to_group(tsk)


@feature('zephyr_ap_program')
@after_method('process_source')
def zephyr_firmware(self):
    """
    After WAF links the vehicle static library, run cmake --build to assemble
    zephyr.elf (Zephyr kernel + WAF-compiled ArduPilot).
    """
    target_cmd = self.bld.env.get_flat('ARDUPILOT_CMD') or 'arducopter'
    if not (self.name == target_cmd or self.name.endswith('/' + target_cmd)):
        return

    self.link_task.always_run = True

    bld = self.bld

    def _cmake_final_link(bld):
        import subprocess
        cmake_bld = bld.bldnode.make_node('zephyr_build').abspath()
        cmake_bin = bld.env.get_flat('CMAKE')
        ret = subprocess.call([cmake_bin, '--build', cmake_bld, '--target', 'all'])
        if ret != 0:
            bld.fatal('cmake final link failed (exit %d)' % ret)

    bld.add_post_fun(_cmake_final_link)
