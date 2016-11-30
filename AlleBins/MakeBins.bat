C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\AnalogModem2\default\AnalogModem2.hex .\TxP2_LeitungAnalog2.bin
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\ED1000\default\ED1000.hex .\TxP2_ED1000.bin
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\ED1000\FuerPrototyp\ED1000.hex .\TxP2_ED1000-PROTO.bin
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\FernschrTW39\default\FernschrTW39.hex .\TxP2_TW39.bin
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\FernschrTW39\AltePlatine\FernschrTW39.hex .\TxP2_TW39-PL10.bin
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\FernschrTW39\USTTY-45\FernschrTW39.hex .\TxP2_TW39-US45.bin
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\Messgeraet\default\Messgeraet.hex .\TxP2_Messgeraet.bin
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\SeriellUndSpeicher\PlatVer13\SeriellUndSpeicher.hex .\TxP2_SeriellUndSpeicher.bin
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\SeriellUndSpeicher\default\SeriellUndSpeicher.hex .\TxP2_SeriellUndSpeicher2.bin
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\SeriellUndSpeicher\USTTY-45\SeriellUndSpeicher.hex .\TxP2_SeriellUndSpeicher2-US45.bin
rem Fuer alte Versionen des i-Telex mit Nachschlage-Tabelle:
copy .\TxP2_LeitungAnalog2.bin AnalogModem.bin
copy .\TxP2_ED1000.bin ED1000.bin
copy .\TxP2_TW39.bin FernschrTW39.bin
copy .\TxP2_Messgeraet.bin Messgeraet.bin
copy .\TxP2_SeriellUndSpeicher.bin SeriellUndSpeicher.bin
