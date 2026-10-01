# repsx
``repsx`` is an embeddable and portable Sony PlayStation emulator core library

## Missing features
The emulator core still doesn't support savestates at the moment, apart from that, everything has been implemented, including memory card support
**note that this repository only includes the emulator core, a proper frontend from me still isn't available yet**

### Writing frontends
You can write a frontend for this core by checking out the API exposed in the ``include`` directory, in short, it boils down to:
- initializing the system (supplying a BIOS, allocating the required memory)
- supplying callbacks for the ``psx_gpu`` struct, by filling in the ``psx_renderer`` struct
- adding peripheral devices (controllers, memory cards) to the system's ports (through the ``psx_system_add_[device]`` functions)
- (in your running loop) stepping the CPU by calling ``psx_system_update``
- (in your running loop, or audio callback) reading the audio ring buffer through ``psx_spu_read_samples``

## Building (tested on Linux)
run:
```
make
```

## Compatibility
~70 games were tested (see ``compat.txt``), most of them work properly, some others may have issues that might not necessarily allow for a playable experience

## Acknowledgements
[PCSX-Redux](https://github.com/grumpycoders/pcsx-redux), [Duckstation](https://github.com/stenzek/duckstation) and [psxe](https://github.com/allkern/psxe) for various info and hints <br>

[Jakub's Awesome test suite](https://github.com/JaCzekanski/ps1-tests) and [Amidog](https://psx.amidog.se/) for the very helpful hardware tests <br>

The folks over at the [emudev discord server](https://discord.gg/exVbx3tRJ)