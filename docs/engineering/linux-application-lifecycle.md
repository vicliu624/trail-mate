# Linux application restart and clock writes

The SDL desktop composition and the Cardputer Wayland/framebuffer entrypoints
now own an application lifecycle scope. A restart request is recorded atomically,
the rendering loop stops, and normal C++ scope cleanup releases UI objects and
application services. Only after the run scope has returned does the owner call
`execv` on the kernel's current executable identity. This replaces the process
image with the same executable and preserves its argument vector and environment;
it does not run a shell or reboot the operating system.

The former direct `exit(0)` path skipped automatic-object destruction and did not
restart the application. Compositions adopting the lifecycle now release phone,
radio, storage and presentation resources before replacement. Unadapted legacy
compositions retain their previous fallback behavior. A failed application run
is not restarted. A failed exec returns an error exit rather than pretending that
restart succeeded.

Window close, Alt+F4 and explicit automatic-exit deadlines take priority over an
overlapping restart request. A later restart request cannot reverse a recorded
normal exit. Repeated restart requests coalesce. There is no installed application
path or board identifier in the lifecycle owner.

After run-owned services shut down, the restart thread isolates its descriptor
table and marks residual non-stdio descriptors close-on-exec. This also covers
process-lifetime platform services and prevents other threads from racing a new
descriptor into the replacement image. The descriptor test includes an open
descriptor deliberately retained outside the run scope.
The descriptor-table operation uses the documented
[CLONE_FILES contract](https://man7.org/linux/man-pages/man2/unshare.2.html).

The process contract follows [execve(2)](https://man7.org/linux/man-pages/man2/execve.2.html).
The test launches a real child, checks cleanup of an intentionally non-CLOEXEC
descriptor before replacement, verifies destructor output and unchanged PID,
and confirms argument/environment preservation, including spaces and shell
metacharacters. It also covers an error result, exec failure and exit priority.
The phone facade regression calls the actual restart backend in a child and
requires a distinct continuation exit status, so the old `exit(0)` behavior
cannot produce a false passing result.

## Clock writes

The Linux `set_utc_time` port now calls `clock_settime(CLOCK_REALTIME)` and returns
the actual result. Negative timestamps are rejected before the syscall. It does
not substitute a local configuration value for an operating-system clock update.
The existing phone facade bounds its epoch conversion and propagates failure;
the shared phone protocols already map failed synchronization to failure replies.

System-clock authorization is an operating-system deployment prerequisite. This
change does not grant capabilities to the executable or install an elevated
helper. The normal unprivileged application's clock-write capability is therefore
not established by these tests, and full phone time administration remains subject
to deployment policy. NTP and timezone policy are unchanged.
See the Linux [clock interface](https://man7.org/linux/man-pages/man3/clock_gettime.3.html)
for the operating-system privilege requirement.

Clock tests interpose the syscall inside the test process. They verify the target
clock, timestamp, success, permission rejection and invalid-value rejection without
changing any host or device clock. The shared Meshtastic protocol regression also
verifies that the actual Linux facade returns rejected writes as failure and
successful writes as success.
