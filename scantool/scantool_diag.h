#ifndef SCANTOOL_DIAG_H
#define SCANTOOL_DIAG_H

// Hack, so scantool_cli can invoke 'disconnect' on exit.

//Currently, this stops + removes the current global L3 conn.
//If there are no more L3 conns, also stop + close the global L2 conn.
int cmd_diag_disconnect(int argc, char **argv);

#endif