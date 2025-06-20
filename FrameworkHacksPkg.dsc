[Defines]
  PLATFORM_NAME           = FrameworkHacksPkg
  PLATFORM_GUID           = 30173B94-643C-11EC-B759-23EEF4283853
  PLATFORM_VERSION        = 1.0
  DSC_SPECIFICATION       = 0x00010005
  OUTPUT_DIRECTORY        = Build/FrameworkHacksPkg
  SUPPORTED_ARCHITECTURES = X64
  BUILD_TARGETS           = DEBUG|RELEASE
  SKUID_IDENTIFIER        = DEFAULT

[LibraryClasses]
  UefiDriverEntryPoint|MdePkg/Library/UefiDriverEntryPoint/UefiDriverEntryPoint.inf
  UefiApplicationEntryPoint|MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf
  UefiRuntimeServicesTableLib|MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf
  UefiRuntimeLib|MdePkg/Library/UefiRuntimeLib/UefiRuntimeLib.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  UefiUsbLib|MdePkg/Library/UefiUsbLib/UefiUsbLib.inf
  UefiScsiLib|MdePkg/Library/UefiScsiLib/UefiScsiLib.inf
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  SynchronizationLib|MdePkg/Library/BaseSynchronizationLib/BaseSynchronizationLib.inf
  PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
  DebugLib|MdePkg/Library/UefiDebugLibStdErr/UefiDebugLibStdErr.inf
  DebugPrintErrorLevelLib|MdePkg/Library/BaseDebugPrintErrorLevelLib/BaseDebugPrintErrorLevelLib.inf
  PostCodeLib|MdePkg/Library/BasePostCodeLibPort80/BasePostCodeLibPort80.inf
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  TimerLib|UefiCpuPkg/Library/CpuTimerLib/BaseCpuTimerLib.inf

  IoLib|MdePkg/Library/BaseIoLibIntrinsic/BaseIoLibIntrinsic.inf

  ShellLib|ShellPkg/Library/UefiShellLib/UefiShellLib.inf
  HiiLib|MdeModulePkg/Library/UefiHiiLib/UefiHiiLib.inf
  FileHandleLib|MdePkg/Library/UefiFileHandleLib/UefiFileHandleLib.inf
  SortLib|MdeModulePkg/Library/UefiSortLib/UefiSortLib.inf
  UefiHiiServicesLib|MdeModulePkg/Library/UefiHiiServicesLib/UefiHiiServicesLib.inf

  HashLib|SecurityPkg/Library/HashLibTpm2/HashLibTpm2.inf
  Tpm2CommandLib|SecurityPkg/Library/Tpm2CommandLib/Tpm2CommandLib.inf
  Tpm2DeviceLib|SecurityPkg/Library/Tpm2DeviceLibTcg2/Tpm2DeviceLibTcg2.inf

  FmapLib|FrameworkHacksPkg/Library/FmapLib/FmapLib.inf

  # Just to be safe, everything in this DSC should link at least one EC communication library
  NULL|FrameworkHacksPkg/Library/MicrochipCrosECLib/MicrochipCrosECLib.inf
  NULL|FrameworkHacksPkg/Library/AzaleaCrosECLib/AzaleaCrosECLib.inf

!include MdePkg/MdeLibs.dsc.inc

[Components]
  FrameworkHacksPkg/Drivers/FrameworkKeyMapDriver/FrameworkKeyMapDriver.inf
  FrameworkHacksPkg/Drivers/ChassisIntrusionPCRMeasurementDriver/ChassisIntrusionPCRMeasurementDriver.inf
  FrameworkHacksPkg/Application/ECTool/ECTool.inf
  FrameworkHacksPkg/Drivers/FrameworkPanicCaptureDriver/FrameworkPanicCaptureDriver.inf
  FrameworkHacksPkg/Application/ECTest/ECTest.inf
  FrameworkHacksPkg/Drivers/ECStatDriver/ECStatDriver.inf

  FrameworkHacksPkg/Drivers/FrameworkEcAcpiNodePatcher/FrameworkEcAcpiNodePatcherDxe.inf

  FrameworkHacksPkg/Drivers/FrameworkEcAcpiNodePatcher/FrameworkEcAcpiNodePatcherDxe.inf {
    <Defines>
      FILE_GUID = 1A65E1D1-43FC-11F0-AF3F-14AC60468E65
    <PcdsFeatureFlag>
      gFrameworkPackageTokenSpaceGuid.PcdUseFrameworkECAcpiNode|FALSE
  }

  # Example - How to customize the EC implementation for one application
  #FrameworkHacksPkg/Application/ECTool/ECTool.inf {
  #  <LibraryClasses>
  #    NULL|FrameworkHacksPkg/Library/MicrochipCrosECLib/MicrochipCrosECLib.inf
  #    NULL|FrameworkHacksPkg/Library/CrosECNullLib/CrosECNullLib.inf
  #}

  FrameworkHacksPkg/Drivers/CrosECDriver/CrosECDriver.inf
