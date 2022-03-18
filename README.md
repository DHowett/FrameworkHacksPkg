## ECTool.efi and FrameworkKeyMapDriver

This repository contains some "fun"(?) tools to interact with the embedded controller in the Framework Laptop:

* **FrameworkKeyMapDriver**: An EFI driver (loadable via systemd-boot) that remaps <kbd>Caps Lock</kbd> to
  <kbd>Escape</kbd>
* **ECTool.efi**: An application, intended to be used from the EFI Shell, that primarily lets you reflash the EC

### Building

#### Prerequisites

* [edk2](https://github.com/tianocore/edk2/), set up and ready to build

#### Okay, what next?

Clone this repository into the root of your edk2 workspace, and then build it:

```
build -p FrameworkHacksPkg/FrameworkHacksPkg.dsc -a X64 -b RELEASE
```

Look in `$WORKSPACE/Build/FrameworkHacksPkg` for your output.
