if ($?LIBRARY_PATH) then
	setenv LIBRARY_PATH ${LIBRARY_PATH}:/opt/orangefs/lib
else
	setenv LIBRARY_PATH /opt/orangefs/lib
endif

if ($?C_INCLUDE_PATH) then
	setenv C_INCLUDE_PATH ${C_INCLUDE_PATH}:/opt/orangefs/include
else
	setenv C_INCLUDE_PATH /opt/orangefs/include
endif

if ($?LD_LIBRARY_PATH) then
	setenv LD_LIBRARY_PATH ${LD_LIBRARY_PATH}:/opt/orangefs/lib
else
	setenv LD_LIBRARY_PATH /opt/orangefs/lib
endif
