# libuv

libuv for sysl — the event loop Node.js is built on, with TCP, pipes, terminals, timers, signals,
child processes, name resolution and a file system that does not block.

```
dependencies {
  libuv { git = "github.com/sysl-lang/libuv", version = "0.1.0" }
}
```

```sysl
import sh.sysl.libuv.*

main()
    val lp = default_loop()
    val server = tcp(lp).expect("a socket")

    server.bind(ip4("0.0.0.0", 8080).expect("an address")).expect("bound")

    server.listen(128, () ->
        val conn = server.accept().expect("a connection")

        conn.read_start((r) -> r match
            Data(bytes) -> conn.write(bytes)
            End -> conn.close()
            Failed(_) -> conn.close()))

    print("listening on 8080")
    lp.run().expect("the loop ran")
```

`sysl run .` is the whole command: libuv installs a `libuv.pc` beside itself and the manifest asks
the machine where the headers and the library are.

## Call `ignore_sigpipe()` first

**A server that writes to a client which hung up is killed by `SIGPIPE`** — the default action for
it ends the process, with no diagnostic and with whatever was buffered thrown away. libuv does not
turn that off for you and neither does anything else, because it is process-wide state and so the
program's to set. One line at the top of `main` turns it into the `EPIPE` the write callback is
meant to carry:

```sysl
main()
    ignore_sigpipe()
    ...
```

This package's own suite has the test that found it: without the call, that test does not fail — it
ends the test runner.

## A handle stays alive until you close it

This is the one rule the rest of the package is built on, and it is worth reading before anything
else. libuv's loop owns the handles on it — it keeps them on its own queues and writes into them
from the backend — so **a handle holds a reference to itself from the moment it is created until
`close` finishes.** Three things follow, and all three are what a program wants:

```sysl
timer()?.start(1000, 0, () -> print("later"))     // nothing binds it, and it still fires
```

- A handle need not be bound to a name. The loop is holding it.
- Dropping the last name for a handle does not stop it, exactly as dropping a `setInterval` handle
  in JavaScript does not.
- **`close` is what ends it.** A handle never closed is a handle never freed — and also one the loop
  is still waiting for, so `run` never returns. That is libuv's own rule and this binding does not
  soften it.

## A callback is handed nothing

Every callback here takes only what it has to report — a `Read`, a status, a signal number — and
never the handle it belongs to. A closure captures, so a callback that needs the handle captures the
handle, which is the same reference the loop is holding and costs nothing:

```sysl
val t = timer()?

t.start(0, 100, () ->
    n.bump()
    if n.value == 3 then t.close())
```

**That closure and the handle refer to each other**, which is a cycle a counted language does not
collect. `close` is what breaks it: closing drops the callbacks and the handle's reference to itself,
so the box goes when the last name for it does. A program that closes its handles leaks nothing, and
one that does not was never going to return from `run` anyway.

**A closure captures by value**, so a counter a callback increments has to be behind a `&` for the
caller to see it afterwards:

```sysl
struct Counter
    n: int

val seen: &Counter = Counter(0)      // shared, because it is a reference

t.start(0, 10, () -> seen.n += 1)
```

## Two layers, and where the line falls

`sh.sysl.libuv.c` is libuv declared verbatim: every name and every convention is C's, so a handle is
storage the caller supplies, a failure is a negative integer, and a callback is one word of code with
a `void *` beside it. `sh.sysl.libuv` is what an application imports.

**What decides which module a thing goes in is whether an application has to name it, not whether it
is a C artifact.** libuv's numbers are `c const` blocks and they live in the *pleasant* module, in
`constants.sysl`, because a program spells `O_RDONLY` and `SIGINT`. What stayed below is the sizes:
the `[N]u64` a handle is carried in is the binding's business and no consumer ever writes one.

The split keeps two jobs apart. The lower one has to be **faithful** — a signature that disagrees
with the header links perfectly and corrupts the call at run time — so every declaration was read out
of `uv.h` rather than remembered. The upper one has to be **pleasant**, which is a different question
and would otherwise be answered in the same breath.

### The shim is three shapes and nothing else

`sh/sysl/libuv/c/shim.c` is forty lines, and each of them is one of the three things C can reach and
sysl cannot:

| | why |
|---|---|
| `struct sockaddr`, `struct addrinfo` | the field order is the **platform's**, not libuv's |
| `uv_stdio_container_t` | libuv declares it with a **union** in it |
| `uv_buf_t` | declared `{base, len}` on Unix and the other way round on Windows |

Sizes are not answered there: a `c const` block measures `sizeof` for the target being built for,
which is the same answer with nothing to keep in step. `tests.sysl` checks each measured size against
what the **linked library** reports at run time, which is what catches headers and a library that are
two different versions of libuv.

## Handles are `&T`, addresses are values

A handle carries libuv's own storage inline and must never move — libuv's queues are linked lists
through the handles on them — so every one of them is reached through `&T` and none is a value a
program can copy. An `Address`, by contrast, is a copy of some bytes with nothing pointing at it, so
it goes in a `val`, is passed by value and is returned:

```sysl
val a = ip4("127.0.0.1", 8080)?

print(a.ip(), a.port(), s"$a")       // 127.0.0.1 8080 127.0.0.1:8080
```

## Errors

Every libuv failure is one negative integer, and libuv carries its own name and sentence for each.
So an `Error` is the code, and the strings are asked for when somebody wants them:

```sysl
server.bind(addr) match
    Ok(_) -> ...
    Err(e) -> print(s"cannot bind: $e")      // EADDRINUSE: address already in use
```

Comparing is by code, which is what makes `if e.code == EOF` the one form a program acting on a
failure writes. The codes worth acting on are named in `constants.sysl`.

## What is here

| | |
|---|---|
| **loop** | `default_loop`, `new_loop`, `run`, `stop`, `alive`, `now`, `backend_fd`, `configure`, `idle_time` |
| **timers** | `timer`, `start`, `stop`, `again`, `set_repeat`, `due_in` |
| **watchers** | `idle`, `prepare`, `check` |
| **wake-ups** | `notifier` — the one handle another thread may touch |
| **signals** | `signal`, `start`, `start_once`, `kill`, `ignore_sigpipe` |
| **streams** | `read_start`, `write`, `try_write`, `shutdown`, `listen`, back-pressure |
| **TCP** | `tcp`, `bind`, `listen`, `accept`, `connect`, `nodelay`, `keepalive`, `sockname` |
| **pipes** | `pipe`, `pipe_pair`, `socket_pair`, and Unix domain sockets |
| **terminals** | `tty`, `set_mode`, `winsize`, `reset_tty_mode`, `guess_handle` |
| **names** | `resolve` — `getaddrinfo` on the thread pool |
| **files, blocking** | `open_sync`, `read_file_sync`, `write_file_sync`, `stat_sync`, `scandir_sync`, `symlink_sync`, … |
| **files, not** | `open`, `read`, `write`, `stat`, `scandir`, `read_file`, … |
| **children** | `spawn`, `Stdio`, `kill`, `on_exit` |
| **the machine** | `hrtime`, `hostname`, `cwd`, `env`, `available_parallelism`, memory, load |

**What is not bound yet**, and is ordinary work rather than anything blocked: UDP, `uv_poll_t`,
`uv_fs_event_t` and `uv_fs_poll_t`, the work queue (`uv_queue_work`), `getnameinfo`, `uv_random`,
threads and the locks that go with them, and `dlopen`. Each is a section of `uv.h` and each would be
added the way the ones above were.

## The file system, both ways round

A file system call blocks, however fast the disk is, so libuv runs the asynchronous form on its
thread pool and answers through the loop. The blocking form is the same call with no callback.

```sysl
val text = read_file_sync("config.hocon")?              // blocking, and usually right

read_file("big.dat", (r) -> r match                     // on the pool, answered on the loop
    Ok(bytes) -> handle(bytes)
    Err(e) -> print(s"cannot read: $e"))?
```

**The suffix is Node's**, which is the closest thing libuv has to a convention: the plain name is
asynchronous and `_sync` is the one that blocks. A server should reach for the plain one on any path
a request waits on, and nobody should reach for it to read a file at startup.

## Child processes

```sysl
val out = pipe()?

val child = spawn("git", ["rev-parse", "HEAD"], ignore_exit,
                  [Ignore, ToPipe(out, WRITABLE_PIPE), Inherit])?

child.on_exit((status, signal) ->
    ended.code = i32(status)
    child.close())

out.read_start((r) -> r match
    Data(bytes) -> collect(bytes)
    End -> out.close()
    Failed(_) -> out.close())
```

`args` does not carry the program's own name — libuv, like `execvp`, wants it first, and `spawn` puts
it there. **The pipe flags are written from the child's point of view**, which is libuv's convention
and the one thing here that is easy to get backwards: a pipe the parent *reads* is one the child
*writes*, so it is `WRITABLE_PIPE`.

The child is reaped by the loop, which is what `SIGCHLD` handling would otherwise be for.

## Installing libuv

```
brew install libuv             # macOS
sudo apt install libuv1-dev    # Debian / Ubuntu
sudo pacman -S libuv           # Arch
```

The build needs no flags: `libuv.pc` is installed beside the library everywhere it ships, and the
manifest's `pkg_config` requirement asks for it. `--include-path libuv=<dir>` and `--link-path <dir>`
still answer it by hand, for a hermetic build or a prefix pkg-config has never heard of.

**This package is `posix`.** An event loop is the operating system's — epoll, kqueue, a thread pool,
a socket, a signal disposition — so there is no freestanding target it works on, and the manifest
says so rather than letting it fail at the link with a message naming `uv_run`.

## Tests

```
sysl test .
```

**A hundred and one of them, over five files, and every public entry point but two is named in one.**
The two are `Loop.fork`, which needs a real `fork(2)` a sysl program has no way to make, and
`Tty.winsize`, which needs a terminal with a slave attached — on a pty *master* macOS refuses it and
Linux allows it, so a test either way would pin a platform rather than this binding. Both are said so
at the site.

The thing under test is the binding rather than libuv. What is untested anywhere else is the
arrangement this package is built on, and every case is chosen to fail if one of these is not true: a
handle's storage stays where it was put, a handle stays alive with nothing naming it and is freed
when it is closed, the address libuv hands a callback finds its way back to the right sysl handle,
and a struct this side declares has the layout the header does.

The end-to-end ones are real: a TCP echo over the loopback on a port the kernel picked, a Unix domain
socket in a temporary directory, `localhost` resolved on the thread pool, a file written and read
back, `echo hello` spawned with its output read through a pipe, and `/bin/pwd` run in a directory it
was given with an environment it was given.

**Six things in this README were wrong until a test said so**, which is the argument for writing them:
`again` refuses only a timer that was never started rather than one with no interval; `due_in` still
reports a deadline after `stop`; `idle_time` answers zero until the loop is asked to measure it;
`socket_pair` gives *Unix domain* sockets, so `Tcp.open` on one connects and then refuses every TCP
option; `Pipe.chmod` takes `READABLE`/`WRITABLE` and not the stdio flags of nearly the same name; and
`hrtime` does not share a base with `clock(CLOCK_MONOTONIC)` — seven seconds apart on this machine.

## A note on `?`

Two kinds of function in this package are written without `?` on purpose — the ones answering with an
`Address` or a `Stat`. Both are larger than 128 bytes, and a `?` in a function whose result is
returned through an `sret` out-parameter currently emits a direct return instead; clang refuses the
IR, naming a temporary file and the word `void`. It is filed as card `0304` against the compiler, and
the comment at each site says so. Nothing about the interface is affected.

## License

ISC. libuv itself is MIT and is not vendored here — this package binds whatever the machine has
installed.
