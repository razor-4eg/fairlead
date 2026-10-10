#ifndef CPROC_H
#define CPROC_H

/// Copies the executable path. Returns length excluding NUL, or -1.
int procspy_copy_path(int pid, char *buf, int cap);

/// Writes newline-separated remote IPv4/IPv6 addresses. Returns bytes written, excluding NUL.
int procspy_copy_remotes(int pid, char *buf, int cap);

/// Fills `buf` with pids. Returns the number of pids written.
int procspy_list_pids(int *buf, int cap);

#endif
