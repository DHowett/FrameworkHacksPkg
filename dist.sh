#!/bin/bash
set -euo pipefail

build -p FrameworkHacksPkg/FrameworkHacksPkg.dsc -a X64 -b RELEASE
build -p ShellPkg/ShellPkg.dsc -a X64 -b RELEASE -D NO_SHELL_PROFILES

DISTBASE="$(mktemp -t -d dist_sh.XXXXXX)"
trap "rm -rf '$DISTBASE'" 0               # EXIT
trap "rm -rf '$DISTBASE'; exit 1" 2       # INT
trap "rm -rf '$DISTBASE'; exit 1" 1 15    # HUP TERM

DISTDIR="$DISTBASE/_dist"
cp -r Dist "$DISTDIR"
mkdir -p "$DISTDIR/EFI/Boot"

cp -v "$WORKSPACE/Build/Shell/RELEASE_GCC5/X64/Shell_7C04A583-9E3E-4f1c-AD65-E05268D0B4D1.efi" "$DISTDIR/EFI/Boot/bootx64.efi"
cp -v "$WORKSPACE/Build/FrameworkHacksPkg/RELEASE_GCC5/X64/ECTool.efi" "$DISTDIR/"

OUT="$PWD/ECTool-x64-$(git describe --always --abbrev).zip"

{ cd "$DISTDIR" && zip "$OUT" -r .; }
