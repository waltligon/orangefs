Current dokany release: 1.5.1.1000, 11/26/2021
https://github.com/dokan-dev/dokany
---
Obtaining files for building:

1. From the dokany release, run TODO (Dokan_x64.msi or DokanSetupDbg.exe).
2. Include files will be under C:\Program Files\Dokan\Dokan Library-{release}\include. Copy these files to orangefs\windows\dokany\include.
3. Obtain dokan.zip from the dokany release page.
   a. Copy dokan1.lib, dokan1.exp and dokan1.pdb from dokan.zip\x64\Debug\ to orangefs\windows\dokany\lib64\debug.
   b. Repeat the above for dokan.zip\x64\Release\ and orangefs\windows\dokany\lib64\release.

---
Test system installation:

TODO
