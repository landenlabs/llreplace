#!/bin/csh -f

set app=llreplace
set buildType=Release
xcodebuild -list -project ${app}.xcodeproj

# rm -rf DerivedData/
xcodebuild -derivedDataPath ./DerivedData/${app} -scheme ${app}  -configuration ${buildType} clean build
# xcodebuild -configuration ${buildType} -alltargets clean

echo ---------------------
find ./DerivedData -type f -name ${app} -perm +444 -ls

echo ---------------------
set src=./DerivedData/Build/Products/${buildType}/${app}
echo File=$src
ls -al $src
cp $src ~/opt/bin/
