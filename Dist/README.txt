┏━┛┏━┛━┏┛┏━┃┏━┃┃  
┏━┛┃   ┃ ┃ ┃┃ ┃┃  
━━┛━━┛ ┛ ━━┛━━┛━━┛

INSTALLATION
============

1. Extract the contents of this archive to a FAT32-formatted storage device.
2. Copy the firmware image (512kb) that you'd like to flash to the same storage
   device.

USAGE
=====

Your newly-created flash drive (from step 1) will boot to the UEFI Shell.

From there, you can run ectool.

Make sure that ectool can talk to your EC:

Shell> fs0:\ectool version

If you don't get a satisfactory reply, you should probably not proceed.

Back up your existing flash (all 1MB):

Shell> fs0:\ectool flashread 0 1048576 fs0:\ec-backup.bin

If you're willing to risk everything:

Shell> fs0:\ectool reflash fs0:\your-firmware-image.bin
