## ECTool.efi, FrameworkKeyMapDriver and others

This repository contains some "fun"(?) tools to interact with the embedded controller in the Framework Laptop:

* **FrameworkKeyMapDriver**: An EFI driver (loadable via systemd-boot) that remaps <kbd>Caps Lock</kbd> to
  <kbd>Escape</kbd>
* **ECTool.efi**: An application, intended to be used from the EFI Shell, that primarily lets you reflash the EC
* **ChassisIntrusionPCRMeasurementDriver**: An EFI driver with [its own readme](Drivers/ChassisIntrusionPCRMeasurementDriver/README.md).

### Building

#### Prerequisites

* [edk2](https://github.com/tianocore/edk2/), set up and ready to build

#### Okay, what next?

Clone this repository into the root of your edk2 workspace, and then build it:

```
build -p FrameworkHacksPkg/FrameworkHacksPkg.dsc -a X64 -b RELEASE -t GCC5
```

> **Note**
> You can set your preferred architecture, build type and toolchain by default
> by editing the values stored in `Conf/target.txt`

> **Warning**
> _FrameworkHacksPkg has only been tested with the `GCC5` toolchain. If you elect
> to use a different toolchain, your mileage may vary._

Look in `$WORKSPACE/Build/FrameworkHacksPkg/RELEASE_GCC5/X64` for your output.
