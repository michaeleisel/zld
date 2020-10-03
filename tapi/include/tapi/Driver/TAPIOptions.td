//===--- TAPIOptions.td - Options for tapi --------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the options accepted by tapi.
//
//===----------------------------------------------------------------------===//

// Include the common option parsing interfaces.
include "llvm/Option/OptParser.td"

/////////
// Flags

// DriverOption - The option is a "standard" option and shared by all tools.
def DriverOption : OptionFlag;

// ArchiveOption - The option is used by the archive driver.
def ArchiveOption : OptionFlag;

// StubOption - The option is used by the dynamic library stubifier.
def StubOption : OptionFlag;

// InstallAPIOption - The option is used by the installapi driver.
def InstallAPIOption : OptionFlag;

// ReexportOption - The option is used by the reexport driver.
def ReexportOption : OptionFlag;

// GenerateAPITestsOption - The option is used by the API test generation driver.
def GenerateAPITestsOption : OptionFlag;


/////////
// Options

//
// Driver options
//
def version : Flag<["-", "--"], "version">, Flags<[DriverOption]>,
  HelpText<"Display the version">;
def help : Flag<["-", "--"], "help">, Flags<[DriverOption]>,
  HelpText<"Display this help message">;
def help_hidden : Flag<["-", "--"], "help-hidden">, Flags<[DriverOption]>;
def snapshot : Flag<["--"], "snapshot">, Flags<[DriverOption]>,
  HelpText<"Force creation of a snapshot">;
def snapshot_dir : Joined<["--"], "snapshot-dir=">, Flags<[DriverOption]>,
  HelpText<"Specify the snapshot output directory">, MetaVarName<"<dir>">;
def load_snapshot : Joined<["--"], "load-snapshot=">, Flags<[DriverOption]>,
  HelpText<"Specify the snapshot input directory">, MetaVarName<"<dir>">;
def snapshot_use_own_resource_dir : Flag<["--"], "snapshot-use-own-resource-dir">, Flags<[DriverOption]>,
  HelpText<"Use our own resource directory">;


//
// Archive options
//
def info : Flag<["--"], "info">, Flags<[ArchiveOption]>,
  HelpText<"List the architectures in the input file">;
def extract : Separate<["--"], "extract">, Flags<[ArchiveOption]>,
  HelpText<"Extract the specified <architecture>.">,
  MetaVarName<"<architecture>">;
def remove : Separate<["--"], "remove">, Flags<[ArchiveOption]>,
  HelpText<"Remove the specified <architecture>.">,
  MetaVarName<"<architecture>">;
def merge : Flag<["--"], "merge">, Flags<[ArchiveOption]>,
  HelpText<"Merge the input files.">;
def verify_arch : Separate<["--"], "verify-arch">, Flags<[ArchiveOption]>,
  HelpText<"Verify that the specified <architecture> is present in the file.">,
  MetaVarName<"<architecture>">;
def listSymbols : Flag<["--"], "list-symbols">, Flags<[ArchiveOption]>,
  HelpText<"List the exported symbols.">;
def allow_arch_merges : Flag<["--"], "allow-arch-merges">,
  Flags<[ArchiveOption]>, HelpText<"Allow merges of the same architecture.">;


//
// Shared Options
//
def target : Joined<["--"], "target=">,
  Flags<[InstallAPIOption, ReexportOption, GenerateAPITestsOption]>,
  HelpText<"Generate code for the given target">;
def target_legacy_spelling : Separate<["-"], "target">,
  Flags<[InstallAPIOption, ReexportOption, GenerateAPITestsOption]>,
  Alias<target>;
def arch : Separate<["-"], "arch">, Flags<[InstallAPIOption, ReexportOption, ArchiveOption, GenerateAPITestsOption]>,
  HelpText<"Specify the architecture to build for">,
  MetaVarName<"<architecture>">;

def mmacos_version_min_EQ : Joined<["-"], "mmacos-version-min=">,
  Flags<[InstallAPIOption, ReexportOption, GenerateAPITestsOption]>;
def mmacosx_version_min_EQ : Joined<["-"], "mmacosx-version-min=">,
  Flags<[InstallAPIOption, ReexportOption, GenerateAPITestsOption]>,
  Alias<mmacos_version_min_EQ>;
def macosx_version_min : Separate<["-"], "macosx_version_min">,
  Flags<[InstallAPIOption, ReexportOption, GenerateAPITestsOption]>,
  Alias<mmacos_version_min_EQ>;

def mios_version_min_EQ : Joined<["-"], "mios-version-min=">,
  Flags<[InstallAPIOption, ReexportOption, GenerateAPITestsOption]>;
def miphoneos_version_min_EQ : Joined<["-"], "miphoneos-version-min=">,
  Flags<[InstallAPIOption, ReexportOption, GenerateAPITestsOption]>,
  Alias<mios_version_min_EQ>;
def ios_version_min : Separate<["-"], "ios_version_min">,
  Flags<[InstallAPIOption, ReexportOption, GenerateAPITestsOption]>,
  Alias<mios_version_min_EQ>;

def mwatchos_version_min_EQ : Joined<["-"], "mwatchos-version-min=">,
  Flags<[InstallAPIOption, ReexportOption, GenerateAPITestsOption]>;
def watchos_version_min : Separate<["-"], "watchos_version_min">,
  Flags<[InstallAPIOption, ReexportOption, GenerateAPITestsOption]>,
  Alias<mwatchos_version_min_EQ>;

def mtvos_version_min_EQ : Joined<["-"], "mtvos-version-min=">,
  Flags<[InstallAPIOption, ReexportOption, GenerateAPITestsOption]>;
def tvos_version_min : Separate<["-"], "tvos_version_min">,
  Flags<[InstallAPIOption, ReexportOption, GenerateAPITestsOption]>,
  Alias<mtvos_version_min_EQ>;

def mbridgeos_version_min_EQ : Joined<["-"], "mbridgeos-version-min=">,
  Flags<[InstallAPIOption, ReexportOption, GenerateAPITestsOption]>;
def bridgeos_version_min : Separate<["-"], "bridgeos_version_min">,
  Flags<[InstallAPIOption, ReexportOption, GenerateAPITestsOption]>,
  Alias<mbridgeos_version_min_EQ>;

def mios_simulator_version_min_EQ : Joined<["-"], "mios-simulator-version-min=">,
  Flags<[InstallAPIOption, ReexportOption, GenerateAPITestsOption]>;
def ios_simulator_version_min : Separate<["-"], "ios_simulator_version_min">,
  Flags<[InstallAPIOption, ReexportOption, GenerateAPITestsOption]>,
  Alias<mios_simulator_version_min_EQ>;

def mwatchos_simulator_version_min_EQ : Joined<["-"], "mwatchos-simulator-version-min=">,
  Flags<[InstallAPIOption, ReexportOption, GenerateAPITestsOption]>;
def watchos_simulator_version_min : Separate<["-"], "watchos_simulator_version_min">,
  Flags<[InstallAPIOption, ReexportOption, GenerateAPITestsOption]>,
  Alias<mwatchos_simulator_version_min_EQ>;

def mtvos_simulator_version_min_EQ : Joined<["-"], "mtvos-simulator-version-min=">,
  Flags<[InstallAPIOption, ReexportOption, GenerateAPITestsOption]>;
def tvos_simulator_version_min : Separate<["-"], "tvos_simulator_version_min">,
  Flags<[InstallAPIOption, ReexportOption, GenerateAPITestsOption]>,
  Alias<mtvos_simulator_version_min_EQ>;


def x : JoinedOrSeparate<["-"], "x">, Flags<[InstallAPIOption, ReexportOption, GenerateAPITestsOption]>,
  HelpText<"Treat subsequent input files as having type <language>">,
  MetaVarName<"<language>">;
def std_EQ : Joined<["-", "--"], "std=">, Flags<[InstallAPIOption, ReexportOption, GenerateAPITestsOption]>,
  HelpText<"Language standard to compile for">;
def ObjC : Flag<["-"], "ObjC">, Flags<[InstallAPIOption, ReexportOption, GenerateAPITestsOption]>,
  HelpText<"Treat source input files as Objective-C inputs">;
def ObjCXX : Flag<["-"], "ObjC++">, Flags<[InstallAPIOption, ReexportOption, GenerateAPITestsOption]>,
  HelpText<"Treat source input files as Objective-C++ inputs">;
def isysroot : JoinedOrSeparate<["-"], "isysroot">,
  Flags<[StubOption, InstallAPIOption, ReexportOption, GenerateAPITestsOption]>,
  HelpText<"Set the system root directory (usually /)">, MetaVarName<"<dir>">;
def iframework : JoinedOrSeparate<["-"], "iframework">,
  Flags<[StubOption,InstallAPIOption,ReexportOption]>,
  HelpText<"Add directory to SYSTEM framework search path">;
def isystem : JoinedOrSeparate<["-"], "isystem">,
  Flags<[InstallAPIOption,ReexportOption]>,
  HelpText<"Add directory to SYSTEM include search path">, MetaVarName<"<directory>">;
def D : JoinedOrSeparate<["-"], "D">, Flags<[InstallAPIOption, ReexportOption, GenerateAPITestsOption]>,
  HelpText<"Define macro">;
def U : JoinedOrSeparate<["-"], "U">, Flags<[InstallAPIOption, ReexportOption, GenerateAPITestsOption]>,
  HelpText<"Undefine macro">;
def F : JoinedOrSeparate<["-"], "F">, Flags<[StubOption, InstallAPIOption, ReexportOption, GenerateAPITestsOption]>,
  HelpText<"Add directory to framework include search path">;
def L : JoinedOrSeparate<["-"], "L">, Flags<[StubOption, InstallAPIOption]>,
  HelpText<"Add directory to the library search path">;
def I : JoinedOrSeparate<["-"], "I">, Flags<[InstallAPIOption, ReexportOption, GenerateAPITestsOption]>,
  HelpText<"Add directory to include search path">;
def Xparser : Separate<["-"], "Xparser">, Flags<[InstallAPIOption, ReexportOption, GenerateAPITestsOption]>,
  HelpText<"Pass <arg> to the clang parser">, MetaVarName<"<arg>">;
def output : Separate<["-"], "o">,
  Flags<[ArchiveOption, StubOption, InstallAPIOption, ReexportOption, GenerateAPITestsOption]>,
  MetaVarName<"<file>">, HelpText<"Write output to <file>">;
def extra_public_header : Separate<["-"], "extra-public-header">,
  Flags<[InstallAPIOption]>, MetaVarName<"<path>">,
  HelpText<"Add additional public header location for parsing">;
def extra_public_header_EQ : Joined<["--"], "extra-public-header=">,
  Flags<[InstallAPIOption]>, Alias<extra_public_header>;
def extra_private_header : Separate<["-"], "extra-private-header">,
  Flags<[InstallAPIOption]>, MetaVarName<"<path>">,
  HelpText<"Add additional private header location for parsing">;
def extra_private_header_EQ : Joined<["--"], "extra-private-header=">,
  Flags<[InstallAPIOption]>, Alias<extra_private_header>;
def exclude_public_header : Separate<["-"], "exclude-public-header">,
  Flags<[InstallAPIOption]>, MetaVarName<"<glob>">,
  HelpText<"Exclude public header from parsing">;
def exclude_public_header_EQ : Joined<["--"], "exclude-public-header=">,
  Flags<[InstallAPIOption]>, Alias<exclude_public_header>;
def exclude_private_header : Separate<["-"], "exclude-private-header">,
  Flags<[InstallAPIOption]>, MetaVarName<"<glob>">,
  HelpText<"Exclude private header from parsing">;
def exclude_private_header_EQ : Joined<["--"], "exclude-private-header=">,
  Flags<[InstallAPIOption]>, Alias<exclude_private_header>;
def public_umbrella_header : Separate<["-"], "public-umbrella-header">,
  Flags<[InstallAPIOption]>, MetaVarName<"<path>">,
  HelpText<"Specify the public umbrella header location">;
def public_umbrella_header_EQ : Joined<["--"], "public-umbrella-header=">,
  Flags<[InstallAPIOption]>, Alias<public_umbrella_header>;
def private_umbrella_header : Separate<["-"], "private-umbrella-header">,
  Flags<[InstallAPIOption]>, MetaVarName<"<path>">,
  HelpText<"Specify the private umbrella header location">;
def private_umbrella_header_EQ : Joined<["--"], "private-umbrella-header=">,
  Flags<[InstallAPIOption]>, Alias<private_umbrella_header>;
def frtti : Flag<["-"], "frtti">, Flags<[InstallAPIOption,ReexportOption]>;
def fno_rtti : Flag<["-"], "fno-rtti">, Flags<[InstallAPIOption,ReexportOption]>,
  HelpText<"Disable generation of rtti information">;
def fvisibility_EQ : Joined<["-"], "fvisibility=">,
  Flags<[InstallAPIOption,ReexportOption]>,
  HelpText<"Set the default symbol visibility for all global declarations">;
def inlinePrivateFrameworks : Flag<["--"], "inline-private-frameworks">,
  Flags<[StubOption,InstallAPIOption]>, HelpText<"Inline private frameworks">;

def fmodules : Flag <["-"], "fmodules">, Flags<[InstallAPIOption]>,
  HelpText<"Enable the 'modules' language feature">;
def fmodules_cache_path : Joined<["-"], "fmodules-cache-path=">,
  Flags<[InstallAPIOption]>, MetaVarName<"<directory>">,
  HelpText<"Specify the module cache path">;
def fmodules_validate_system_headers : Flag<["-"], "fmodules-validate-system-headers">,
  Flags<[InstallAPIOption]>,
  HelpText<"Validate the system headers that a module depends on when loading the module">;

def fobjc_arc : Flag<["-"], "fobjc-arc">,
  Flags<[InstallAPIOption,ReexportOption]>,
  HelpText<"Synthesize retain and release calls for Objective-C pointers">;
def fobjc_weak : Flag<["-"], "fobjc-weak">,
  Flags<[InstallAPIOption,ReexportOption]>,
  HelpText<"Enable ARC-style weak references in Objective-C">;

def noUUIDs : Flag<["--"], "no-uuids">, Flags<[StubOption,InstallAPIOption]>,
  HelpText<"Don't record the UUIDs from the library in the text-based stub file">;

def filetype : Joined<["--"], "filetype=">, Flags<[StubOption,InstallAPIOption]>,
  HelpText<"Specify the output file type (tbd-v1, tbd-v2, or tbd-v3)">;

def umbrella : Separate<["-"], "umbrella">,
  Flags<[InstallAPIOption]>, MetaVarName<"<framework_name>">,
  HelpText<"Specifies that the dylib being linked is re-exported through an umbrella framework of the specified name.">;

def Xarch__ : Joined<["-"], "Xarch_">, Flags<[InstallAPIOption]>;

def inferIncludePaths : Flag<["--"], "infer-include-paths">, Flags<[InstallAPIOption]>,
  HelpText<"Infer include paths based on the provided/found header files">;
def noInferIncludePaths : Flag<["--"], "no-infer-include-paths">, Flags<[InstallAPIOption]>,
  HelpText<"Don't infer include paths based on the provided/found header files">;

def verbose : Flag<["-"], "v">, Flags<[InstallAPIOption]>,
  HelpText<"Verbose output, show scan content and driver options">;

//
// Stubifier options
//
def deleteInputFile : Flag<["--"], "delete-input-file">, Flags<[StubOption]>,
  HelpText<"Delete input file on success">;
def deletePrivateFrameworks : Flag<["--"], "delete-private-frameworks">,
  Flags<[StubOption]>, HelpText<"Delete private frameworks from the SDK">;
def setInstallAPI : Flag<["--"], "set-installapi-flag">, Flags<[StubOption]>,
  HelpText<"Set the installapi flag in the text-based stub file">;

//
// InstallAPI options
//
def target_variant : Joined<["--"], "target-variant=">,
  Flags<[InstallAPIOption]>,
  HelpText<"Generate code for the given target variant">;
def target_variant_legacy_spelling : Separate<["-"], "target-variant">,
  Flags<[InstallAPIOption]>, Alias<target_variant>;
def install_name : Separate<["-"], "install_name">, Flags<[InstallAPIOption]>,
  HelpText<"Sets an internal \"install path\" (LC_ID_DYLIB) in a dynamic library">,
  MetaVarName<"<path>">;
def current_version : Separate<["-"], "current_version">,
  Flags<[InstallAPIOption]>,
  HelpText<"Specifies the current version number of the library">;
def compatibility_version : Separate<["-"], "compatibility_version">,
  Flags<[InstallAPIOption]>,
  HelpText<"Specifies the compatibility version number of the library">;
def allowable_client : Separate<["-"], "allowable_client">,
  Flags<[InstallAPIOption]>,
  HelpText<"Restricts what can link against the dynamic library being created">;
def reexport_install_name : Separate<["-"], "reexport_install_name">,
  MetaVarName<"<name>">, Flags<[InstallAPIOption]>,
  HelpText<"Re-export the specified internal \"install path\" (LC_ID_DYLIB)">;
def _serialize_diags : Separate<["-", "--"], "serialize-diagnostics">,
  Flags<[InstallAPIOption]>,
  HelpText<"Serialize compiler diagnostics to a file">;
def verify_against : Separate<["-"], "verify-against">,
  Flags<[InstallAPIOption]>,
  HelpText<"Verify the specified dynamic library/framework against the headers">;
def verify_against_EQ : Joined<["--"], "verify-against=">,
  Flags<[InstallAPIOption]>, Alias<verify_against>;
def verify_mode_EQ : Joined<["--"], "verify-mode=">,
  Flags<[InstallAPIOption]>,
  HelpText<"Specify the severity and extend of the validation. Valid modes are ErrorsOnly, ErrorsAndWarnings, and Pedantic.">;
def ferror_limit : Separate<["-"], "ferror-limit">, Flags<[InstallAPIOption]>,
  MetaVarName<"<N>">,
  HelpText<"Set the maximum number of errors to emit before stopping (0 = no limit).">;
def fapplication_extension : Flag<["-"], "fapplication-extension">,
  Flags<[InstallAPIOption]>,
  HelpText<"Restrict code to those available for App Extensions">;
def fno_application_extension : Flag<["-"], "fno-application-extension">,
  Flags<[InstallAPIOption]>;
def demangle : Flag<["--", "-"], "demangle">,
  Flags<[InstallAPIOption]>,
  HelpText<"Demangle C++ symbols when printing warnings and errors">;
def dynamiclib : Flag<["-"], "dynamiclib">, Flags<[InstallAPIOption]>;
def fprofile_instr_generate : Flag<["-"], "fprofile-instr-generate">,
  Flags<[InstallAPIOption]>,
  HelpText<"Add extra symbols for InstallAPI that are created by code coverage.">;
def print_after_EQ : Joined<["--"], "print-after=">,
    Flags<[InstallAPIOption]>;

def reexport_l : Joined<["-"], "reexport-l">,
  Flags<[InstallAPIOption]>, HelpText<"Re-export the specified library">;
def reexport_library : Separate<["-"], "reexport_library">, MetaVarName<"<path>">,
  Flags<[InstallAPIOption]>, HelpText<"Re-export the specified library">;
def reexport_framework : Separate<["-"], "reexport_framework">,
  Flags<[InstallAPIOption]>, HelpText<"Re-export the specified framework">;

def verify_api : Flag<["--"], "verify-api">, Flags<[InstallAPIOption]>,
  HelpText<"Verify the API of zippered frameworks">;
def no_verify_api : Flag<["--"], "no-verify-api">, Flags<[InstallAPIOption]>;
def verify_api_skip_external_headers : Flag<["--"], "verify-api-skip-external-headers">,
  Flags<[InstallAPIOption]>,
  HelpText<"Skip external headers for API verification of zippered frameworks">;
def no_verify_api_skip_external_headers : Flag<["--"], "no-verify-api-skip-external-headers">,
  Flags<[InstallAPIOption]>;
def verify_api_error_as_warning : Flag<["--"], "verify-api-error-as-warning">,
  Flags<[InstallAPIOption]>, HelpText<"Emit API Verification errors as warnings">;

def alias_list : Separate<["-"], "alias_list">,
  Flags<[InstallAPIOption]>, MetaVarName<"<path>">,
  HelpText<"Specify the path to a file containing a list of symbol aliases">;

def file_list_EQ : Joined<["--"], "filelist=">,
  Flags<[InstallAPIOption]>, MetaVarName<"<path>">,
  HelpText<"Specifies the <path> to a file that contains a list of headers to parse.">;
def file_list : Separate<["-"], "filelist">,
  Flags<[InstallAPIOption]>, Alias<file_list_EQ>;

def extra_project_header : Separate<["-"], "extra-project-header">,
  Flags<[InstallAPIOption]>, MetaVarName<"<path>">,
  HelpText<"Add additional project header location for parsing">;
def extra_project_header_EQ : Joined<["--"], "extra-project-header=">,
  Flags<[InstallAPIOption]>, Alias<extra_project_header>;
def exclude_project_header : Separate<["-"], "exclude-project-header">,
  Flags<[InstallAPIOption]>, MetaVarName<"<glob>">,
  HelpText<"Exclude project header from parsing">;
def exclude_project_header_EQ : Joined<["--"], "exclude-project-header=">,
  Flags<[InstallAPIOption]>, Alias<exclude_project_header>;

def ivfsoverlay : JoinedOrSeparate<["-"], "ivfsoverlay">,
  Flags<[InstallAPIOption]>,
  HelpText<"Overlay the virtual filesystem described by file over the real file system">;
