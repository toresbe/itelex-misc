cd AnalogModem2
make -C default
cd ..

cd ED1000
call makeall.bat
cd ..

cd FernschrOhneFSG
call makeall.bat
cd ..

cd FernschrTW39
call makeall.bat
cd ..

cd FernschrTW39plus
call makeall.bat
cd ..

cd FernschrV21_aufLA2
call makeall.bat
cd ..

cd Messgeraet
call makeall.bat
cd ..

cd SeriellUndSpeicher
call makeall.bat
cd ..

cd UniversalIF
call makeall.bat
cd ..

cd Wahlbruecke
make -C default
cd ..
