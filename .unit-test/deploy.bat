cd ../..
tar -a -c -f PinkyvoltToolkit.zip -X PinkyvoltToolkit/.unit-test/deploy.exclude.txt PinkyvoltToolkit
arduino-cli lib install --zip-path PinkyvoltToolkit.zip
pause