# Dirt

A different way to program.

## Mutability

In this model mutability does not exist. It is a special case of computing a new
value where the old value no longer exists.

For example in C:
    int x = 1;
    x = 2;

In this model:
    x = 1
    x' = 2
    x is no longer available (requires explicit mark if you really mean it)

For debugging, this allows you to ask "what is in location". For instance if x
and x' are in rax, the debugger will be accurate.

## Portable Executables

There's no such thing, even if you use libc. Even if the calling convention is
the same on two platforms, the methods for loading a shared object is different
on both platfroms. If the calling convention is the same, the snippet of code is
portable.

## License

All content in the `src` directory is distributed under the GPL-3.0.
