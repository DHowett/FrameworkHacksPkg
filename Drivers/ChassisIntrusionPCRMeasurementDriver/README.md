This is a DXE driver that measures the chassis intrusion state of the Framework Laptop into PCR[6]¹.

Just over a year ago, while reverse-engineering the embedded controller firmware (before it was open-source), I stumbled
upon a [host command that reports the entire chassis intrusion state] and speculated that it could be used as part of a
PCR measurement. Framework let me know that it _wasn't_ being used like that.

The value reported by this embedded controller feature:

- changes every time the chassis is opened
   - ... even if the machine was powered fully off when it was opened
- resets when the RTC battery is removed

Because it increments unless reset and otherwise simply tracks the raw count of times the chassis was opened, it's
somewhat tamper-evident.

<details>
<summary>Notes from use</summary>

As part of developing this driver, I've captured PCR measurements from four boots:

1. Before installing the driver (baseline)
2. After installing the driver
3. After opening the chassis once
4. After removing the driver

The differences are below:

### 1 to 2

```diff
--- pcrs.0.before       2023-01-20 22:16:58.000000000 -0600
+++ pcrs.1.with_driver  2023-01-20 22:16:58.000000000 -0600
@@ -1,15 +1,15 @@
   sha1:
     0 : 0x759BAEDB49070E3FB333B71CD3E599E3E0727349
     1 : 0x0ED7903FD6A72EBE1263E23C3B067024FE9C8110
-    2 : 0x58DB64F49A155C69FD060A1E2EDDCE4B983F4A9C
+    2 : 0x0C39F6A598C90597DD900C03A74976DD82FA424F
     3 : 0xB2A83B0EBF2F8374299A5B2BDFC31EA955AD7236
     4 : 0xB6B4F407929851EE335F340C2C02DA1406FD2C97
     5 : 0x873F1331A9BE77F875F330ABD491B697E7C64937
-    6 : 0xB2A83B0EBF2F8374299A5B2BDFC31EA955AD7236
+    6 : 0x240A31A2F2D558EA9B61C2F60B10650CE5D3713A
     7 : 0xDE581970A4C0AFAD9EF6BC21A4E4858AE5158436
     8 : 0x0000000000000000000000000000000000000000
     9 : 0x1227F6297C90F87731031BE47192D461006B22F8
-    10: 0x6CD74CCC5A98099603801F3B80CD88FF10B02B79
+    10: 0x7D4506E4BC356F72125E73F7CC0F5861B33F11B7
     11: 0x0000000000000000000000000000000000000000
     12: 0x63E7FCEDCC66A85AD1A7C1B4AF468AF458A773BD
     13: 0x0000000000000000000000000000000000000000
```

We observe a difference in PCR 2 due to the loading of a new driver.

I cannot explain the difference in PCR 10, though I presume it is likely from `systemd-boot` (my bootloader).

### 2 to 3

```diff
--- pcrs.1.with_driver  2023-01-20 22:16:58.000000000 -0600
+++ pcrs.2.after_first_intrusion        2023-01-20 22:16:58.000000000 -0600
@@ -5,11 +5,11 @@
     3 : 0xB2A83B0EBF2F8374299A5B2BDFC31EA955AD7236
     4 : 0xB6B4F407929851EE335F340C2C02DA1406FD2C97
     5 : 0x873F1331A9BE77F875F330ABD491B697E7C64937
-    6 : 0x240A31A2F2D558EA9B61C2F60B10650CE5D3713A
+    6 : 0x763050510BA63DDC284AA7CE324A64BDEA460865
     7 : 0xDE581970A4C0AFAD9EF6BC21A4E4858AE5158436
     8 : 0x0000000000000000000000000000000000000000
     9 : 0x1227F6297C90F87731031BE47192D461006B22F8
-    10: 0x7D4506E4BC356F72125E73F7CC0F5861B33F11B7
+    10: 0xFA6F552F00B546B7D8A3B7CC5859A811D412AB7F
     11: 0x0000000000000000000000000000000000000000
     12: 0x63E7FCEDCC66A85AD1A7C1B4AF468AF458A773BD
     13: 0x0000000000000000000000000000000000000000
```

PCR 6 shows a change after chassis intrusion.

### 1 vs 4 (does removing the driver revert the PCRs?)

```shell-session
dustin@rigel:/mnt/c/Users/Dustin$ diff -u pcrs.0.before  pcrs.3.after_removal
dustin@rigel:/mnt/c/Users/Dustin$
```

Yes.

</details>

¹ I chose PCR[6] because it is the closest PCR in the specification to being OEM-controlled, and if this were to become an official extension then that might be the right place for it. I do not know what else is measured into PCR[6].

[host command that reports the entire chassis intrusion state]: https://www.howett.net/posts/2021-12-framework-ec/#3e09---historical-chassis-intrusion-data
