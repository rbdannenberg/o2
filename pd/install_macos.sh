#!/bin/sh
# Install O2 externals to ~/Documents/Pd/externals

# Check if an argument was provided
if [ $# -ne 1 ]; then
    echo "Error: Please provide exactly one argument - either 'r' for Release or 'd' for Debug"
    echo "Usage: $0 [r|d]"
    exit 1
fi

# Determine the source directory based on the argument
case $1 in
    r)
        SOURCE_DIR="Release"
        ;;
    d)
        SOURCE_DIR="Debug"
        ;;
    *)
        echo "Error: Invalid argument. Please use 'r' for Release or 'd' for Debug"
        echo "Usage: $0 [r|d]"
        exit 1
        ;;
esac

echo "Installing from ./$SOURCE_DIR/ to ~/Documents/Pd/externals/ ..."
cp $SOURCE_DIR/o2send.pd_darwin ~/Documents/Pd/externals/
cp $SOURCE_DIR/o2ensemble.pd_darwin ~/Documents/Pd/externals/
cp $SOURCE_DIR/o2receive.pd_darwin ~/Documents/Pd/externals/
cp $SOURCE_DIR/o2property.pd_darwin ~/Documents/Pd/externals/
cp $SOURCE_DIR/libo2pd.dylib ~/Documents/Pd/externals/
install_name_tool -change libo2pd.dylib @rpath/libo2pd.dylib \
        ~/Documents/Pd/externals/o2send.pd_darwin
install_name_tool -change libo2pd.dylib @rpath/libo2pd.dylib \
        ~/Documents/Pd/externals/o2ensemble.pd_darwin
install_name_tool -change libo2pd.dylib @rpath/libo2pd.dylib \
        ~/Documents/Pd/externals/o2receive.pd_darwin
install_name_tool -change libo2pd.dylib @rpath/libo2pd.dylib \
        ~/Documents/Pd/externals/o2property.pd_darwin
echo "... installation done."
