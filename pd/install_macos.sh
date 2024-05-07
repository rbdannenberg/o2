#!/bin/sh
# Install O2 externals to ~/Documents/Pd/externals
echo "Installing from ./Release/ to ~/Documents/Pd/externals/ ..."
cp Release/o2send.pd_darwin ~/Documents/Pd/externals/
cp Release/o2ensemble.pd_darwin ~/Documents/Pd/externals/
cp Release/o2receive.pd_darwin ~/Documents/Pd/externals/
cp Release/o2property.pd_darwin ~/Documents/Pd/externals/
cp Release/libo2pd.dylib ~/Documents/Pd/externals/
install_name_tool -change libo2pd.dylib @loader_path/libo2pd.dylib \
        ~/Documents/Pd/externals/o2send.pd_darwin
install_name_tool -change libo2pd.dylib @loader_path/libo2pd.dylib \
        ~/Documents/Pd/externals/o2ensemble.pd_darwin
install_name_tool -change libo2pd.dylib @loader_path/libo2pd.dylib \
        ~/Documents/Pd/externals/o2receive.pd_darwin
install_name_tool -change libo2pd.dylib @loader_path/libo2pd.dylib \
        ~/Documents/Pd/externals/o2property.pd_darwin
echo "... installation done."
