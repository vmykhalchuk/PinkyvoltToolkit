arduino-cli compile --fqbn arduino:avr:nano --output-dir ./.build blink.ino
avr-objdump -S ./build/blink.ino.elf > blink.asm
pause