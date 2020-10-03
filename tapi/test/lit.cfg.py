# -*- Python -*-

import os
import platform
import re
import subprocess
import tempfile

import lit.formats
import lit.util

# Configuration file for the 'lit' test runner.

# name: The name of this test suite.
config.name = 'Tapi'

# Tweak PATH for Win32
if platform.system() == 'Windows':
    # Seek sane tools in directories and set to $PATH.
    path = getattr(config, 'lit_tools_dir', None)
    path = lit_config.getToolsPath(path,
                                   config.environment['PATH'],
                                   ['cmp.exe', 'grep.exe', 'sed.exe'])
    if path is not None:
        path = os.path.pathsep.join((path,
                                     config.environment['PATH']))
        config.environment['PATH'] = path

# Choose between lit's internal shell pipeline runner and a real shell.  If
# LIT_USE_INTERNAL_SHELL is in the environment, we use that as an override.
use_lit_shell = os.environ.get("LIT_USE_INTERNAL_SHELL")
if use_lit_shell:
    # 0 is external, "" is default, and everything else is internal.
    execute_external = (use_lit_shell == "0")
else:
    # Otherwise we default to internal on Windows and external elsewhere, as
    # bash on Windows is usually very slow.
    execute_external = (not sys.platform in ['win32'])

# testFormat: The test format to use to interpret tests.
#
# For now we require '&&' between commands, until they get globally killed and
# the test runner updated.
config.test_format = lit.formats.ShTest(execute_external)

# suffixes: A list of file extensions to treat as test files.
config.suffixes = [ '.test', '.c', '.cpp', '.m', '.mm']

# excludes: A list of directories to exclude from the testsuite. The 'Inputs'
# subdirectories contain auxiliary inputs for various tests in their parent
# directories.
config.excludes = ['Inputs', 'CMakeLists.txt', 'README.txt', 'LICENSE.txt']

# test_source_root: The root path where tests are located.
config.test_source_root = os.path.dirname(__file__)

# test_exec_root: The root path where tests should be run.
tapi_obj_root = getattr(config, 'tapi_obj_root', None)
if tapi_obj_root is not None:
    config.test_exec_root = os.path.join(tapi_obj_root, 'test')

# Set llvm_{src,obj}_root for use by others.
config.llvm_src_root = getattr(config, 'llvm_src_root', None)
config.llvm_obj_root = getattr(config, 'llvm_obj_root', None)

# Clear some environment variables that might affect Tapi.
#
# This first set of vars are read by Tapi, but shouldn't affect tests
# that aren't specifically looking for these features, or are required
# simply to run the tests at all.
#
# FIXME: Should we have a tool that enforces this?

# safe_env_vars = ('TMPDIR', 'TEMP', 'TMP', 'USERPROFILE', 'PWD',
#                  'MACOSX_DEPLOYMENT_TARGET', 'IPHONEOS_DEPLOYMENT_TARGET',
#                  'VCINSTALLDIR', 'VC100COMNTOOLS', 'VC90COMNTOOLS',
#                  'VC80COMNTOOLS')
possibly_dangerous_env_vars = ['COMPILER_PATH', 'RC_DEBUG_OPTIONS',
                               'CINDEXTEST_PREAMBLE_FILE', 'LIBRARY_PATH',
                               'CPATH', 'C_INCLUDE_PATH', 'CPLUS_INCLUDE_PATH',
                               'OBJC_INCLUDE_PATH', 'OBJCPLUS_INCLUDE_PATH',
                               'LIBCLANG_TIMING', 'LIBCLANG_OBJTRACKING',
                               'LIBCLANG_LOGGING', 'LIBCLANG_BGPRIO_INDEX',
                               'LIBCLANG_BGPRIO_EDIT', 'LIBCLANG_NOTHREADS',
                               'LIBCLANG_RESOURCE_USAGE',
                               'LIBCLANG_CODE_COMPLETION_LOGGING']
# Clang/Win32 may refer to %INCLUDE%. vsvarsall.bat sets it.
if platform.system() != 'Windows':
    possibly_dangerous_env_vars.append('INCLUDE')
for name in possibly_dangerous_env_vars:
    if name in config.environment:
        del config.environment[name]

# Tweak the PATH to include the tools dir and the scripts dir.
if tapi_obj_root is not None:
    llvm_tools_dir = getattr(config, 'llvm_tools_dir', None)
    if not llvm_tools_dir:
        lit_config.fatal('No LLVM tools dir set!')
    path = os.path.pathsep.join((llvm_tools_dir, config.environment['PATH']))
    config.environment['PATH'] = path
    llvm_libs_dir = getattr(config, 'llvm_libs_dir', None)
    if not llvm_libs_dir:
        lit_config.fatal('No LLVM libs dir set!')
    path = os.path.pathsep.join((llvm_libs_dir,
                                config.environment.get('LD_LIBRARY_PATH', '')))
    config.environment['LD_LIBRARY_PATH'] = path
    path = os.path.pathsep.join((llvm_libs_dir,
                                 config.environment.get('DYLD_LIBRARY_PATH', '')))
    config.environment['DYLD_LIBRARY_PATH'] = path

# Propagate path to symbolizer for ASan/MSan.
for symbolizer in ['ASAN_SYMBOLIZER_PATH', 'MSAN_SYMBOLIZER_PATH']:
    if symbolizer in os.environ:
        config.environment[symbolizer] = os.environ[symbolizer]

###

# Abort when the object root is unknown.
if config.test_exec_root is None:
    lit_config.fatal("object root is unknown")

###

def infer_tapi_tool(NAME, ENV, PATH):
    # Determine which tool to use
    tool = os.getenv(ENV)

    # If the user set ENV in the environment, definitely use that and don't
    # try to validate.
    if tool:
        return tapi_run

    # Otherwise look in the path.
    tool = lit.util.which(NAME, PATH)

    if not tool:
        lit_config.fatal("couldn't find '{0}' program, try setting "
                         "{1} in your environment".format(NAME, ENV))
    return tool


def get_macos_sdk_path(config):
    try:
        cmd = subprocess.Popen(['xcrun', '--sdk', 'macosx', '--show-sdk-path'],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out, err = cmd.communicate()
        out = out.strip()
        res = cmd.wait()
    except OSError:
        res = -1

    if res == 0 and out:
        return out

    lit_config.fatal("couldn't find sysroot")


# Sanitizers.
if 'Address' in config.llvm_use_sanitizer:
    config.available_features.add("asan")
else:
    config.available_features.add("not_asan")

if 'Memory' in config.llvm_use_sanitizer:
    config.available_features.add("msan")
else:
    config.available_features.add("not_msan")

if 'Undefined' in config.llvm_use_sanitizer:
    config.available_features.add("ubsan")
else:
    config.available_features.add("not_ubsan")

if config.iosmac_support == '1':
    config.available_features.add("iosmac")

if config.i386_support == '1':
    config.available_features.add("i386")

config.inputs = os.path.join(tapi_obj_root, 'Inputs')
config.tapi = infer_tapi_tool("tapi", "TAPI", config.environment['PATH']).replace('\\', '/')
config.tapi_run = infer_tapi_tool("tapi-run", "TAPI_RUN", config.environment['PATH']).replace('\\', '/')
config.tapi_frontend = infer_tapi_tool("tapi-frontend", "TAPI_FRONTEND", config.environment['PATH']).replace('\\', '/')
config.tapi_binary_reader = infer_tapi_tool("tapi-binary-reader", "TAPI_BINARY_READER", config.environment['PATH']).replace('\\', '/')
config.sysroot = get_macos_sdk_path(config)
lit_config.note('using SDKROOT: %r' % config.sysroot)

config.substitutions.append( ('%inputs', config.inputs) )
config.substitutions.append( ('%tapi-frontend', config.tapi_frontend + ' -no-colors') )
config.substitutions.append( ('%tapi-binary-reader', config.tapi_binary_reader + ' -no-colors') )
config.substitutions.append( ('%tapi-run', config.tapi_run) )
config.substitutions.append( ('%tapi', config.tapi) )
config.substitutions.append( ('%sysroot', config.sysroot) )
config.substitutions.append(
    ('%hmaptool', "'%s' %s" % (config.python_executable,
                             os.path.join(config.tapi_tools_dir, 'hmaptool'))))

config.substitutions.append(
    (' tapi ', """*** Do not use 'tapi' in tests, use '%tapi'. ***""") )

# Sanitizers.
if 'Address' in config.llvm_use_sanitizer:
    config.available_features.add("asan")
else:
    config.available_features.add("not_asan")

if 'Undefined' in config.llvm_use_sanitizer:
    config.available_features.add("ubsan")
else:
    config.available_features.add("not_ubsan")

###
