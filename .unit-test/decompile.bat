set SKETCH_PARENT_DIR=..\examples\
set SKETCH_NAME=LazyWireLogger

rmdir /S /Q .build
mkdir .build

mkdir ".build\%SKETCH_NAME%"
mkdir ".build\obj"

arduino-cli compile --fqbn arduino:avr:nano --output-dir .build/obj "%SKETCH_PARENT_DIR%\%SKETCH_NAME%\%SKETCH_NAME%.ino"

avr-objdump -S ".build/obj/%SKETCH_NAME%.ino.elf" > .build/res.asm

pause