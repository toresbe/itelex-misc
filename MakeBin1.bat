rem Parameter: %1=Ziel %2=Quelle %3=Fuse-Musterdatei
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary %2 AlleBins\%1.bin
copy FuseMuster\%3.txt AlleBins\%1.txt
echo %1 ^<br^> >>AlleBins\index.html
