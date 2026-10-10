#include "CProc.h"

#include <arpa/inet.h>
#include <libproc.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

int procspy_copy_path(int pid, char *buf, int cap) {
    if (buf == NULL || cap < 2 || pid <= 0) {
        return -1;
    }
    int n = proc_pidpath(pid, buf, (uint32_t)cap);
    if (n <= 0) {
        buf[0] = '\0';
        return -1;
    }
    buf[cap - 1] = '\0';
    return (int)strlen(buf);
}

int procspy_list_pids(int *buf, int cap) {
    if (buf == NULL || cap <= 0) {
        return 0;
    }
    int bytes = proc_listallpids(buf, cap * (int)sizeof(int));
    if (bytes <= 0) {
        return 0;
    }
    int count = bytes / (int)sizeof(int);
    if (count > cap) {
        count = cap;
    }
    return count;
}

static int append_ip(char *out, int cap, int used, const char *ip) {
    if (ip == NULL || ip[0] == '\0' || strcmp(ip, "0.0.0.0") == 0 || strcmp(ip, "::") == 0) {
        return used;
    }
    size_t len = strlen(ip);
    if (used + (int)len + 2 >= cap) {
        return used;
    }
    if (used > 0) {
        out[used++] = '\n';
    }
    memcpy(out + used, ip, len);
    used += (int)len;
    out[used] = '\0';
    return used;
}

int procspy_copy_remotes(int pid, char *buf, int cap) {
    if (buf == NULL || cap < 2 || pid <= 0) {
        return 0;
    }
    buf[0] = '\0';

    int size = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, NULL, 0);
    if (size <= 0) {
        return 0;
    }

    struct proc_fdinfo *fds = malloc((size_t)size);
    if (fds == NULL) {
        return 0;
    }
    int got = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, fds, size);
    if (got <= 0) {
        free(fds);
        return 0;
    }

    int count = got / (int)sizeof(struct proc_fdinfo);
    int used = 0;

    for (int i = 0; i < count; i++) {
        if (fds[i].proc_fdtype != PROX_FDTYPE_SOCKET) {
            continue;
        }
        struct socket_fdinfo info;
        memset(&info, 0, sizeof(info));
        int rc = proc_pidfdinfo(pid, fds[i].proc_fd, PROC_PIDFDSOCKETINFO, &info, (int)sizeof(info));
        if (rc <= 0) {
            continue;
        }
        int family = info.psi.soi_family;
        if (family != AF_INET && family != AF_INET6) {
            continue;
        }

        struct in_sockinfo *in = NULL;
        if (info.psi.soi_kind == SOCKINFO_TCP) {
            in = &info.psi.soi_proto.pri_tcp.tcpsi_ini;
        } else if (info.psi.soi_kind == SOCKINFO_IN) {
            in = &info.psi.soi_proto.pri_in;
        } else {
            continue;
        }
        if (in->insi_fport == 0) {
            continue;
        }

        char ip[INET6_ADDRSTRLEN];
        ip[0] = '\0';
        if (in->insi_vflag & INI_IPV4) {
            if (inet_ntop(AF_INET, &in->insi_faddr.ina_46.i46a_addr4, ip, sizeof(ip)) == NULL) {
                continue;
            }
        } else if (in->insi_vflag & INI_IPV6) {
            if (inet_ntop(AF_INET6, &in->insi_faddr.ina_6, ip, sizeof(ip)) == NULL) {
                continue;
            }
        } else {
            continue;
        }
        used = append_ip(buf, cap, used, ip);
    }

    free(fds);
    return used;
}
