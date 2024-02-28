APP=Foo
mkdir -vp ${APP}.app/Contents/MacOS ${APP}.app/Contents/Resources # Create the folders.
PATH="$PATH:/usr/libexec" # Make sure PlistBuddy is in the PATH.
printf '#!/usr/bin/osascript\ntell application "Terminal"\n\tactivate\n\tdo script "top"\nend tell\n' > ${APP}.app/Contents/MacOS/${APP}
chmod +x ${APP}.app/Contents/MacOS/${APP} # Sets the executable flag.
/usr/libexec/PlistBuddy ${APP}.app/Contents/Info.plist -c "add CFBundleDisplayName string ${APP}"
/usr/libexec/PlistBuddy ${APP}.app/Contents/version.plist -c "add ProjectName string ${APP}"
find ${APP}.app # Verify the files.
open ${APP}.app # Run the app.
