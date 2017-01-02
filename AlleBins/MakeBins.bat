C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\AnalogModem2\default\AnalogModem2.hex .\itlx_LeitungAnalog2.bin
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\ED1000\default\ED1000.hex .\itlx_ED1000.bin
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\ED1000\FuerPrototyp\ED1000.hex .\itlx_ED1000-PROTO.bin
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\ED1000\V.21\ED1000.hex .\itlx_V21.bin
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\FernschrOhneFSG\default\FernschrOhneFSG.hex .\itlx_OhneFSG.bin
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\FernschrOhneFSG\English\FernschrOhneFSG.hex .\itlx_OhneFSG-EN.bin
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\FernschrOhneFSG\USTTY-45\FernschrOhneFSG.hex .\itlx_OhneFSG-US45.bin
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\FernschrTW39\AltePlatine\FernschrTW39.hex .\itlx_TW39-PL10.bin
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\FernschrTW39\default\FernschrTW39.hex .\itlx_TW39.bin
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\FernschrTW39\USTTY-45\FernschrTW39.hex .\itlx_TW39-US45.bin
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\FernschrTW39Plus\AltePlatine\FernschrTW39plus.hex .\itlx_TW39plus-PL10.bin
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\FernschrTW39Plus\default\FernschrTW39plus.hex .\itlx_TW39plus.bin
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\FernschrTW39Plus\DoppelAusgabe\FernschrTW39plus.hex .\itlx_TW39plus-DOPAUS.bin
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\FernschrTW39Plus\USTTY-45\FernschrTW39plus.hex .\itlx_TW39plus-US45.bin
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\Messgeraet\default\Messgeraet.hex .\itlx_Messgeraet.bin
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\Messgeraet\USTTY-45\Messgeraet.hex .\itlx_Messgeraet-US45.bin
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\SeriellUndSpeicher\PlatVer13\SeriellUndSpeicher.hex .\itlx_SeriellUndSpeicher.bin
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\SeriellUndSpeicher\PlatVer21\SeriellUndSpeicher.hex .\itlx_SeriellUndSpeicher2.bin
rem OhneSpeicher wird nicht vertrieben
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\SeriellUndSpeicher\USTTY-45_21\SeriellUndSpeicher.hex .\itlx_SeriellUndSpeicher2-US45.bin
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\UniversalIF\RS232-Hex\UniversalIF.hex .\itlx_UniIF-RS232-Hex.bin
rem Wahlbrücke wird nicht vertrieben
rem Fuer alte Versionen des i-Telex mit Nachschlage-Tabelle:
copy .\itlx_LeitungAnalog2.bin AnalogModem.bin
copy .\itlx_ED1000.bin ED1000.bin
copy .\itlx_TW39.bin FernschrTW39.bin
copy .\itlx_Messgeraet.bin Messgeraet.bin
copy .\itlx_SeriellUndSpeicher.bin SeriellUndSpeicher.bin
pause
