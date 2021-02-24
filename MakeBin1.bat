rem Parameter: %1=Ziel %2=Quelle %3=Fuse-Musterdatei
"c:\Program Files (x86)\Atmel\Studio\7.0\toolchain\avr8\avr8-gnu-toolchain\bin\avr-objcopy.exe" -I ihex -O binary %2 AlleBins\%1.bin
copy FuseMuster\%3.txt AlleBins\%1.txt
echo %1 ^<br^> >>AlleBins\index.html
