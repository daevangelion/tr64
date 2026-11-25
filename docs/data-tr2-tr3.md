# Classic Tomb Raider Steam data: II and III cook inputs

## Releases and ownership

The [Steam Store appdetails API](https://store.steampowered.com/api/appdetails?appids=225300)
identifies I–V as the original Windows PC releases, distinct from the
remasters. The saved desktop Steam login on `<remote-host>` worked. The Steam
console's `licenses_for_app` reported an active Tomb Raider Collection
licence (package `26663`) for all five classics and no licence for either
remaster.

| Title | App ID | Owned | Windows depot | Installed/depot bytes | Download bytes |
| --- | ---: | :---: | ---: | ---: | ---: |
| Tomb Raider I (1996) | 224960 | Yes | [224961](https://steamdb.info/depot/224961/) | 318,059,021 | 232,068,720 |
| Tomb Raider II (1997) | 225300 | Yes | [225301](https://steamdb.info/depot/225301/) | 349,264,865 | 256,221,056 |
| Tomb Raider III (1998) | 225320 | Yes | [225321](https://steamdb.info/depot/225321/) | 490,731,953 | 327,490,432 |
| Tomb Raider IV: The Last Revelation (1999) | 224980 | Yes | [224981](https://steamdb.info/depot/224981/) | 673,762,308 | 601,485,792 |
| Tomb Raider V: Chronicles (2000) | 225000 | Yes | [225001](https://steamdb.info/depot/225001/) | 579,694,975 | 451,477,776 |
| Tomb Raider I–III Remastered | 2478970 | No | [2478971](https://steamdb.info/depot/2478971/) | 6,626,603,937 | 3,960,595,952 |
| Tomb Raider IV–VI Remastered | 2525380 | No | [2525381](https://steamdb.info/depot/2525381/) | 12,600,920,523 | 6,952,413,824 |

For II–V, the installed and download bytes above are the observed
`SizeOnDisk` and completed `BytesDownloaded` in the local Steam manifests.
For I and the uninstalled remasters, download bytes come from the
[public Steam app-info mirror](https://api.steamcmd.net/v1/info/224960),
which also gives the remaster depot sizes. Tomb Raider I's installed
manifest agrees with its public depot size. Neither remaster was installed.

## Installation and disk space

The saved GUI login was used on a private Xvfb display (`:61`). One title
at a time, `steam://install/<appid>` installed II, III, IV and V in that
order. II, III and V presented SteamOS compatibility warnings; Steam
allowed installation via **Install** without a Steam Play settings change.
IV proceeded directly to the regular Install dialog. No game was launched.

On 2026-11-21, on the owner's explicit instruction, the **Crystal
Dynamics/Eidos Interactive Terms of Service** (last updated September 5,
2022) were accepted in Steam's EULA prompt separately for Tomb Raider II,
Tomb Raider III: Adventures of Lara Croft, Tomb Raider IV: The Last
Revelation, and Tomb Raider V: Chronicles. No other agreement was accepted.

Steam's `content_log.txt` recorded `Fully Installed` and `finished update`
for each new title, and all four manifests reached `StateFlags=4`. Each
preflight allowed at least 2 GB free after the new depot, even reserving
another depot's worth of space for temporary download staging.

| Title | Installed build ID | Free before install (bytes) | Free less depot (bytes) |
| --- | ---: | ---: | ---: |
| Tomb Raider II (1997) | `24581` | 7,340,650,496 | 6,991,385,631 |
| Tomb Raider III (1998) | `24585` | 44,698,677,248 | 44,207,945,295 |
| Tomb Raider IV: The Last Revelation (1999) | `23331` | 43,672,657,920 | 42,998,895,612 |
| Tomb Raider V: Chronicles (2000) | `35793` | 42,802,421,760 | 42,222,726,785 |

`df -h ~` on `<remote-host>` reported 6.3G free initially (6,761,197,568 bytes).
The owner made room during the install sequence. After the final shutdown
it reported 40G free (41,967,132,672 bytes). After `steam -shutdown`,
`pgrep` showed no Steam client, SteamCMD, Steam WebHelper, or Steam runtime
processes, and our Xvfb PID, display socket, lock and scratch directory were
gone. The only `finished update` entries during the authorized session were
for II, III, IV and V.

Only II and III game-flow scripts, level-format files (including in-engine
cutscenes), and sound effects were copied: 62 files, 190,131,096 bytes,
with each SHA-256 verified against `<remote-host>`. Local input directories are
read-only (`0555`, regular files `0444`). Music, video, game executables,
installer/runtime files and auxiliary images remain on `<remote-host>`. The IV and V
installs were inventoried but not copied. No game data is committed.

## Tomb Raider II

- Steam install: `<steam-library>/steamapps/common/Tomb Raider (II)/`.
- `data/` contains 24 `.TR2` files (80,473,884 bytes): 19 map/training
  files (71,598,880 bytes), four in-engine `CUT*.TR2` scenes (8,263,112
  bytes), and `TITLE.tr2` (611,892 bytes). `data/TOMBPC.DAT` is the
  5,340-byte game-flow script; `data/MAIN.SFX` holds 7,961,208 bytes of
  sound effects. The root `tombpc.dat` is a byte-identical duplicate of
  `data/TOMBPC.DAT` (matching SHA-256); it was not copied twice.
- Music: 60 loose MP3 files in `music/` (85,855,387 bytes), plus
  `audio/cdaudio.mp3` (42,848,365 bytes) and `audio/cdaudio.dat`
  (1,656 bytes). Video: eight Escape `.RPL` files in `fmv/`
  (126,269,710 bytes).
- The game data are loose Windows PC files, not inside an emulated disc
  image. The executable, bundled installer/runtime files, music, and FMV
  were left on `<remote-host>`.

### Copied data and SHA-256

The 26 files (88,440,432 bytes) in `$TR64_DATA/pc-tr2/` are
relative to Steam's `data/` directory; filename case is preserved.

| Local relative path | Bytes | SHA-256 |
| --- | ---: | --- |
| `ASSAULT.TR2` | 3,471,450 | `395e33f995fb1a5e56bc016c3cd7de781c1078fb0801ccc53a6893a0a2303a92` |
| `BOAT.TR2` | 3,945,738 | `a061b05a6dcf584a471da375bb3ce11fc819d520cb7c9e943aa461f0f12602db` |
| `catacomb.TR2` | 3,605,052 | `04ac407aa2104aeffb9e6bd71d6c98a93d7508d1213033fb53010b128267b6f3` |
| `CUT1.TR2` | 1,767,052 | `71ef97c08c18e7643f411d550011228d8ba8d9e6f64ae3a18719586d9e1b7957` |
| `cut2.TR2` | 2,037,544 | `6c297be78dcb4c73deb49baacdbaca93969088c97a3f4080b4c125026382ac98` |
| `cut3.TR2` | 2,558,286 | `e31631e9e56490c5f80ef0d77ec1a8310be7d57d63e0a54785bf26dcc10b3b75` |
| `cut4.TR2` | 1,900,230 | `274afd911fec603e6b6b4a3d126163448910189e0caa8cd1129552911719d2f0` |
| `deck.TR2` | 3,890,042 | `89fb398b9ac4e7ca7c8ad2bb98b73a6df3bd66c938e93866bbae3b1244ef4383` |
| `Emprtomb.tr2` | 4,291,468 | `4f13d2989bfc69bce31b8a007a6fc5850fb3cb9c9e1ba134b3f36234aba54173` |
| `FLOATING.TR2` | 4,007,486 | `5d33c17f00346f242a8ddc4d100b8684ad5c670a7dca2dc1191085901fee9f3c` |
| `house.TR2` | 3,693,108 | `7f362fbb6c3011a91dee270e9011048788f6c78c159dc053b9f84768707af7b3` |
| `icecave.TR2` | 3,803,398 | `a1cb7d9fe0d02b82ee109513574a450cae5beb47f6238b8203927a0a897a71e9` |
| `keel.TR2` | 3,995,356 | `e3a600ca213795c2a7a0d717a9e1de0faf696ea93a578a83a7ca7b6413421f1a` |
| `living.TR2` | 3,501,472 | `d8540123be55b61ac59904fafaf1dbf76c55f8492acd7bd4823143e2a9e5c136` |
| `MAIN.SFX` | 7,961,208 | `74cd20e38dec5db3c31f21d2a5699f087f0cc15ba98e2e5c3333d0aef46f5be0` |
| `MONASTRY.TR2` | 4,325,562 | `aa332c29ec2d16fdfea6401fe5d98b5a7df260e353cbb25409130d779da823be` |
| `OPERA.TR2` | 3,996,500 | `e2abd46162759ff34db1bcb8063bd9d16f6302ab4934c3e6922c47c4a8909611` |
| `platform.TR2` | 4,101,590 | `f6b0ab2d8cef28dad19e6865cb0b1811797fa88ac42ed550b2c3b127ce717b41` |
| `RIG.TR2` | 3,944,472 | `aa2b25d2ba3d024588061bd9acd31c07dec89c262c2fd944ac97b66de9d4f125` |
| `SKIDOO.TR2` | 3,521,106 | `baff7a508ebfbc85db2595cb00c1c2870d1c46d0c34eddfc49d81513b8bc5918` |
| `TITLE.tr2` | 611,892 | `7be89a7ba782ca07e364bc98164c17f45afd536e810e5b05ff54a333b653e229` |
| `TOMBPC.DAT` | 5,340 | `d1f1a9c8f784301824ed6c59c8b34378d84e7f7fc93c90f5df7c4a5c8ed9868c` |
| `unwater.TR2` | 3,131,814 | `dd4b3156ca4b7c3a9629fdf3dd030d719712e5621837d81ec7de4dc531b80f63` |
| `venice.TR2` | 4,213,070 | `46fe2ac17a8629393a00866293adfe7f79b444909c81406920f74b86be6f712f` |
| `WALL.TR2` | 2,986,356 | `a6e0acef3c28a0ca37b1262fb1040279ba4ef8f0efc95731065f181ebc3d1d3b` |
| `xian.TR2` | 3,173,840 | `d8f4d7b384434b8c649caf505b4786e26c34761c6518b27b78a35b9090f8e7b9` |

### Installed tree

All paths below are relative to the Steam install; file sizes are bytes.

```text
audiere.dll                                        512000
audio/
audio/cdaudio.dat                                  1656
audio/cdaudio.mp3                                  42848365
data/
data/_INST32I.EX_                                  316937
data/_ISDEL.EXE                                    8192
data/_setup.dll                                    11264
data/_sys1.cab                                     181745
data/_user1.cab                                    10082
data/ASSAULT.TR2                                   3471450
data/autorun.exe                                   170496
data/autorun.ico                                   766
data/autorun.inf                                   49
data/BOAT.TR2                                      3945738
data/catacomb.TR2                                  3605052
data/credit01.PCX                                  48302
data/credit02.PCX                                  50453
data/credit03.PCX                                  46974
data/credit04.PCX                                  46493
data/credit05.PCX                                  59882
data/credit06.PCX                                  47959
data/credit07.PCX                                  54619
data/credit08.PCX                                  58867
data/credit09.PCX                                  112771
data/CUT1.TR2                                      1767052
data/cut2.TR2                                      2037544
data/cut3.TR2                                      2558286
data/cut4.TR2                                      1900230
data/data                                          5340
data/DATA.TAG                                      101
data/data1.cab                                     549037
data/deck.TR2                                      3890042
data/dsetup.dll                                    127488
data/dsetup16.dll                                  63056
data/dsetup32.dll                                  41984
data/Emprtomb.tr2                                  4291468
data/END.PCX                                       112773
data/FLOATING.TR2                                  4007486
data/house.TR2                                     3693108
data/icecave.TR2                                   3803398
data/keel.TR2                                      3995356
data/lang.dat                                      352
data/language.dat                                  4881
data/layout.bin                                    353
data/legal.PCX                                     68005
data/living.TR2                                    3501472
data/MAIN.SFX                                      7961208
data/MONASTRY.TR2                                  4325562
data/OPERA.TR2                                     3996500
data/os.dat                                        419
data/platform.TR2                                  4101590
data/RIG.TR2                                       3944472
data/setup.BMP                                     165400
data/SETUP.EXE                                     59392
data/SETUP.INI                                     30
data/setup.ins                                     56261
data/setup.lid                                     47
data/SKIDOO.TR2                                    3521106
data/TITLE.PCX                                     209815
data/TITLE.tr2                                     611892
data/TOMBPC.DAT                                    5340
data/unwater.TR2                                   3131814
data/venice.TR2                                    4213070
data/WALL.TR2                                      2986356
data/xian.TR2                                      3173840
Dec130.dll                                         98816
Edec.dll                                           117248
fmv/
fmv/ANCIENT.RPL                                    36931896
fmv/CRASH.RPL                                      20387254
fmv/END.RPL                                        11242086
fmv/JEEP.RPL                                       21942472
fmv/LANDING.RPL                                    4452622
fmv/LOGO.RPL                                       15996067
fmv/MODERN.RPL                                     4765135
fmv/MS.RPL                                         10552178
installscript.vdf                                  457
msvcp90.dll                                        568832
msvcr90.dll                                        655872
music/
music/10.mp3                                       371650
music/11.mp3                                       514435
music/12.mp3                                       281470
music/13.mp3                                       300675
music/14.mp3                                       121334
music/15.mp3                                       114755
music/16.mp3                                       151305
music/17.mp3                                       298170
music/18.mp3                                       95018
music/19.mp3                                       94287
music/2.mp3                                        2781300
music/20.mp3                                       112562
music/21.mp3                                       142533
music/22.mp3                                       104521
music/23.mp3                                       543035
music/24.mp3                                       313846
music/25.mp3                                       95018
music/26.mp3                                       1678856
music/27.mp3                                       3791190
music/28.mp3                                       3769786
music/29.mp3                                       5329149
music/3.mp3                                        3677431
music/30.mp3                                       5883589
music/31.mp3                                       256524
music/32.mp3                                       157159
music/33.mp3                                       518601
music/34.mp3                                       1746788
music/35.mp3                                       641144
music/36.mp3                                       5086685
music/37.mp3                                       398474
music/38.mp3                                       374259
music/39.mp3                                       482182
music/4.mp3                                        4355546
music/40.mp3                                       577372
music/41.mp3                                       1896568
music/42.mp3                                       1075032
music/43.mp3                                       149540
music/44.mp3                                       455254
music/45.mp3                                       327395
music/46.mp3                                       402022
music/47.mp3                                       278234
music/48.mp3                                       5242230
music/49.mp3                                       2301036
music/5.mp3                                        258925
music/50.mp3                                       248694
music/51.mp3                                       1330426
music/52.mp3                                       322489
music/53.mp3                                       1576032
music/54.mp3                                       6708465
music/55.mp3                                       6713579
music/56.mp3                                       3383388
music/57.mp3                                       724626
music/58.mp3                                       701684
music/59.mp3                                       574032
music/6.mp3                                        232101
music/60.mp3                                       5321836
music/61.mp3                                       20489
music/7.mp3                                        125720
music/8.mp3                                        123631
music/9.mp3                                        201310
readme.txt                                         19476
sound.dll                                          26112
Tomb2.exe                                          912896
tombpc.dat                                         5340
WINPLAY.DLL                                        66048
WINSDEC.DLL                                        89600
WINSTR.DLL                                         86016
```

## Tomb Raider III

- Steam install: `<steam-library>/steamapps/common/TombRaider (III)/`.
- There are 34 `.TR2` files (90,038,196 bytes): 22 map/training files in
  `data/` (70,467,578 bytes), 11 in-engine scenes in `cuts/` (18,295,352
  bytes), and `data/TITLE.TR2` (1,275,266 bytes). `data/TOMBPC.DAT` is
  the 5,550-byte game-flow script; `data/MAIN.SFX` holds 11,646,918
  bytes of sound effects.
- Music: `audio/cdaudio.wad` (239,634,998 bytes), one container for
  WAV/MS-ADPCM tracks. Its 130-entry directory contains 119 nonempty
  tracks (239,600,158 WAV bytes), counted from the directory described in
  [TRosettaStone §8.1.2](https://opentomb.github.io/TRosettaStone3/trosettastone.html#_tr3).
  Video: five Escape `.RPL` files in `fmv/` (130,333,733 bytes).
- The game data are loose Windows PC files, not inside an emulated disc
  image. The executable, bundled installer/runtime files, music, and FMV
  were left on `<remote-host>`.

### Copied data and SHA-256

The 36 files (101,690,664 bytes) in `$TR64_DATA/pc-tr3/` keep
Steam's `data/` files at the local root and retain the original `cuts/`
subdirectory for cutscenes; filename case is preserved.

| Local relative path | Bytes | SHA-256 |
| --- | ---: | --- |
| `cuts/CUT1.TR2` | 1,547,866 | `80dff629444175d305a69ebfe6237d0b7f203642ba505ce1178adb180e1bf0d5` |
| `cuts/CUT11.TR2` | 1,402,003 | `04ccdad297502045d62418fc33ea33e8160c9a16d27c1355c67ba616de833acf` |
| `cuts/CUT12.TR2` | 2,321,393 | `a33534a1d7493d34bb3748b6c175e34c8e4c0587b5348d86e07200e8030ad9a8` |
| `cuts/CUT2.TR2` | 1,957,327 | `92526110a4821075d84592b5489058361ca9d25e335c51f6f3514fc296f3c059` |
| `cuts/CUT3.TR2` | 2,020,225 | `3825974237dfa92a7d706c7bdcb00ef07ee91a9a24d5dd1c69e8b4ad78e983b5` |
| `cuts/CUT4.TR2` | 1,048,466 | `a469d092309188b5dbb0f024fe27bca1b4cb005a6f65b435b80b6b7d3947e5b4` |
| `cuts/CUT5.TR2` | 1,580,868 | `0a55a72b335cda44e87e471d565410140a6509d5b5c342192e296a2b36327baa` |
| `cuts/CUT6.TR2` | 2,430,940 | `9b6729dcff3681bed3cd4de166c574053876359429c3312aeecf45a8c05bc9b2` |
| `cuts/CUT7.TR2` | 1,253,227 | `4e619fe5519713da2fa3bcc1a23cc1c821171b7621a6b23c932cc4e3b9712231` |
| `cuts/CUT8.TR2` | 944,962 | `1341c81c6e40de65cf8194ac29aef5287d122dd98e94615baef28e362c0810b3` |
| `cuts/CUT9.TR2` | 1,788,075 | `f294e143cb9a5f2445ac053521f966e843cbed4798d4cdeeff912e2ade1e3a74` |
| `ANTARC.TR2` | 3,515,272 | `e5336e79f5eff6129ee71bd10d6ccae1bdd4685309b06847c62b3e5cea00cd18` |
| `AREA51.TR2` | 3,530,130 | `d792957f1e5fe9ac2ae0d03931864685c9b2d05f062db5c2d5ddc263f01a6a98` |
| `CHAMBER.TR2` | 3,190,736 | `ae29f01a0d35de9504b1a9634a29d1ec0525e3bafb738c0dac23c806632ce747` |
| `CITY.TR2` | 3,575,873 | `b455d5205286f3b8d8e5af8e7ec093e59c4f5a244befdb162c4e891a47928827` |
| `COMPOUND.TR2` | 3,554,880 | `d523d38d7a7b8d3ac4822e3b7bba5b939ac5d9b13b75eaad8ef84a904d28cbcf` |
| `CRASH.TR2` | 3,331,132 | `d398ade33e62f14b6e48a658dfea2128eed4c8b867045ad3e2dcf726f58a623a` |
| `HOUSE.TR2` | 3,437,855 | `51f165edfaa8b8808a5cc8919704dcae4266ee58430c7850cc2aa10953e8c508` |
| `JUNGLE.TR2` | 3,360,001 | `e72190e3c011f5d76cc596d0806cc78f70fbce5e27468edfcfa9bbf5befd17ee` |
| `MAIN.SFX` | 11,646,918 | `f99289b021d974b2e088d87ee43bca17323afb993df30b58e86fd405c32c9763` |
| `MINES.TR2` | 3,331,087 | `12cafdba23bb82141f7b223725075d2787a9618c8975c59547bcd185f000ced4` |
| `NEVADA.TR2` | 3,140,417 | `debf2a3f89ce4f6f673c6c8650bd1bc10a10002d407184fa36753c3ec52a09d7` |
| `OFFICE.TR2` | 2,504,910 | `0286e2907afe5b44d07ac2ebfb85932c1888b7cb840b68a86fde39f875967e80` |
| `QUADCHAS.TR2` | 3,131,453 | `ebf3c22af417ee643afd2e1bca2bed9a7c3c3b6a1634eb31bce81f0deec616ca` |
| `RAPIDS.TR2` | 3,148,248 | `c1040cfe22a19392d5747b2c3b0b138a5ad24ec7010dea9a8959c363af185cb1` |
| `ROOFS.TR2` | 3,579,476 | `2cf15e3e081fb958296932d0703989b5f7af6f3667068a4340d70672a6e40d7e` |
| `SEWER.TR2` | 3,670,149 | `3897d00df7638480b091a5fe9f98a8ba2ec331db19ecd1c9844224f7eb521fba` |
| `SHORE.TR2` | 3,365,562 | `c00acd6f6e20e2d24d28c569f4ebc2b533970ea4e39568fd74a2f910ce73d8c5` |
| `STPAUL.TR2` | 3,140,028 | `4bceebcd97dba90683395795c379b3ed324642762bef06ebdb6971df4250ff84` |
| `TEMPLE.TR2` | 3,180,412 | `5404a0b39afebf3a61fa54949d1d63190740d81e44500a43fc98164bcb69aabe` |
| `TITLE.TR2` | 1,275,266 | `479c3f3e791c2fe014a1ca12f4c2e36916536d167686258284eade0b360499ea` |
| `TOMBPC.DAT` | 5,550 | `1191d732e1e3beaa8bee15c7cc16d8b5e019e2ecb15cf0cacc11a1b121f5f632` |
| `TONYBOSS.TR2` | 2,618,437 | `c6c025833652c9c0a869ea0439375164f03ed6aff8c6a628184b1e352fe26fe0` |
| `TOWER.TR2` | 3,523,586 | `28c380baa48bacdcbed8ceb04a6a65984c4825698f303d549c2efce22574d296` |
| `TRIBOSS.TR2` | 2,276,838 | `bbf1b13e76ac50be3b91e7d4dc99d990ccb9ce0257a00bac6354d3d50efb4c1e` |
| `VICT.TR2` | 2,361,096 | `ad74e2978ee5355fcd24858f106a1a99a6530c4a523c2130a0f382bdef97f710` |

### Installed tree

All paths below are relative to the Steam install; file sizes are bytes.

```text
audio/
audio/cdaudio.wad                                  239634998
config.txt                                         244
cuts/
cuts/CUT1.TR2                                      1547866
cuts/CUT11.TR2                                     1402003
cuts/CUT12.TR2                                     2321393
cuts/CUT2.TR2                                      1957327
cuts/CUT3.TR2                                      2020225
cuts/CUT4.TR2                                      1048466
cuts/CUT5.TR2                                      1580868
cuts/CUT6.TR2                                      2430940
cuts/CUT7.TR2                                      1253227
cuts/CUT8.TR2                                      944962
cuts/CUT9.TR2                                      1788075
data/
data.bin                                           84
DATA.TAG                                           106
data/ANTARC.TR2                                    3515272
data/AREA51.TR2                                    3530130
data/CHAMBER.TR2                                   3190736
data/CITY.TR2                                      3575873
data/COMPOUND.TR2                                  3554880
data/CRASH.TR2                                     3331132
data/HOUSE.TR2                                     3437855
data/JUNGLE.TR2                                    3360001
data/MAIN.SFX                                      11646918
data/MINES.TR2                                     3331087
data/NEVADA.TR2                                    3140417
data/OFFICE.TR2                                    2504910
data/QUADCHAS.TR2                                  3131453
data/RAPIDS.TR2                                    3148248
data/ROOFS.TR2                                     3579476
data/SEWER.TR2                                     3670149
data/SHORE.TR2                                     3365562
data/STPAUL.TR2                                    3140028
data/TEMPLE.TR2                                    3180412
data/TITLE.TR2                                     1275266
data/TOMBPC.DAT                                    5550
data/TONYBOSS.TR2                                  2618437
data/TOWER.TR2                                     3523586
data/TRIBOSS.TR2                                   2276838
data/VICT.TR2                                      2361096
DEC130.DLL                                         97280
EDEC.DLL                                           116224
fmv/
fmv/Crsh_Eng.rpl                                   10740561
fmv/Endgame.rpl                                    10686681
fmv/Intr_Eng.rpl                                   41939290
fmv/logo.rpl                                       15996067
fmv/Sail_Eng.rpl                                   50971134
installscript.vdf                                  603
lang.dat                                           4557
layout.bin                                         334
os.dat                                             417
pix/
pix/ANTARC.BMP                                     921654
pix/CREDIT01.BMP                                   921656
pix/CREDIT02.BMP                                   921656
pix/CREDIT03.BMP                                   921656
pix/CREDIT04.BMP                                   921656
pix/CREDIT05.BMP                                   921656
pix/CREDIT06.BMP                                   921656
pix/CREDIT07.BMP                                   921654
pix/CREDIT08.BMP                                   921654
pix/CREDIT09.BMP                                   921656
pix/HOUSE.BMP                                      921656
pix/INDIA.BMP                                      921654
pix/LEGAL.BMP                                      921656
pix/LONDON.BMP                                     921654
pix/NEVADA.BMP                                     921654
pix/SOUTHPAC.BMP                                   921654
pix/THEEND.BMP                                     921654
pix/THEEND2.BMP                                    921654
pix/TITLEUK.BMP                                    921656
support/
support/info/
support/info/core.jpg                              9706
support/info/eidos.gif                             841
support/info/Ninja.jpg                             26946
support/info/ninjahead.gif                         4922
support/info/Readme.txt                            11683
support/info/slegal.gif                            1006
support/info/support.htm                           42151
support/info/tr3logo.jpg                           36685
<retail Windows executable; name omitted by provenance rule> 966656
WINPLAY.DLL                                        66048
WINSDEC.DLL                                        88576
WINSTR.DLL                                         86016
```

## Tomb Raider IV: The Last Revelation

- Steam install: `<steam-library>/steamapps/common/Tomb Raider (IV) The Last Revelation/`.
- `data/` contains 38 `.TR4` level-format files (168,006,231 bytes):
  37 non-title files (164,999,076 bytes) and `title.tr4` (3,007,155
  bytes). `data/cutseq.pak` is the 724,500-byte in-engine cutscene pack.
- `SCRIPT.DAT` is the 6,089-byte script; `English.dat` is the 6,544-byte
  language file. There is no separate `MAIN.SFX`; the PC level format
  carries samples in the levels, as described by
  [TRosettaStone §2.1](https://opentomb.github.io/TRosettaStone3/trosettastone.html#_file_types).
- Music: 112 loose `.wav` tracks under `audio/` (180,701,024 bytes).
  Video: 13 Bink `.bik` files under `fmv/` (323,123,180 bytes).
- This is a loose Windows PC install, not an emulated disc image. All IV
  files were left on `<remote-host>`; no IV cook inputs were copied.

### Installed tree

All paths below are relative to the Steam install; file sizes are bytes.

```text
audio/
audio/001_VonCroy2.wav                             548954
audio/002_VonCroy3.wav                             884826
audio/003_VonCroy4.wav                             544858
audio/004_VonCroy5.wav                             503898
audio/005_VonCroy6_Lara2.wav                       714842
audio/006_VonCroy7.wav                             426074
audio/007_VonCroy8.wav                             673882
audio/008_VonCroy9a.wav                            180314
audio/009_VonCroy9b_Lara3.wav                      677978
audio/010_VonCroy10.wav                            217178
audio/011_VonCroy11a.wav                           393306
audio/012_VonCroy11b.wav                           393306
audio/013_VonCroy12_13a_Lara4.wav                  675930
audio/014_VonCroy13b.wav                           399450
audio/015_VonCroy14.wav                            223322
audio/016_VonCroy15.wav                            620634
audio/017_VonCroy16_lara5.wav                      1013850
audio/018_VonCroy17.wav                            581722
audio/019_Lara6_VonCroy18.wav                      876634
audio/020_VonCroy19.wav                            487514
audio/021_VC20_L7_VC21_L8_VC22a.wav                1071194
audio/022_VonCroy22b.wav                           522330
audio/023_VonCroy23.wav                            419930
audio/024_VonCroy24a.wav                           438362
audio/025_VonCroy24b.wav                           417882
audio/026_VC25_L9_VC26_L10.wav                     1032282
audio/027_VonCroy27.wav                            507994
audio/028_VonCroy28_L11.wav                        522330
audio/029_VonCroy29.wav                            671834
audio/030_VonCroy30.wav                            372826
audio/031_VonCroy31_L12.wav                        600154
audio/032_VonCroy32_L13.wav                        612442
audio/033_VonCroy33.wav                            587866
audio/034_VonCroy34.wav                            489562
audio/035_VonCroy35.wav                            505946
audio/036_VonCroy36.wav                            503898
audio/037_VC37_L15_VC38.wav                        1450074
audio/038_A_Short_01.wav                           991322
audio/039_TR4_Title_Q10.wav                        6090842
audio/040_Action_Part_ii.wav                       634970
audio/041_Action_Part_iii.wav                      548954
audio/042_Action_Part_iv.wav                       354394
audio/043_Action_Part_v.wav                        358490
audio/044_Attack_part_i.wav                        139354
audio/045_Authentic_TR.wav                         714842
audio/046_Boss_01.wav                              4102234
audio/047_Boss_02.wav                              3393626
audio/048_Close_to_the_End.wav                     1751130
audio/049_Close_to_the_End_part_ii.wav             1108058
audio/050_Underwater_Find_part_i.wav               731226
audio/051_Egyptian_Mood_Part_i.wav                 1259610
audio/052_Egyptian_Mood_Part_ii.wav                2809946
audio/053_General_Part_i.wav                       510042
audio/054_General_Part_ii.wav                      495706
audio/055_General_Part_iii.wav                     821338
audio/056_General_Part_iv.wav                      1058906
audio/057_General_Part_v.wav                       575578
audio/058_Gods_Part_i.wav                          718938
audio/059_Gods_Part_ii.wav                         864346
audio/060_Gods_Part_iii.wav                        778330
audio/061_In_The_Pyramid_Part_i.wav                747610
audio/062_Jeep_Thrills_max.wav                     4333658
audio/063_Misc_Inc_01.wav                          395354
audio/064_Misc_Inc_02.wav                          585818
audio/065_Misc_Inc_03.wav                          628826
audio/066_Misc_Inc_04.wav                          909402
audio/067_Mystery_Part_i.wav                       960602
audio/068_Mystery_Part_ii.wav                      864346
audio/069_Mystery_Part_iii.wav                     2742362
audio/070_Mystery_Part_iv.wav                      1816666
audio/071_Ominous_Part_i.wav                       749658
audio/072_Puzzle_part_i.wav                        2568282
audio/073_Secret.wav                               288858
audio/074_backpack.wav                             2740314
audio/075_captain1.wav                             5118042
audio/076_captain2.wav                             868442
audio/077_crocgod.wav                              5202010
audio/078_croywon.wav                              1218650
audio/079_crypt1.wav                               4776026
audio/080_crypt2.wav                               3432538
audio/081_dig.wav                                  694362
audio/082_finale.wav                               5038170
audio/083_horus.wav                                1157210
audio/084_inscrip.wav                              2992218
audio/085_intro.wav                                770138
audio/086_jeepA.wav                                1288282
audio/087_jeepB.wav                                1329242
audio/088_key.wav                                  1001562
audio/089_larawon.wav                              1466458
audio/090_libend.wav                               4010074
audio/091_minilib1.wav                             1319002
audio/092_minilib2.wav                             1171546
audio/093_minilib3.wav                             2310234
audio/094_minilib4.wav                             1548378
audio/095_phildoor.wav                             747610
audio/096_sarcoph.wav                              1095770
audio/097_scorpion.wav                             1464410
audio/098_throne.wav                               1658970
audio/099_whouse.wav                               3358810
audio/100_Attack_part_ii.wav                       180314
audio/101_A1_In_Dark.wav                           6594650
audio/102_A2_In_Drips.wav                          6615130
audio/103_A3_Out_Night.wav                         6649946
audio/104_A4_Weird1.wav                            6475866
audio/105_A5_Battle.wav                            5185626
audio/106_A6_Out_Day.wav                           6166618
audio/107_A7_Train+.wav                            6494298
audio/108_A8_Coastal.wav                           6549594
audio/109_Lyre_01.wav                              313434
audio/110_Lyre_02.wav                              477274
audio/111_charmer.wav                              405594
audio/112_Gods_part_iv.wav                         645210
binkw32.dll                                        178176
data/
DATA.TAG                                           120
data/alexhub.tr4                                   4007946
data/alexhub2.tr4                                  4735043
data/ang_race.tr4                                  4343019
data/angkor1.tr4                                   4034313
data/bikebit.tr4                                   5013974
data/citnew.tr4                                    4776907
data/cortyard.tr4                                  4989001
data/csplit1.tr4                                   4570232
data/csplit2.tr4                                   4838007
data/cutseq.pak                                    724500
data/frlogo.pak                                    46516
data/grlogo.pak                                    40931
data/hall.tr4                                      4882065
data/highstrt.tr4                                  4725022
data/jeepchas.tr4                                  3600478
data/jeepchs2.tr4                                  4826055
data/joby1a.tr4                                    5011064
data/joby1b.tr4                                    4544163
data/joby2.tr4                                     4839409
data/joby3a.tr4                                    4433722
data/joby3b.tr4                                    5141026
data/joby4a.tr4                                    4786641
data/joby4b.tr4                                    4401690
data/joby4c.tr4                                    4999677
data/joby5a.tr4                                    3741579
data/joby5b.tr4                                    4623726
data/joby5c.tr4                                    4398142
data/karnak1.tr4                                   4773596
data/lake.tr4                                      5021843
data/libend.tr4                                    3240517
data/library.tr4                                   4606099
data/lowstrt.tr4                                   3970465
data/nutrench.tr4                                  4260336
data/palaces.tr4                                   4549992
data/palaces2.tr4                                  4779709
data/semer.tr4                                     4409367
data/semer2.tr4                                    4294398
data/settomb1.tr4                                  3715110
data/settomb2.tr4                                  3868566
data/title.tr4                                     3007155
data/train.tr4                                     3246177
data/uklogo.pak                                    72384
data/uslogo.pak                                    56602
English.dat                                        6544
fmv/
fmv/Fmv00.bik                                      7384620
fmv/Fmv01.bik                                      34871480
fmv/Fmv02.bik                                      46432836
fmv/Fmv03.bik                                      22439508
fmv/Fmv04.bik                                      35027876
fmv/Fmv05.bik                                      28791116
fmv/Fmv06.bik                                      18592112
fmv/Fmv07.bik                                      29229364
fmv/Fmv08.bik                                      29277516
fmv/Fmv09.bik                                      25916860
fmv/Fmv10.bik                                      20838168
fmv/Fmv11.bik                                      20292736
fmv/Fmv12.bik                                      4028988
readme.txt                                         9483
SCRIPT.DAT                                         6089
tomb4.exe                                          790528
```

## Tomb Raider V: Chronicles

- Steam install: `<steam-library>/steamapps/common/Tomb Raider (V) Chronicles/`.
- `data/` contains 16 `.TRC` level-format files (73,511,948 bytes),
  including `Title.trc` (4,556,626 bytes) and the cutscene-named map
  `richcut2.trc` (2,490,858 bytes). `data/cutseq.bin` is the 3,299,328-byte
  in-engine cutscene pack.
- `SCRIPT.DAT` is the 1,612-byte script; `SINGLE.DAT` (237 bytes) and
  language `.DAT` files are also at the install root. There is no separate
  `MAIN.SFX`; the PC level format carries samples in the levels.
- Music: 136 loose `.wav` files under `audio/` (242,360,272 bytes).
  Video: nine Bink `.bik` files under `movie/` (205,186,176 bytes).
  The five additional `.str` screen assets under `screens/` total
  53,452,800 bytes and are counted separately from the Bink movies.
- This is a loose Windows PC install, not an emulated disc image. All V
  files were left on `<remote-host>`; no V cook inputs were copied.

### Installed tree

All paths below are relative to the Steam install; file sizes are bytes.

```text
audio/
audio/xa10_echoir01.wav                            1103962
audio/xa10_perc01.wav                              1120346
audio/xa10_spooky04.wav                            1300570
audio/xa10_title2.wav                              1267802
audio/xa10_title3.wav                              1108058
audio/xa10_vc01.wav                                1136730
audio/xa10_z_09.wav                                1314906
audio/xa10_z_10.wav                                1405018
audio/xa11_andy3.wav                               1439834
audio/xa11_andy4.wav                               1572954
audio/xa11_flyby1.wav                              1484890
audio/xa11_flyby2.wav                              1597530
audio/xa11_flyby3.wav                              1597530
audio/xa11_monk_2.wav                              1525850
audio/xa11_title1.wav                              1460314
audio/xa11_vc01atv.wav                             1417306
audio/xa12_andy4b.wav                              1736794
audio/xa12_andy5.wav                               1867866
audio/xa12_flyby4.wav                              1775706
audio/xa12_moses01.wav                             1693786
audio/xa12_richcut1.wav                            1796186
audio/xa12_z_01.wav                                2087002
audio/xa12_z_05.wav                                2052186
audio/xa12_z_10.wav                                1757274
audio/xa13_Andrea3.wav                             2320474
audio/xa13_Andrea3B.wav                            2154586
audio/xa13_Andy6.wav                               2265178
audio/xa13_Andy7.wav                               2144346
audio/xa13_cossack.wav                             2170970
audio/xa13_Joby3.wav                               2117722
audio/xa13_Joby7.wav                               2357338
audio/xa13_Z_07.wav                                2209882
audio/xa14_andrea1.wav                             3147866
audio/xa14_andy10.wav                              2680922
audio/xa14_andy8.wav                               3162202
audio/xa14_ecredits.wav                            3237978
audio/xa14_joby1.wav                               2664538
audio/xa14_joby6.wav                               3186778
audio/xa14_richcut2.wav                            2979930
audio/xa14_uwater1.wav                             2590810
audio/xa15_a_andy.wav                              4386906
audio/xa15_a_rome.wav                              4386906
audio/xa15_andy2.wav                               4485210
audio/xa15_boss_01.wav                             3280986
audio/xa15_joby2.wav                               3291226
audio/xa15_joby4.wav                               3420250
audio/xa15_joby5.wav                               3604570
audio/xa15_joby9.wav                               3817562
audio/xa16_A_Andy_Out_Norm.wav                     5353562
audio/xa16_A_Andy_Out_Spooky.wav                   5353562
audio/xa16_A_Harbour_out.wav                       5285978
audio/xa16_A_Rome_Day.wav                          5361754
audio/xa16_A_Sub_Amb.wav                           5154906
audio/xa16_A_Underwater.wav                        5367898
audio/xa16_Joby10.wav                              5154906
audio/xa16_Joby8.wav                               4788314
audio/xa17_A_Industry.wav                          5812314
audio/xa17_A_Rome_Night.wav                        5439578
audio/xa17_A_VC_Saga.wav                           5773402
audio/xa17_Andrea2.wav                             5961818
audio/xa17_Andrea4.wav                             6170714
audio/xa17_Andy1.wav                               5963866
audio/xa17_Andy11.wav                              8919130
audio/xa17_Andy9.wav                               7290970
audio/xa1_AndyPew.wav                              299098
audio/xa1_Secret.wav                               303194
audio/xa1_TL_02.wav                                309338
audio/xa1_TL_05.wav                                229466
audio/xa1_TL_08.wav                                264282
audio/xa1_TL_10b.wav                               213082
audio/xa1_TL_11.wav                                278618
audio/xa1_Z_10.wav                                 227418
audio/xa2_ATTACK04.wav                             333914
audio/xa2_HMMM02.WAV                               354394
audio/xa2_HMMM05.WAV                               309338
audio/xa2_SPOOKY2A.WAV                             344154
audio/xa2_TL_01.wav                                321626
audio/xa2_tl_10a.wav                               350298
audio/xa2_TOMS01.wav                               356442
audio/xa2_UWATER2B.wav                             338010
audio/xa3_ATTACK01.WAV                             374874
audio/xa3_ATTACK02.WAV                             364634
audio/xa3_ATTACK03.WAV                             358490
audio/xa3_HMMM01.WAV                               368730
audio/xa3_stealth1.wav                             368730
audio/xa3_stealth2.wav                             370778
audio/xa3_TL_03.wav                                397402
audio/xa3_tl_06.wav                                395354
audio/xa4_drama01.wav                              446554
audio/xa4_hit01.wav                                430170
audio/xa4_hmmm06.wav                               409690
audio/xa4_mil01.wav                                417882
audio/xa4_mil05.wav                                489562
audio/xa4_spooky05.wav                             440410
audio/xa4_stealth4.wav                             489562
audio/xa4_Z_03.wav                                 421978
audio/xa5_HMMM03.WAV                               518234
audio/xa5_HMMM04.WAV                               495706
audio/xa5_MIL02.WAV                                520282
audio/xa5_MIL02A.WAV                               516186
audio/xa5_MIL06.WAV                                499802
audio/xa5_SPOOKY02.WAV                             503898
audio/xa5_TL_04.WAV                                534618
audio/xa5_TL_12.WAV                                503898
audio/xa6_AuthSolo.wav                             630874
audio/xa6_Mil04.wav                                553050
audio/xa6_Solo01.wav                               555098
audio/xa6_Spooky03.wav                             655450
audio/xa6_Stealth3.wav                             591962
audio/xa6_Z12.wav                                  581722
audio/xa6_Z13.wav                                  661594
audio/xa6_Z_04anim.wav                             663642
audio/xa7_andyooer.wav                             845914
audio/xa7_andyoooh.wav                             694362
audio/xa7_authtr.wav                               747610
audio/xa7_evibes01.wav                             729178
audio/xa7_tl_07.wav                                716890
audio/xa7_z_02.wav                                 718938
audio/xa7_z_06.wav                                 782426
audio/xa7_z_06a.wav                                669786
audio/xa8_fightsc.wav                              786522
audio/xa8_jobyalrm.wav                             837722
audio/xa8_mil02b.wav                               858202
audio/xa8_mil03.wav                                786522
audio/xa8_richcut3.wav                             792666
audio/xa8_uwater2a.wav                             825434
audio/xa8_z_08.wav                                 809050
audio/xa8_z_13.wav                                 796762
audio/xa9_chopin01.wav                             1077338
audio/xa9_evibes02.wav                             907354
audio/xa9_gods01.wav                               909402
audio/xa9_richcut4.wav                             979034
audio/xa9_spooky01.wav                             1058906
audio/xa9_swampy.wav                               862298
audio/xa9_title4.wav                               991322
audio/xa9_z_03.wav                                 974938
binkw32.dll                                        291840
cfgmgr32.dll                                       41984
data/
DATA.TAG                                           109
data/Andrea1.trc                                   5723173
data/Andrea2.trc                                   5579092
data/Andrea3.trc                                   5412895
data/andy1.trc                                     5107477
data/Andy2.trc                                     5721256
data/andy3.trc                                     5169937
data/cutseq.bin                                    3299328
data/del.trc                                       2298974
data/joby2.trc                                     5406527
data/joby3.trc                                     4371827
data/joby4.trc                                     3082056
data/joby5.trc                                     4480239
data/LOGO512.pak                                   74581
data/rich1.trc                                     4716621
data/rich2.trc                                     4805655
data/rich3.trc                                     4588735
data/richcut2.trc                                  2490858
data/Title.trc                                     4556626
data/uklogo.pak                                    72384
data/uslogo.pak                                    56602
DUTCH.DAT                                          6544
ENGLISH.DAT                                        5265
FRENCH.DAT                                         5982
GERMAN.DAT                                         5766
ITALIAN.DAT                                        5265
JAPAN.DAT                                          4589
movie/
movie/Fmv0.bik                                     7384620
movie/FMV1.bik                                     17939132
movie/FMV2.bik                                     37782232
movie/fmv3.bik                                     30778252
movie/fmv4.bik                                     18919968
movie/fmv5.bik                                     16583860
movie/fmv6.bik                                     31438532
movie/fmv7.bik                                     27895492
movie/fmv8.bik                                     16464088
PCTomb5.exe                                        1282096
readme.txt                                         17649
screens/
screens/gallery.str                                14745600
screens/nxg.str                                    9830400
screens/screens.str                                12902400
screens/story1.str                                 7987200
screens/story2.str                                 7987200
SCRIPT.DAT                                         1612
SINGLE.DAT                                         237
SPANISH.DAT                                        6681
US.DAT                                             5265
```

## Tomb Raider I comparison

- Source on `<remote-host>`:
  `<steam-library>/steamapps/common/Tomb Raider (I)/`.
  `appmanifest_224960.acf` reports build `4774071`, state `4` (installed),
  and 318,059,021 bytes on disk.
- This is the DOSBox-wrapped PC release. `GAME.DAT` is a 590-byte track
  descriptor, not `TOMBPC.DAT`. It lists the data track `GAME.GOG`
  (raw MODE1/2352, 282,319,968 bytes) and nine external MP3 audio tracks
  (`02.mp3`–`10.mp3`, 17,614,848 bytes). The disc directory was listed
  without extracting the image or reading any executable contents.
- The image's `DATA/` contains 21 `.PHD` files (50,733,276 bytes):
  15 `LEVEL*` files plus training `GYM.PHD` (48,070,970 bytes together),
  four in-engine `CUT*.PHD` files (2,345,846 bytes), and `TITLE.PHD`
  (316,460 bytes). It has no `TOMBPC.DAT` or `MAIN.SFX`.
- The image's `FMV/` contains 11 `.RPL` clips (124,750,330 bytes).
  Media paths and sizes are listed below. Neither the image nor the
  external tracks were copied.
- The build host already has `$TR64_DATA/pc/` (22 `.phd`
  files, 53,078,030 bytes). That count includes `snowcut.phd`
  (2,344,754 bytes), which is not in the installed Steam image's `DATA/`
  listing. The existing PC inputs were not changed or re-copied.

### Installed tree, through two levels

Names and logical file sizes in bytes, relative to the Steam install root.
Directories at the depth limit are shown with a trailing `/`.

```text
02.mp3                              3250176
03.mp3                              2027520
04.mp3                              3037184
05.mp3                              3125248
06.mp3                              3108864
07.mp3                              1122304
08.mp3                              1028096
09.mp3                               350208
10.mp3                               565248
3dfxSpl2.dll                        1105408
DOCS/
  AUTHORS.txt                           274
  COPYING.txt                         18332
  ChangeLog.txt                        4424
  ChangeLog_SVN.txt                   22336
  INSTALL.txt                          3937
  Official manuals/
  SVN-specific manuals/
  THANKS.txt                           1000
  VERSION.txt                             5
FONTS/
  Courier.ttf                        292288
  OCRA.ttf                            23952
  Roman.ttf                          105340
  Script.ttf                          71116
  license.txt                           281
  sansserif.ttf                      594132
GAME.DAT                                590
GAME.GOG                          282319968
SDL.dll                              364558
SDL_net.dll                           17920
SHADERS/
  2xSaI.fx                            20902
  2xSaI_sRGB.fx                       20935
  2xSaL.fx                             2626
  2xSaL2xAA.fx                        3685
  2xSaLAA.fx                          3636
  2xSaL_Ls.fx                         3250
  4xSaL.fx                            3029
  4xSoft.fx                           2993
  4xSoft_PS3.0.fx                     3052
  AdvancedAA.fx                       2874
  Cartoon.fx                          2586
  ColorSketch.fx                      2597
  GS2x.fx                             2418
  GS2xFilter.fx                       3710
  GS2xSuper.fx                        2423
  GS2xTwo.fx                          2405
  GS4x.fx                             3051
  GS4xColorScale.fx                   4742
  GS4xFilter.fx                       3628
  GS4xHqFilter.fx                     4507
  GS4xScale.fx                        3116
  GS4xSoft.fx                         3069
  Gs2xLS.fx                           2863
  Gs2xSmartFilter.fx                  5005
  HQ2x.fx                             7963
  Lanczos.fx                          3687
  Lanczos12.fx                        3391
  Lanczos16.fx                        3653
  MCAmber.fx                          1955
  MCGreen.fx                          1957
  MCHerc.fx                           1637
  MCOrange.fx                         1954
  Matrix.fx                           2192
  Scale2xPlus.fx                      1677
  Scale4x.fx                          3021
  SimpleAA.fx                         2083
  Sketch.fx                           2241
  Super2xSaI.fx                       4914
  SuperEagle.fx                       5270
  Tv.fx                               2174
  bilinear.fx                         2096
  gpl.txt                            18326
  none.fx                                0
  point.fx                            2094
  scale2x.fx                          4124
  scale2x_ps14.fx                     6813
  scaling.inc                        4207
  shader.code                        3569
TOMBENG/
TOMBPATH.TXT                             11
TOMBRAID/
  DOS4GW.EXE                         265396
  HMIDET.386                          83774
  HMIDRV.386                         317317
  HMISET.CFG                            220
  SETTINGS.DAT                           46
  SETUP.EXE                          214281
  TOMB.SP                             11888
  TOMBno3Dfx.EXE                     768611
  tomb.exe                           873739
TOOLS/
  Install movie codec.bat             2109
  Read manuals.bat                       85
  Reset DOSBox configuration.bat      377
  Run DOSBox configuration.bat        1951
  Run DOSBox with WINDIB driver.bat    122
  Run DOSBox without GUI.bat           108
  Run save states manager.bat         3784
  config_template/
  zmbv/
d3dx9.dll                          2414360
dosbox.conf                          19454
dosbox.exe                         6829056
dosboxTR.conf                        19314
dosboxTRWindowed.conf                19315
glide.ini                              630
glide.log                           229206
glide2x.dll                         349710
glide2x.ovl                          31425
glide2x_emu.ovl                     348791
gogwrap.exe                         273419
installscript.vdf                      323
libfluidsynth-1.dll                  318464
libglib-2.0-0.dll                   1071635
libgthread-2.0-0.dll                  21523
libphysfs.dll                        160270
stderr.txt                                0
```

### Game files inside `GAME.GOG`

```text
DATA/CRED1.PCX        106587
DATA/CRED2.PCX        196985
DATA/CRED3.PCX        174908
DATA/CUT1.PHD         599840
DATA/CUT2.PHD         354320
DATA/CUT3.PHD         512104
DATA/CUT4.PHD         879582
DATA/EIDOSPC.PCX       22700
DATA/END.PCX          102512
DATA/GYM.PHD         3237128
DATA/INSTALL.PCX      168100
DATA/LEVEL1.PHD      2533634
DATA/LEVEL2.PHD      2873450
DATA/LEVEL3A.PHD     2934730
DATA/LEVEL3B.PHD     2738258
DATA/LEVEL4.PHD      3030872
DATA/LEVEL5.PHD      2718540
DATA/LEVEL6.PHD      3074376
DATA/LEVEL7A.PHD     2817612
DATA/LEVEL7B.PHD     3389096
DATA/LEVEL8A.PHD     2880564
DATA/LEVEL8B.PHD     2886756
DATA/LEVEL8C.PHD     3105450
DATA/LEVEL10A.PHD    3224138
DATA/LEVEL10B.PHD    3094342
DATA/LEVEL10C.PHD    3532024
DATA/TITLE.PHD        316460
DATA/TITLEH.PCX       115980
FMV/CAFE.RPL        15995851
FMV/CANYON.RPL      32413718
FMV/CORE.RPL         1877283
FMV/END.RPL         10158823
FMV/ESCAPE.RPL       1062180
FMV/LIFT.RPL        17919663
FMV/MANSION.RPL      2895565
FMV/PRISON.RPL      13764300
FMV/PYRAMID.RPL      1330206
FMV/SNOW.RPL        16352839
FMV/VISION.RPL      10979902
```

## PlayStation inputs

The classic Steam apps provide PC builds, not PlayStation disc images or
PlayStation level sets. tr64's Tomb Raider I PlayStation inputs already live in
`$TR64_DATA/psx/`, with levels under `PSXDATA/`. For II and III,
use the owner's original PlayStation releases and make verified disc images
with the region/revision identified and the original track layout retained.
Inventory the console levels and other game data from their disc paths;
retain CD audio and FMV sources separately if needed. Those inputs require
edition-specific format/cook support: the PC `.TR2` data and its Steam depot
are not substitutes for PlayStation inputs.
