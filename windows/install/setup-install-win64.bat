@echo off
rem Builds directory structure for OrangeFS Windows Client installer (64-bit)

md install-root-win64
cd install-root-win64

rem Create directories
md Dokan
md Dokan\DokanLibrary
md OrangeFS
md OrangeFS\Client
md OrangeFS\Client\CA
md OrangeFS\Client\Doc
md OrangeFS\Client\Licenses
md OrangeFS\Client\Tools

rem Install instructions
copy ..\install.txt OrangeFS\Client /y

rem Dokan files
copy ..\..\dokan\bin64\dokan.dll Dokan\DokanLibrary /y
copy ..\..\dokan\bin64\dokan.sys Dokan\DokanLibrary /y
copy ..\..\dokan\bin64\dokanctl.exe Dokan\DokanLibrary /y
copy ..\..\dokan\bin64\mounter.exe Dokan\DokanLibrary /y
copy ..\..\dokan\*.txt Dokan\DokanLibrary /y

rem LDAP files
copy ..\..\ldap\Win64\bin\ldap*.dll OrangeFS\Client /y

rem OpenSSL files
copy ..\..\openssl\bin64\release\*.dll OrangeFS\Client /y

rem Client executable
copy ..\..\projects\OrangeFS\x64\Release\orangefs-client.exe OrangeFS\Client /y

rem orangefs-get-user-cert executable
copy ..\..\projects\OrangeFS\x64\Release\orangefs-get-user-cert.exe OrangeFS\Client /y

rem MS CRT files
copy "C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\redist\x64\Microsoft.VC110.CRT\*.dll" OrangeFS\Client /y

rem Documentation
copy ..\doc\OrangeFSWindowsClient.pdf OrangeFS\Client\Doc /y

rem Licenses
copy ..\..\..\COPYING OrangeFS\Client\Licenses /y
copy ..\..\ldap\COPYRIGHT.* OrangeFS\Client\Licenses /y
copy ..\..\ldap\LICENSE.* OrangeFS\Client\Licenses /y

rem Grid utility
copy ..\..\..\cert-utils\pvfs2-grid-proxy-init.sh OrangeFS\Client\Tools /y

rem Create zip
del ..\orangefs-windows-client-2.9.6-win64.zip
winrar a ..\orangefs-windows-client-2.9.6-win64.zip Dokan OrangeFS

cd ..
