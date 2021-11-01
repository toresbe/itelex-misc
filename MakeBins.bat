mkdir AlleBins
echo Inhalt:^<br^> >AlleBins\index.html
call MakeBin1.bat itlx_LeitungAnalog2 AnalogModem2\default\AnalogModem2.hex Mega168_ExtTakt
call MakeBin1.bat itlx_ED1000 ED1000\default\ED1000.hex Mega168_Quarz
call MakeBin1.bat itlx_ED1000-100Bd ED1000\100Baud\ED1000.hex Mega168_Quarz
call MakeBin1.bat itlx_ED1000-75Bd ED1000\75Baud\ED1000.hex Mega168_Quarz
call MakeBin1.bat itlx_ED1000-PROTO ED1000\FuerPrototyp\ED1000.hex Mega168_Quarz
call MakeBin1.bat itlx_V21 ED1000\V.21\ED1000.hex Mega168_Quarz
call MakeBin1.bat itlx_OhneFSG FernschrOhneFSG\default\FernschrOhneFSG.hex Mega168_Quarz
call MakeBin1.bat itlx_OhneFSG-EN FernschrOhneFSG\English\FernschrOhneFSG.hex Mega168_Quarz
call MakeBin1.bat itlx_OhneFSG-US45 FernschrOhneFSG\USTTY-45\FernschrOhneFSG.hex Mega168_Quarz
call MakeBin1.bat itlx_TW39-PL10 FernschrTW39\AltePlatine\FernschrTW39.hex Mega8_Quarz
call MakeBin1.bat itlx_TW39 FernschrTW39\default\FernschrTW39.hex Mega8_Quarz
call MakeBin1.bat itlx_TW39-US45 FernschrTW39\USTTY-45\FernschrTW39.hex Mega8_Quarz
call MakeBin1.bat itlx_FsV21-aufLA21 FernschrV21_aufLA2\default\FernschrV21.hex Mega168_ExtTakt
call MakeBin1.bat itlx_FsV21-aufLA21-EN FernschrV21_aufLA2\english\FernschrV21.hex Mega168_ExtTakt
call MakeBin1.bat itlx_FsV21-aufLA21-US45 FernschrV21_aufLA2\USTTY-45\FernschrV21.hex Mega168_ExtTakt
call MakeBin1.bat itlx_TW39plus FernschrTW39Plus\default\FernschrTW39plus.hex Mega168_Quarz
call MakeBin1.bat itlx_TW39plus-PL10 FernschrTW39Plus\AltePlatine\FernschrTW39plus.hex Mega168_Quarz
call MakeBin1.bat itlx_TW39plus-DOPAUS FernschrTW39Plus\DoppelAusgabe\FernschrTW39plus.hex Mega168_Quarz
call MakeBin1.bat itlx_TW39plus-US45 FernschrTW39Plus\USTTY-45\FernschrTW39plus.hex Mega168_Quarz
call MakeBin1.bat itlx_TW39plus-100Bd FernschrTW39Plus\100Baud\FernschrTW39plus.hex Mega168_Quarz
call MakeBin1.bat itlx_TW39plus-75Bd FernschrTW39Plus\75Baud\FernschrTW39plus.hex Mega168_Quarz
call MakeBin1.bat itlx_TW39plus-EN FernschrTW39Plus\English\FernschrTW39plus.hex Mega168_Quarz
call MakeBin1.bat itlx_Tloch15 Tloch15\default\Tloch15.hex Mega168_Quarz
call MakeBin1.bat itlx_Doppelstrom FernschrTW39Plus\Doppelstrom\Doppelstrom.hex Mega168_Quarz
call MakeBin1.bat itlx_Doppelstrom-EN FernschrTW39Plus\Doppelstrom-EN\Doppelstrom.hex Mega168_Quarz
call MakeBin1.bat itlx_Doppelstrom-75Bd FernschrTW39Plus\Doppelstrom-75Bd\Doppelstrom.hex Mega168_Quarz
call MakeBin1.bat itlx_Messgeraet Messgeraet\default\Messgeraet.hex Mega168_Quarz
call MakeBin1.bat itlx_Messgeraet-75BD Messgeraet\75baud\Messgeraet.hex Mega168_Quarz
call MakeBin1.bat itlx_Messgeraet-US45 Messgeraet\USTTY-45\Messgeraet.hex Mega168_Quarz
call MakeBin1.bat itlx_Messgeraet-plED Messgeraet\AufED1000\Messgeraet.hex Mega168_Quarz
call MakeBin1.bat itlx_SeriellUndSpeicher SeriellUndSpeicher\PlatVer13\SeriellUndSpeicher.hex Mega168_Quarz
call MakeBin1.bat itlx_SeriellUndSpeicher-CODESW SeriellUndSpeicher\PlatVer13-CodeSw\SeriellUndSpeicher.hex Mega168_Quarz
call MakeBin1.bat itlx_SeriellUndSpeicher-Full SeriellUndSpeicher\PlatVer13-Full\SeriellUndSpeicher.hex Mega328_Quarz
call MakeBin1.bat itlx_SeriellUndSpeicher2 SeriellUndSpeicher\PlatVer21\SeriellUndSpeicher.hex Mega168_Quarz
call MakeBin1.bat itlx_SeriellUndSpeicher2-CODESW SeriellUndSpeicher\PlatVer21-CodeSw\SeriellUndSpeicher.hex Mega168_Quarz
call MakeBin1.bat itlx_SeriellUndSpeicher2-Full SeriellUndSpeicher\PlatVer21-Full\SeriellUndSpeicher.hex Mega328_Quarz
rem itlx_SeriellUndSpeicher2-mZS (Zeitsperre) durch -Full ersetzt
rem OhneSpeicher wird nicht vertrieben
call MakeBin1.bat itlx_SeriellUndSpeicher2-KOI7N2 SeriellUndSpeicher\KOI7N2_21\SeriellUndSpeicher.hex Mega168_Quarz
call MakeBin1.bat itlx_SeriellUndSpeicher2-KOI8R SeriellUndSpeicher\KOI8R_21\SeriellUndSpeicher.hex Mega168_Quarz
call MakeBin1.bat itlx_SeriellUndSpeicher2-US45 SeriellUndSpeicher\USTTY-45_21\SeriellUndSpeicher.hex Mega168_Quarz
call MakeBin1.bat itlx_SeriellUndSpeicher2-110-7 SeriellUndSpeicher\110-7_21\SeriellUndSpeicher.hex Mega168_Quarz
call MakeBin1.bat itlx_SeriellUndSpeicher2-110-7_EN SeriellUndSpeicher\110-7_EN_21\SeriellUndSpeicher.hex Mega168_Quarz
call MakeBin1.bat itlx_HellschrSignal Hellschreiber\Release\HellschrSignal.hex Mega168_Quarz
call MakeBin1.bat itlx_HellschrKomm2 Hellschreiber\ReleaseVer21\HellschrKomm.hex Mega168_Quarz

rem UniIF wird nicht vertrieben
rem Wahlbrücke wird nicht vertrieben
rem Fuer alte Versionen des i-Telex mit Nachschlage-Tabelle:
copy AlleBins\itlx_LeitungAnalog2.bin AlleBins\AnalogModem.bin
copy AlleBins\itlx_LeitungAnalog2.txt AlleBins\AnalogModem.txt
copy AlleBins\itlx_ED1000.bin AlleBins\ED1000.bin
copy AlleBins\itlx_ED1000.txt AlleBins\ED1000.txt
copy AlleBins\itlx_TW39.bin AlleBins\FernschrTW39.bin
copy AlleBins\itlx_TW39.txt AlleBins\FernschrTW39.txt
copy AlleBins\itlx_Messgeraet.bin AlleBins\Messgeraet.bin
copy AlleBins\itlx_Messgeraet.txt AlleBins\Messgeraet.txt
copy AlleBins\itlx_SeriellUndSpeicher.bin AlleBins\SeriellUndSpeicher.bin
copy AlleBins\itlx_SeriellUndSpeicher.txt AlleBins\SeriellUndSpeicher.txt
pause
