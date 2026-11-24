Project Dam @VERSION@ -- GoldenEye 007, Windows build
=====================================================

The PC build of Project Dam (https://github.com/christphe/project-dam-nx),
a fork of the GoldenEye 007 PC port by jkdansereau and contributors
(https://github.com/jkdansereau/goldeneye-pc-port). Unofficial; not affiliated
with Nintendo, Rare, Microsoft or MGM. No game content is included: you need
your own GoldenEye 007 ROM.

Requirements
------------
- Windows 10 or 11, x86-64.
- Your own GoldenEye 007 ROM, US (NTSC-U) version, big-endian .z64
  (SHA-1 abe01e4aeb033b6c0836819f549c791b26cfde83).

Install
-------
1. Unpack this folder anywhere (not inside the zip).
2. Copy your ROM into the data folder next to ge007.x86_64.exe as
       data\ge007.ntsc-final.z64
3. Start ge007.x86_64.exe. The first launch converts the ROM and writes the
   converted model and level files into data\; later launches skip this.

Settings, saves and logs live in data\ (ge007.ini, ge007.eep, ge007.log).
F10 or a pad's Select button opens the options menu.

Source and changelog: the project-dam-nx repository on GitHub.
License: MIT for the port code (licenses\LICENSE); third-party components in
licenses\NOTICE, with the texts of the bundled runtime libraries.
