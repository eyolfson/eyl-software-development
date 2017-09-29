# eyl Software Development

Currently focused on the Teensy 3.2 board.

## TODO

- [ ] Describe boot process
  - [ ] Frequency requirements
- [ ] Blink LED on Teensy 3.2 (and boot)
  - [ ] Blink RGB LED
- [ ] Software Engineering tools
  - [ ] Computer Science tools(?)
  - [ ] Programming tools
- [ ] Better name(?)
- [ ] Better documentation and implementation

## x86-64 Compiler

A different way to program.

### Hello World

- [ ] Binary input
- [x] Binary output

### Portable Executables

There's no such thing, even if you use libc. Even if the calling convention is
the same on two platforms, the methods for loading a shared object is different
on both platfroms. If the calling convention is the same, the snippet of code is
portable.

## License

All content in the `src` directory is distributed under the GPL-3.0.
