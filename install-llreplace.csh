#!/bin/csh -f

set app=llreplace
# xcodebuild -list -project ${app}.xcodeproj

# rm -rf DerivedData/
# xcodebuild -configuration Release -alltargets clean
# xcodebuild -derivedDataPath ./DerivedData/lldupdir -scheme $app -configuration Release clean build
xcodebuild -scheme $app -configuration Release clean build

# echo -------------------
# find ./DerivedData -type f -name $app -perm +111 -ls
set src=./DerivedData/$app/Build/Products/Release/$app
set src=./DerivedData/Build/Products/Release/$app

echo
echo "---Install $src"
cp $src ~/opt/bin/

echo
echo "---Files "
ls -al $src  ~/opt/bin/$app

echo
echo "---Signed---"
codesign -dv  ~/opt/bin/$app |& grep Sig