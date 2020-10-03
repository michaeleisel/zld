#include "SpecialLinkerSymbols.h"

const int SpecialLinkerSymbolsVersion = 7;
int symbol1 = 77;
int symbol3 = 777;

// Test install name change.
const char install_name_10_4 __asm("$ld$install_name$os10.4$/System/Library/"
                                   "Frameworks/A.framework/Versions/A/A");
const char install_name_10_5 __asm("$ld$install_name$os10.5$/System/Library/"
                                   "Frameworks/B.framework/Versions/A/B");

// Test hide.
const char hide_symbol1_10_6 __asm("$ld$hide$os10.6$_symbol1");
const char hide_symbol1_10_7 __asm("$ld$hide$os10.7$_symbol1");

// Test add.
const char add_symbol1_10_4 __asm("$ld$add$os10.4$_symbol2");
const char add_symbol1_10_5 __asm("$ld$add$os10.5$_symbol2");

// Test weak.
const char weak_symbol3_10_4 __asm("$ld$weak$os10.4$_symbol3");
const char weak_symbol3_10_5 __asm("$ld$weak$os10.5$_symbol3");
