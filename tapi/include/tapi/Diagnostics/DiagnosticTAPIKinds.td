//==--- DiagnosticTAPIKinds.td - TAPI diagnostics -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Define the diagnostic severities.
class Severity<string N> {
  string Name = N;
}
def SEV_Ignored : Severity<"Ignored">;
def SEV_Remark  : Severity<"Remark">;
def SEV_Warning : Severity<"Warning">;
def SEV_Error   : Severity<"Error">;
def SEV_Fatal   : Severity<"Fatal">;

// Define the diagnostic classes.
class DiagClass;
def CLASS_NOTE      : DiagClass;
def CLASS_REMARK    : DiagClass;
def CLASS_WARNING   : DiagClass;
def CLASS_EXTENSION : DiagClass;
def CLASS_ERROR     : DiagClass;

// Responses to a diagnostic in a SFINAE context.
class SFINAEResponse;
def SFINAE_Suppress            : SFINAEResponse;

// Textual substitutions which may be performed on the text of diagnostics
class TextSubstitution<string Text> {
  string Substitution = Text;
  // TODO: These are only here to allow substitutions to be declared inline with
  // diagnostics
  string Component = "";
  string CategoryName = "";
}

// Diagnostic Categories.  These can be applied to groups or individual
// diagnostics to specify a category.
class DiagCategory<string Name> {
  string CategoryName = Name;
}

// Diagnostic Groups.
class DiagGroup<string Name, list<DiagGroup> subgroups = []> {
  string GroupName = Name;
  list<DiagGroup> SubGroups = subgroups;
  string CategoryName = "";
  code Documentation = [{}];
}
class InGroup<DiagGroup G> { DiagGroup Group = G; }

// All diagnostics emitted by the compiler are an indirect subclass of this.
class Diagnostic<string text, DiagClass DC, Severity defaultmapping> {
  string         Component = ?;
  string         Text = text;
  DiagClass      Class = DC;
  SFINAEResponse SFINAE = SFINAE_Suppress;
  bit            AccessControl = 0;
  bit            WarningNoWerror = 0;
  bit            ShowInSystemHeader = 0;
  Severity       DefaultSeverity = defaultmapping;
  DiagGroup      Group;
  string         CategoryName = "";
}

class Error<string str>     : Diagnostic<str, CLASS_ERROR, SEV_Error>;
class Warning<string str>   : Diagnostic<str, CLASS_WARNING, SEV_Warning>;
class Note<string str>      : Diagnostic<str, CLASS_NOTE, SEV_Fatal/*ignored*/>;

let Component = "TAPI" in {

let CategoryName = "Command Line Issue" in {

def err : Error<"%0: '%1'">;
def err_no_architecture : Error<"no architecture specified: add -arch <architecture>">;
def err_no_target : Error<"no target specified: add --target=<triple>">;
def err_one_architecture : Error<"only one architecture can be specified '%0'">;
def err_one_target : Error<"only one target can be specified">;
def err_no_matching_target : Error<"no matching target found for target variant '%0'">;
def err_unsupported_vendor : Error<"vendor '%0' is not supported: '%1'">;
def err_unsupported_os : Error<"os '%0' is not supported: '%1'">;
def err_unsupported_environment : Error<"environment '%0' is not supported: '%1'">;
def err_no_install_name : Error<"no install name specified: add -install_name <path>">;
def err_invalid_current_version : Error<"invalid current version '%0'">;
def err_invalid_compatibility_version : Error<"invalid compatibility version '%0'">;
def err_no_deployment_target : Error<"no deployment target specified">;
def err_missing_sysroot : Error<"no such sysroot directory: '%0'">;
def err_cannot_open_file : Error<"cannot open file: '%0'">;
def err_no_such_header_file : Error<"no such %select{public|private|project}1 header file: '%0'">;
def err_no_such_symbols_file : Error<"no such symbols file: '%0'">;
def err_invalid_input_file : Error<"invalid input file: '%0'">;
def err_no_framework : Error<"no framework/dynamic library found">;
def err_more_than_one_framework : Error<" more than one framework/dynamic library found">;
def err_no_such_umbrella_header_file : Error<"no such %select{public|private|project}1 umbrella header file: '%0'">;
def err_cannot_read_file : Error<"cannot read file '%0': %1">;
def err_cannot_write_file : Error<"cannot write file '%0': %1">;
def err_cannot_create_directory : Error<"cannot create directory '%0': %1">;
def err_cannot_find : Error<"cannot find %0: %1">;
def err_platform_mismatch : Error<"platform does not match: '%0' (provided) vs '%1' (found)">;
def warn_platform_mismatch : Warning<"platform does not match: '%0' (provided) vs '%1' (found)">;
def err_architecture_mismatch : Error<"architectures do not match: '%0' (provided) vs '%1' (found)">;
def err_install_name_mismatch : Error<"install_name does not match: '%0' (provided) vs '%1' (found)">;
def err_current_version_mismatch : Error<"current_version does not match: '%0' (provided) vs '%1' (found)">;
def err_compatibility_version_mismatch : Error<"compatibility_version does not match: '%0' (provided) vs '%1' (found)">;
def err_appextension_safe_mismatch : Error<"ApplicationExtensionSafe flag does not match: '%0' (provided) vs '%1' (found)">;
def err_reexported_libraries_missing : Error<"re-exported library missing from %0: '%1'">;
def err_reexported_libraries_mismatch : Error<"re-exported libraries do not match: '%0' (provided) vs '%1' (found)">;
def err_allowable_clients_missing : Error<"allowable client missing from %0: '%1'">;
def err_allowable_clients_mismatch : Error<"allowable clients do not match: '%0' (provided) vs '%1' (found)">;
def warn_parent_umbrella_mismatch : Warning<"parent umbrella does not match: '%0' (provided) vs '%1' (found)">;
def err_no_twolevel_namespace : Error<"flat namespace libraries are not supported">;
def err_library_missing_symbol : Error<"library doesn't have symbol '%0'">;
def err_header_symbol_tlv_mismatch : Error<"header symbol '%0' is thread local, but dynamic library symbol '%1' is not">;
def err_dylib_symbol_tlv_mismatch : Error<"dynamic library symbol '%0' is thread local, but header symbol '%1' is not">;
def err_header_symbol_weak_mismatch : Error<"header symbol '%0' is weak defined, but dynamic library symbol '%1' is not">;
def err_dylib_symbol_weak_mismatch : Error<"dynamic library symbol '%0' is weak defined, but header symbol '%1' is not">;
def err_availability_mismatch : Error<"symbol '%0' doesn't have the same availability information: '%1' (header) vs '%2' (found)">;
def err_parsing_disabled : Error<"parsing of all headers disabled">;
def err_no_directory : Error<"input '%0' is not a directory">;
def err_not_a_dylib : Error<"input '%0' is not a dynamic library">;
def err_cannot_convert_dylib : Error<"cannot convert '%0' to a text-based stub file">;
def err_cannot_generate_stub : Error<"cannot generate text-based stub file">;
def err_cannot_find_reexport : Error<"cannot find re-exported library '%0'">;
def err_property_mismatch : Error<"%0 in '%1' does not match '%2'">;
def err_not_all_architectures: Error<"dynamic library '%0' doesn't have all the required architectures">;
def err_expected_one_input_file: Error<"expected only one input file">;
def err_expected_option: Error<"cannot infer '%0', please specify with option '%1'">;
def err_unsupported_file_type: Error<"unsupported file type">;
def err_no_output_file: Error<"no output file specified">;
def err_header_symbol_missing : Error<"headers don't have symbol '%0'">;
def err_cannot_find_header_dir: Error<"cannot find any public (usr/include) or private (usr/local/include) header directory: '%0'">;
def err_missing_vfs_overlay_file : Error<"virtual filesystem overlay file '%0' not found">;
def err_invalid_vfs_overlay : Error<"invalid virtual filesystem overlay file '%0'">;
def err_invalid_verifier_whitelist_file: Error<"cannot parse whitelist file %0: %1">;
def err_inlining_not_supported: Error<"inlining is not supported for output file type '%0'">;

def warn : Warning<"%0">;
def warn_truncating_current_version : Warning<
    "truncating current version '%0' to fit in 32-bit space used by old mach-o format">;
def warn_symbol_unavailable : Warning<"header marked symbol '%0' as unavailable">;
def warn_broken_symlink : Warning<"ignoring broken symlink '%0'">;
def warn_header_symbol_missing : Warning<"headers don't have symbol '%0'">;
def warn_glob_did_not_match: Warning<"glob '%0' did not match any header file">;
def warn_no_such_header_file : Warning<"no such %select{public|private}0 header file: '%1'">;
def warn_no_such_pre_header_file : Warning<"no such %select{public|private}0 pre-include header file: '%1'">;
def warn_no_such_excluded_header_file : Warning<"no such excluded %select{public|private}0 header file: '%1'">;
def warn_no_such_umbrella_header_file : Warning<"no such %select{public|private}0 umbrella header file: '%1'">;

def warn_auto_zippered : Warning<"Auto zippering was requested, output will be zippered">;

} // end of command line category.

let CategoryName = "API Verifier" in {

def warn_api_inconsistent : Warning<"%0 has incompatible definitions">;
def note_api_var : Note<"variable %0 has type %1 here">;
def note_api_return : Note<"return value has type %0 here">;
def note_api_param : Note<"parameter has type %0 here">;
def note_api_missing_param : Note<"no corresponding parameter here">;
def note_api_interface : Note<"interface %0 has type %1 here">;
def note_api_interface_superclass : Note<"interface %0 has super class %1 here">;
def note_api_missing_superclass : Note<"interface %0 has no corresponding super class here">;
def note_api_inconsistent_superclass : Note<"interface %0 has inconsistent super class here">;
def note_api_protocol : Note<"%0 conforms to protocol %1">;
def note_api_missing_protocol : Note<"%0 has no corresponding protocol here">;
def note_api_method : Note<"%0 has method %1 here">;
def note_api_missing_method : Note<"%0 has no corresponding method here">;
def note_api_property : Note<"%0 has property %1 here">;
def note_api_missing_property : Note<"%0 has no corresponding property here">;
def note_api_property_type : Note<"property %0 has type %1 here">;
def note_api_property_attribute : Note<"property %0 has attribute here">;
def note_api_ivar : Note<"%0 has ivar %1 here">;
def note_api_missing_ivar : Note<"%0 has no corresponding ivar here">;
def note_api_type_def : Note<"%0 is defined to type %1 here">;
def note_api_enumerator : Note<"enumerator %0 with value %1 here">;
def note_api_enum_type : Note<"enum has type %0 here">;
def note_api_field_name : Note<"field has name %0 here">;
def note_api_field_type : Note<"field %0 has type %1 here">;
def note_api_field_bitfield : Note<"field %0 %select{is not|is}1 bit field here">;
def note_api_field_bitfield_width : Note<"bit field %0 has width %1 here">;
def note_api_field : Note<"%0 has field %1 here">;
def note_api_field_missing : Note<"%0 has no corresponding field here">;
def note_api_base_class : Note<"%0 has base class %1 here">;
def note_api_base_class_num : Note<"%0 has %1 base class(es) here">;
def note_api_base_class_virtual : Note<"base class %0 %select{is not|is}1 virtual here">;
def note_api_base_class_access : Note<"base class %0 has access specifier '%select{public|protected|private|none}1' here">;
def note_api_decl_name : Note<"declaration has name %0 here">;

def note_api_target_note : Note<"for target %0">;

def warn_api_incomplete : Warning<"%0 is missing from target %1 but required for target %2">;

} // end of API verifier category.

} // end of TAPI component.
