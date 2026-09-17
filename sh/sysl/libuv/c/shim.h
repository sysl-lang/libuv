/* What only C can reach in libuv's surface, flattened to scalars and pointers.
 *
 * Three shapes are here: a struct whose FIELD ORDER is the platform's rather than libuv's
 * (`struct sockaddr`, `struct addrinfo`), a struct libuv declares with a UNION in it
 * (`uv_stdio_container_t`), and `uv_buf_t`, whose two fields are declared in one order on Unix and
 * the other on Windows.
 *
 * Beside them are the few calls whose SIGNATURE sysl cannot spell: a variadic one
 * (`uv_loop_configure`), one that answers with a platform type by value (`uv_thread_self`), and one
 * whose options struct is positional (`uv_thread_create_ex`).  Everything else in this binding is an
 * `extern` over libuv's own symbol.
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

/* How many bytes the address above really is.  A resolver's answer points at a `sockaddr_in` or a
 * `sockaddr_in6`, which are 16 and 28 bytes here, and NOT at the 128-byte `sockaddr_storage` every
 * other address in this binding lives in -- so a reader that copies a whole storage out of one
 * reads off the end of the resolver's allocation. */
size_t sysl_uv_ai_addrlen(const struct addrinfo *ai);
const char *sysl_uv_ai_canonname(const struct addrinfo *ai);

/* Writing to a socket whose peer has gone raises SIGPIPE, whose default action ends the process --
 * so a server that never asked for it dies on a client that hung up.  Ignoring it is what turns
 * that into the `EPIPE` libuv reports through the write callback, and it is a `signal` call rather
 * than anything of libuv's. */
void sysl_uv_ignore_sigpipe(void);

/* `uv_loop_configure` is variadic, which sysl has no way to spell.  Every option it takes wants at
 * most one integer, and one it takes none for does not read the argument -- so a fixed signature
 * answers for all of them. */
int sysl_uv_loop_configure(uv_loop_t *loop, int option, int value);

/* `uv_stdio_container_t` — a flag word and a union of a stream pointer and a descriptor. */
void sysl_uv_stdio_set_fd(uv_stdio_container_t *stdio, int i, int flags, int fd);
void sysl_uv_stdio_set_stream(uv_stdio_container_t *stdio, int i, int flags, uv_stream_t *stream);

/* `uv_thread_self` answers with a `uv_thread_t` BY VALUE, and that type is the platform's own -- a
 * `pthread_t` here and a `HANDLE` on Windows -- so there is nothing above for it to be received
 * into.  Writing it through a pointer is the same answer in a shape sysl can hold; every other call
 * in the family already takes a pointer and needs nothing here. */
void sysl_uv_thread_self(uv_thread_t *out);

/* `uv_thread_create_ex` reads its options out of a `uv_thread_options_t`, whose two fields are
 * positional rather than independent: the stack size is looked at only when the flag asking for it
 * is set, and anything else in the struct has to be zero.  Writing the pair here keeps that
 * agreement in one place rather than in every caller. */
int sysl_uv_thread_create_ex(uv_thread_t *tid, unsigned int flags, size_t stack_size,
                             uv_thread_cb entry, void *arg);

#endif /* SYSL_UV_SHIM_H */
