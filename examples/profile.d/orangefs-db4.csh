if ($?LIBRARY_PATH) then
	setenv LIBRARY_PATH ${LIBRARY_PATH}:/opt/orangefs/lib:/opt/db4/lib
else
	setenv LIBRARY_PATH /opt/orangefs/lib:/opt/db4/lib
endif

if ($?C_INCLUDE_PATH) then
	setenv C_INCLUDE_PATH ${C_INCLUDE_PATH}:/opt/orangefs/include:/opt/db4/include
else
	setenv C_INCLUDE_PATH /opt/orangefs/include:/opt/db4/include
endif

if ($?LD_LIBRARY_PATH) then
	setenv LD_LIBRARY_PATH ${LD_LIBRARY_PATH}:/opt/orangefs/lib:/opt/db4/lib
else
	setenv LD_LIBRARY_PATH /opt/orangefs/lib:/opt/db4/lib
endif
