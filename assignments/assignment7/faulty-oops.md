# Faulty Driver Kernel Oops Analysis

## Trigger

The kernel oops was intentionally triggered by writing to the faulty driver:

```sh
echo "hello_world" > /dev/faulty
```

## Observed Behavior

The kernel generated the following error:

```
Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
```

The program counter indicates that the fault occurred inside the `faulty_write()` function:

```
pc : faulty_write+0x10/0x20 [faulty]
```

## Root Cause

The `faulty` driver is intentionally written to dereference a NULL pointer inside its write callback. When the user writes data to `/dev/faulty`, the kernel executes `faulty_write()`, which attempts to access address `0x0`.

Since virtual address `0x0` is not mapped in kernel space, the processor raises a data abort exception, resulting in a kernel oops.

## Call Trace

The call trace shows the execution path:

```
faulty_write()
  -> ksys_write()
     -> __arm64_sys_write()
        -> invoke_syscall()
```

This confirms that the fault originated in the driver's write handler.

## Analysis

The oops reports:

- NULL pointer dereference
- Faulting virtual address: `0x0000000000000000`
- Exception class: Data Abort
- Fault occurred in `faulty_write()`

The kernel correctly identified the offending module (`faulty`) and printed the processor register contents along with the stack trace to aid debugging.

## Why the System Continued Running

A kernel oops is different from a kernel panic.

A kernel oops terminates only the offending kernel task while allowing the remaining kernel and user-space processes to continue running whenever possible.

In this experiment, the shell process terminated because it triggered the fault, and the system returned to the Buildroot login prompt instead of rebooting.

## Conclusion

The experiment successfully demonstrated how an invalid memory access inside a kernel module results in a kernel oops. The kernel detected the NULL pointer dereference, reported the complete stack trace, and recovered without crashing the entire system.
