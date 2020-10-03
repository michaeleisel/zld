// simple alias from one symbol to another.
extern int simple_symbol;
extern int alias_symbol;

// This symbol comes from the alias file.
extern int exported_symbol;

// This symbol was moved here and has several special hide symbols in the alias
// file.
extern int moved_here_symbol;

// This alias is public, whereas the source is private.
extern int public_symbol;
