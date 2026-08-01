#pragma once

// Offsets come from the AOB patterns compiled into the lib -- there is no address file to load
// and no server to ask, so a client patch is fixed by re-deriving signatures and rebuilding.

// PatternScanner only: write the scanned addresses to a file next to the exe.
//#define DUMP_TO_FILE
#define ADRESS_FILE_NAME "addresses.csv"
