#include "shim.h"

#include <string.h>

void sysl_uv_buf_set(uv_buf_t *buf, char *base, size_t len) {
  buf->base = base;
  buf->len = len;
}

char *sysl_uv_buf_base(const uv_buf_t *buf) { return buf->base; }

size_t sysl_uv_buf_len(const uv_buf_t *buf) { return buf->len; }

int sysl_uv_sockaddr_family(const struct sockaddr *addr) { return addr->sa_family; }

/* Host byte order, which is what a caller means by "which port".  The two families keep it in the
 * same place and in the same order, so one branch answers for both. */
int sysl_uv_sockaddr_port(const struct sockaddr *addr) {
  if (addr->sa_family == AF_INET)
    return ntohs(((const struct sockaddr_in *)addr)->sin_port);

  if (addr->sa_family == AF_INET6)
    return ntohs(((const struct sockaddr_in6 *)addr)->sin6_port);

  return 0;
}

void sysl_uv_hints_init(struct addrinfo *hints, int family, int socktype, int protocol, int flags) {
  memset(hints, 0, sizeof(*hints));
  hints->ai_family = family;
  hints->ai_socktype = socktype;
  hints->ai_protocol = protocol;
  hints->ai_flags = flags;
}

const struct addrinfo *sysl_uv_ai_next(const struct addrinfo *ai) { return ai->ai_next; }

int sysl_uv_ai_family(const struct addrinfo *ai) { return ai->ai_family; }

int sysl_uv_ai_socktype(const struct addrinfo *ai) { return ai->ai_socktype; }

int sysl_uv_ai_protocol(const struct addrinfo *ai) { return ai->ai_protocol; }

const struct sockaddr *sysl_uv_ai_addr(const struct addrinfo *ai) { return ai->ai_addr; }

const char *sysl_uv_ai_canonname(const struct addrinfo *ai) { return ai->ai_canonname; }

void sysl_uv_stdio_set_fd(uv_stdio_container_t *stdio, int i, int flags, int fd) {
  stdio[i].flags = (uv_stdio_flags)flags;
  stdio[i].data.fd = fd;
}

void sysl_uv_stdio_set_stream(uv_stdio_container_t *stdio, int i, int flags, uv_stream_t *stream) {
  stdio[i].flags = (uv_stdio_flags)flags;
  stdio[i].data.stream = stream;
}
