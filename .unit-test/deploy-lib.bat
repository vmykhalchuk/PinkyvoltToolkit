set LIB_NAME=PinkyvoltToolkit
set UNIT_TEST_DIR=.unit-test

rmdir /S /Q .build

mkdir .build
cd ..
tar -a -c -f %UNIT_TEST_DIR%/.build/%LIB_NAME%.zip -X %UNIT_TEST_DIR%/deploy.exclude.txt ./*
cd %UNIT_TEST_DIR%

mkdir .build\temp
mkdir .build\temp\%LIB_NAME%
tar -x -f .build/%LIB_NAME%.zip -C .build/temp/%LIB_NAME%

mkdir .build\res
tar -a -c -f .build/res/%LIB_NAME%.zip -C .build/temp %LIB_NAME%

arduino-cli lib install --zip-path .build/res/%LIB_NAME%.zip
pause