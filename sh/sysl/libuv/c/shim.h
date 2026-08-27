/* What only C can reach in libuv's surface, flattened to scalars and pointers.
 *
 * Three shapes are here and nothing else is: a struct whose FIELD ORDER is the platform's rather
 * than libuv's (`struct sockaddr`, `struct addrinfo`), a struct libuv declares with a UNION in it
 * (`uv_stdio_container_t`), and `uv_buf_t`, whose two fields are declared in one order on Unix and
 * the other on Windows.  Everything else in this binding is an `extern` over libuv's own symbol.
 *
 * Sizes are NOT answered here.  A `c const` block measures `sizeof` for the target being built for,
 * which is the same answer with nothing to keep in step.
 */

#ifndef SYSL_UV_SHIM_H
#define SYSL_UV_SHIM_H

#include <uv.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stddef.h>
#include <sys/socket.h>

/* `uv_buf_t` — `{ char *base; size_t len; }` on Unix and the two the other way round on Windows,
 * which is why nothing above reads it as a sysl struct.  `uv_buf_init` returns one by value and
 * cannot fill the one an alloc callback is handed. */
void sysl_uv_buf_set(uv_buf_t *buf, char *base, size_t len);
char *sysl_uv_buf_base(const uv_buf_t *buf);
size_t sysl_uv_buf_len(const uv_buf_t *buf);

/* `struct sockaddr` — the family is at a different offset on a BSD, which puts a length byte first.
 * libuv answers for the address itself with `uv_ip_name`; the port and the family are left. */
int sysl_uv_sockaddr_family(const struct sockaddr *addr);
int sysl_uv_sockaddr_port(const struct sockaddr *addr);

/* `struct addrinfo` — read for a resolver's answer, and written for its hints. */
void sysl_uv_hints_init(struct addrinfo *hints, int family, int socktype, int protocol, int flags);
const struct addrinfo *sysl_uv_ai_next(const struct addrinfo *ai);
int sysl_uv_ai_family(const struct addrinfo *ai);
int sysl_uv_ai_socktype(const struct addrinfo *ai);
int sysl_uv_ai_protocol(const struct addrinfo *ai);
const struct sockaddr *sysl_uv_ai_addr(const struct addrinfo *ai);
const char *sysl_uv_ai_canonname(const struct addrinfo *ai);

/* `uv_stdio_container_t` — a flag word and a union of a stream pointer and a descriptor. */
void sysl_uv_stdio_set_fd(uv_stdio_container_t *stdio, int i, int flags, int fd);
void sysl_uv_stdio_set_stream(uv_stdio_container_t *stdio, int i, int flags, uv_stream_t *stream);

#endif /* SYSL_UV_SHIM_H */
