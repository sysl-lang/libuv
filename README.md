# libuv

libuv for sysl — the event loop Node.js is built on, with TCP, pipes, terminals, timers, signals,
child processes, name resolution and a file system that does not block.

```
dependencies {
  libuv { git = "github.com/sysl-lang/libuv", version = "0.1.8" }
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

### The shim is three shapes, and a few signatures

`sh/sysl/libuv/c/shim.c` is fifty lines. Most of them are one of the three things C can reach and
sysl cannot:

| | why |
|---|---|
| `struct sockaddr`, `struct addrinfo` | the field order is the **platform's**, not libuv's |
| `uv_stdio_container_t` | libuv declares it with a **union** in it |
| `uv_buf_t` | declared `{base, len}` on Unix and the other way round on Windows |

The rest are the calls whose **signature** has no sysl spelling: `uv_loop_configure`, which is
variadic; `uv_thread_self`, which answers with a platform type by value; and `uv_thread_create_ex`,
whose options struct is positional — its stack size is read only when the flag asking for it is set.

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
| **wake-ups** | `notifier` — the one handle another thread may touch; `waker`, which is the `send` on its own as a value |
| **signals** | `signal`, `start`, `start_once`, `kill`, `ignore_sigpipe` |
| **streams** | `read_start`, `write`, `try_write`, `shutdown`, `listen`, back-pressure |
| **TCP** | `tcp`, `bind` (with `TCP_IPV6ONLY` and `TCP_REUSEPORT`), `listen`, `accept`, `connect`, `nodelay`, `keepalive`, `sockname` |
| **pipes** | `pipe`, `pipe_pair`, `socket_pair`, Unix domain sockets, and `write_handle` / `pending_count` / `pending_type` / `accept_pending` for passing a handle |
| **terminals** | `tty`, `set_mode`, `winsize`, `reset_tty_mode`, `guess_handle` |
| **names** | `resolve` — `getaddrinfo` on the thread pool |
| **the pool** | `queue` — a job on a worker thread, answered on the loop; `cancel`, `is_pending` |
| **threads** | `thread`, `thread_with_stack`, `join`, `id`, `current_thread`, `set_thread_name` |
| **locks** | `mutex`, `recursive_mutex`, `cond`, `semaphore`, `rwlock`, `barrier` |
| **files, blocking** | `open_sync`, `read_file_sync`, `write_file_sync`, `stat_sync`, `scandir_sync`, `symlink_sync`, … |
| **files, not** | `open`, `read`, `write`, `stat`, `scandir`, `read_file`, … |
| **children** | `spawn`, `Stdio`, `kill`, `on_exit` |
| **the machine** | `hrtime`, `hostname`, `cwd`, `env`, `available_parallelism`, memory, load |

**What is not bound yet**, and is ordinary work rather than anything blocked: UDP, `uv_poll_t`,
`uv_fs_event_t` and `uv_fs_poll_t`, `getnameinfo`, `uv_random`, and `dlopen`. Each is a section of
`uv.h` and each would be added the way the ones above were.

**Three of libuv's threading calls are left out on purpose.** `uv_thread_detach` would free the box a
running body lives in, which is the one thing a binding must not offer; `uv_once` needs a guard in
static storage, which is what shuts a package out of a bare-metal target; and `uv_key_*` is
thread-local storage of a `void *`, which is a language feature rather than something to reach
through a library. Thread priority, affinity and `uv_thread_getcpu` are scheduling rather than
threading, and are the same ordinary work as the list above.

## One port, several listeners

`bind` takes `TCP_REUSEPORT`, which asks the kernel to let more than one socket hold the same port
and to distribute incoming connections across them:

```sysl
server.bind(ip4("0.0.0.0", 8080).expect("an address"), TCP_REUSEPORT).expect("bound")
```

That is how a server uses every core: a process per core, each with its own loop and its own
listener, and no thread accepting connections on behalf of the others. It is `SO_REUSEPORT` and it
is **not** `SO_REUSEADDR`, which libuv sets for every listener on its own — that one is about
rebinding a port whose last connection is still in `TIME_WAIT`, and it distributes nothing.

**It answers `ENOTSUP` where the kernel does not distribute, and macOS is one of those.** The flag
works on Linux 3.9+, DragonFly 3.6+, FreeBSD 12.0+, Solaris 11.4 and AIX 7.2.5+. macOS is refused on
purpose rather than by omission: its `SO_REUSEPORT` lets the duplicate bind succeed and then delivers
every connection to the *last* socket bound, which is not load balancing — so a program developed on
a Mac and shipped to Linux would have been silently wrong about what it had. A server that wants the
port either way binds again without the flag when the first attempt is refused.

It needs libuv 1.49 or newer, which is where the flag was added.

## One listener, several processes — passing a handle

**Where `TCP_REUSEPORT` is refused, the portable answer is to move the socket rather than the
listener**, and that is what an ipc pipe is for: a supervisor owns the one listener, accepts every
connection itself, and hands each accepted socket to a worker over the pipe it spawned that worker
with. It is Node's cluster module's default scheduler, and it is the same mechanism underneath —
`SCM_RIGHTS` over a Unix domain socket, with libuv doing the `sendmsg`.

The channel is a `stdio` slot, and `stdio` is not limited to three:

```sysl
val to_worker = pipe(true)?

spawn(exe, ["--worker"], on_exit,
      [Inherit, Inherit, Inherit, ToPipe(to_worker, READABLE_PIPE | WRITABLE_PIPE)])?
```

Slot `i` is the child's descriptor `i`, so the worker's end is fd 3 and it adopts it with
`pipe(true)?.open(3)`. Both ends must be made with `pipe(true)`.

Sending is one call, and **the payload is never empty** — a handle travels attached to bytes, and a
single byte is the usual one. What is in it is the supervisor's business; libuv does not look:

```sysl
to_worker.write_handle([1], conn, (r) ->
    r.expect("sent")
    conn.close())?
```

**The handle is duplicated, not moved.** The descriptor is copied when libuv reaches the `sendmsg`,
which is why `conn` has to stay open until the write callback and why this side still closes its own
copy afterwards.

Receiving happens **inside the read callback**, which is the only place a pending handle exists:

```sysl
from_supervisor.read_start((r) -> r match
    Data(_) ->
        if from_supervisor.pending_count() > 0 && from_supervisor.pending_type() == HANDLE_TCP
            val conn = tcp()?

            from_supervisor.accept_pending(conn)?
            serve(conn)
    End -> ...
    Failed(e) -> ...)
```

`pending_type` has to be asked before the handle is taken, because `accept_pending` puts it into
whatever handle it is given and only the type says which kind that should be — `HANDLE_TCP` for a
socket, `HANDLE_NAMED_PIPE` for a pipe or a Unix domain socket. `accept_pending` with nothing waiting
is `EAGAIN`; `write_handle` on a pipe that was not made with `ipc` is `EINVAL`, which is libuv's own
refusal rather than a check this package added.

## Work that is too slow for the loop

A loop is one thread and everything on it takes turns, so a callback that hashes a password or
compresses an asset is a callback nothing else runs during. `queue` puts the slow part on one of
libuv's worker threads and answers back on the loop:

```sysl
var out: &sync Digest = Digest([0; 32])

queue(() -> hash_into(out), (r) -> r match             // the job, then the answer
    Ok(_) -> reply(out)
    Err(e) -> print(s"the job did not run: $e"))?
```

**Two callbacks on two threads, and only one of them is ordinary.** The completion runs on the loop
and captures what any other callback here captures. The job crosses a concurrency domain, so it is a
`&sync Fn` — and **the compiler is what holds a caller to that**, at the closure rather than at run
time:

```
error: a closure shared between two domains may be called from either, so every count it captures
has to be atomic — but the 'c' it captures reaches a '&Cell', whose count is not. Hold it as a
'&sync Cell'
```

So a job may reach scalars and arrays, which are copied into it; a `&sync T`, which is how two
threads share one object; and a `*T`, which carries no count and asks the writer to have thought
about it. A `string`, a slice and an ordinary `&T` are refused, and the refusal names the capture.
**A `&sync T` makes the reference safe to share, not the object safe to mutate** — that still wants
`sysl.sync.Atomic` or a mutex.

**A job answers by writing rather than by returning.** Its closure is `() -> unit`, because a value
coming back out of a pool thread would be a value crossing a boundary; what it has to say it says
through the `&sync` it captured, and the completion callback is where that is read.

**`cancel` catches a job a thread has not reached yet**, and `EBUSY` says one already running cannot
be. Either way the completion callback runs — with `ECANCELED` for a cancelled job — so a program
has one place to release what the job was holding.

**The pool is four threads unless `UV_THREADPOOL_SIZE` says otherwise, and libuv reads that once**,
at first use — which `resolve` and every asynchronous file call also count as. There is no call here
that sets it: a library function cannot promise what the program did before it, so that is a variable
a program's environment carries rather than a function this package could honestly offer. The pool is
also **shared**, so a job that blocks for a second is a second in which one of those four threads
resolves no names and reads no files.

## Threads

A pool job borrows one of libuv's four worker threads and gives it back, so it is for something slow
that has an end. A **thread** is for something that outlives the call: an actor with a loop of its
own, a reader blocked on a device libuv has no backend for, a second event loop serving a second
port. `thread` starts one and `join` waits for it:

```sysl
val running: &sync Atomic[int] = Atomic(1)

val t = thread(() -> serve(running))?      // the body is a &sync Fn, like a pool job's

running.store(0)
t.join()?
```

**What may cross into a thread is what may cross into a pool job**, for the same reason and with the
same refusal: scalars and arrays are copied in, a `&sync T` is how two threads share one object, a
`*T` carries no count — and a `string`, a slice or an ordinary `&T` is refused at the closure, named.

**A thread holds a reference to itself while it runs**, so a body is never freed underneath a running
thread, and `join` is what drops it. There is no `detach`, and that is deliberate: the body lives in
that box, so a detached thread would be running a closure with nothing keeping it alive. A thread
that should not be waited for is given something to wait on instead. Joining twice answers `EINVAL`
rather than the undefined behaviour POSIX would give it.

### The mailbox

A thread with a loop of its own is reached the way libuv reaches any loop from outside: leave the
message where both threads can see it, and wake the loop with an `Async`. **A handle is a `&T` and a
body may not capture one**, so what crosses is a `Waker` — an address, and `uv_async_send` is the one
libuv call safe from any thread at all:

```sysl
val t = thread(() ->
    val lp = new_loop().expect("a loop of its own")
    val a = notifier(() -> drain(box), lp).expect("a notifier on it")

    box.wake = Some(a.waker())          // the one thing here another thread may hold
    box.ready.post()

    lp.run().expect("the thread's loop ran"))?

box.ready.wait()

val wake = box.wake.expect("the thread published it")

box.lock.lock()
box.slots[box.held] = 1
box.held += 1
box.lock.unlock()

wake.send()?                            // the loop runs `drain` on its own thread
```

**The mutex guards the queue and the wake-up carries no data**, which is the shape libuv is built
for: sends coalesce, so the callback's job is to drain whatever is there rather than to be counted.
The handle has to outlive every waker taken from it — the arrangement above has the thread owning
both, which makes that true by construction.

**The queue is an array rather than a `Buf`**, and not by preference: a growable collection owns its
elements through a count that is not atomic, so a `&sync` struct may not hold one and the compiler
says so. A fixed array is what a shared queue is made of today.

### The locks

None of them is a handle: they belong to no loop, have no close callback, and are released by
`destroy`. Each is a `&sync T`, because a lock only one thread can reach is not a lock — and because
`&sync` is what a body may capture.

| | |
|---|---|
| `mutex`, `recursive_mutex` | `lock`, `try_lock`, `unlock`, and `with(f)`, which holds it for a closure and answers what the closure did |
| `cond` | `wait(m)`, `timed_wait(m, ns)`, `signal`, `broadcast` |
| `semaphore(n)` | `wait`, `try_wait`, `post` — and unlike a condition it *remembers* a post nobody was waiting for |
| `rwlock` | `read_lock`, `write_lock`, their `try_` forms, and `reading(f)` / `writing(f)` |
| `barrier(n)` | `wait`, which answers `true` for the one thread that may `destroy` it |

**`unlock` on a mutex this thread does not hold is undefined**, in libuv and in pthreads under it,
and nothing here checks it: the word it would cost is paid by every correct program. `with` is the
form that cannot get it wrong, and the bare pair is for the case that cannot use it — a lock held
across a `wait`, or across a callback that drains a queue.

**A condition carries no state**, so a `signal` nobody is waiting for is lost, and a wait may return
without one. That is why the test is a loop and never an `if`:

```sysl
m.lock()

while queue_is_empty()
    ready.wait(m)

m.unlock()
```

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

**libuv 1.49 or newer**, which is where `UV_TCP_REUSEPORT` was added. An older one fails to compile
`constants.sysl`, naming that identifier — which reads as a defect in this package and is a libuv
that predates the flag.

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

**A hundred and forty-eight of them, over eight files, and every public entry point but one is exercised
by one.** The one is `Tty.winsize`, which needs a terminal with a slave attached — on a pty *master*
macOS refuses it and Linux allows it, so a test either way would pin a platform rather than this
binding, and a test runner has no controlling terminal to use instead. It says so at the site.

**The whole raw layer is exercised too**, which is a separate file: a declaration nothing calls is a
declaration nothing checks, and a signature that disagrees with `uv.h` links perfectly and corrupts
the call at run time. Every one of the 223 `extern`s is now reached, from the pleasant layer or from
`raw_tests.sysl` directly. `uv_cancel` was the one exception until the thread pool arrived — it may
only be called on a request still in flight, and on a finished one it faults, so there was nothing
safe to call it from. `Work.cancel` is that caller now, because a request knows whether it is still
the loop's.

**The most important test is the one that watches memory.** A handle holds a reference to itself so
that the loop owns it, and `close` dropping that is what the whole design rests on — as does a work
request, which has no close and releases itself when it reports instead — but a refcount
is not something a program can ask about, so a `finish_close` missing one line would leak every
handle a program ever opened with every other test still green. `closing_frees` churns ten thousand
of each handle type per round and asserts resident memory stops growing once the allocator has
settled. Removing one line from any single `finish_close` turns it red; that was checked against
three of them.

### Under AddressSanitizer

```
SYSL_EXTRA_CFLAGS="-fsanitize=address -g" sysl test .
```

**What that reaches is the sysl half.** libuv is a library the machine already has, so nothing of
libuv's is instrumented — what is, is this binding's own pointer arithmetic, which is where a binding's
risk lives anyway. It found a real one: `resolve_done` copied a whole `sockaddr_storage` out of a
resolver's answer, which points at a `sockaddr_in` of sixteen bytes in an allocation of exactly that
size, so every lookup read 112 bytes past the end of it. The suite was green before and after, because
the extra bytes went into a value nothing reads — `ai_addrlen` is what the copy is bounded by now.

**`closing_frees` fails under the sanitizer and only under it.** It asserts that resident memory stops
growing, and ASan gives every block a redzone and holds freed ones in a quarantine, so RSS grows
whatever the reference counting does. Read that test's answer from a run without the flag.

**`Loop.fork` is tested by actually forking**, which took two attempts worth recording: the first
version passed with the call under test taken out, because a loop carrying only a timer does not
exercise it. What a fork invalidates is the backend's registration of *descriptors*, so the test now
carries a live read across the fork — 2ms with the call, and a failure at its 500ms guard without
it.

The thing under test is the binding rather than libuv. What is untested anywhere else is the
arrangement this package is built on, and every case is chosen to fail if one of these is not true: a
handle's storage stays where it was put, a handle stays alive with nothing naming it and is freed
when it is closed, the address libuv hands a callback finds its way back to the right sysl handle,
and a struct this side declares has the layout the header does.

The end-to-end ones are real: a TCP echo over the loopback on a port the kernel picked, a Unix domain
socket in a temporary directory, `localhost` resolved on the thread pool, a file written and read
back, `echo hello` spawned with its output read through a pipe, and `/bin/pwd` run in a directory it
was given with an environment it was given.

**Seven things here were wrong until a test said so**, which is the argument for writing them:
`again` refuses only a timer that was never started rather than one with no interval; `due_in` still
reports a deadline after `stop`; `idle_time` answers zero until the loop is asked to measure it;
`socket_pair` gives *Unix domain* sockets, so `Tcp.open` on one connects and then refuses every TCP
option; `Pipe.chmod` takes `READABLE`/`WRITABLE` and not the stdio flags of nearly the same name;
`hrtime` does not share a base with `clock(CLOCK_MONOTONIC)` — seven seconds apart on this machine;
and a write to a peer that has gone ends the process unless `ignore_sigpipe` was called.

An eighth was a compiler bug rather than a mistake here: rendering an `AddrInfo` used to segfault,
because a struct over 128 bytes could not be rendered through `str` or `s"$x"` at all while `print` of
one worked. That was card `0305`, fixed in sysl 0.0.83; `AddrInfo` has a `Display` now, and a test
covers it directly.

## Two compiler bugs this package used to work around

Both were the same 128-byte boundary, which is where a value stops being passed and returned
directly, and both were fixed in sysl 0.0.83 — this package's floor, so neither workaround is in the
source any more.

**Card `0304`** — a `?` in a function whose result was larger than 128 bytes emitted a direct return
out of a function the ABI made `void`, and clang refused the compiler's own IR. `ip4` and `ip6`, which
answer an `Address`, now use `?` like everything else.

**Card `0305`** — a struct over 128 bytes could not implement `Display` usefully: `str(x)` and
`s"$x"` segfaulted with no diagnostic, while `print(x)` worked. `AddrInfo` now has a `Display`,
rendering its address and, when the resolver was asked for one, its canonical name.

## License

ISC. libuv itself is MIT and is not vendored here — this package binds whatever the machine has
installed.
