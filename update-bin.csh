#!/bin/csh -f

#
#
#

# cp ./DerivedData/llreplace/Build/Products/Debug/llreplace /usr/local/opt/
# cp ./DerivedData/llreplace/Build/Products/Debug/llreplace bin/osx/

ls -al ./DerivedData/llreplace/Build/Products/Release/llreplace
cp ./DerivedData/llreplace/Build/Products/Release/llreplace /usr/local/opt/
cp ./DerivedData/llreplace/Build/Products/Release/llreplace bin/osx/