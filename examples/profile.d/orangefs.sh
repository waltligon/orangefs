if [ -n "$LIBRARY_PATH" ]
then
    export LIBRARY_PATH=/opt/orangefs/lib:$LIBRARY_PATH
else
    export LIBRARY_PATH=/opt/orangefs/lib
fi

if [ -n "$C_INCLUDE_PATH" ]
then
    export C_INCLUDE_PATH=/opt/orangefs/include:$C_INCLUDE_PATH
else
    export C_INCLUDE_PATH=/opt/orangefs/include
fi

if [ -n "$LD_LIBRARY_PATH" ]
then
    export LD_LIBRARY_PATH=/opt/orangefs/lib:$LD_LIBRARY_PATH
else
    export LD_LIBRARY_PATH=/opt/orangefs/lib
fi
