# Önerilen AppleALC layout (ALC285, GU605M) — tasarım, henüz derlenmedi

Layout ID önerisi: 63 (boş). Dosyalar: `Resources/ALC285/layout63.xml`, `Platforms63.xml`, Info.plist girdileri,
`PinConfigs.kext` girdisi.

## PinConfig (ConfigData, verb biçimi `0nn71cXX 0nn71dXX 0nn71eXX 0nn71fXX`)
| NID | Default | Kaynak |
|---|---|---|
| 0x12 | 0x90a60110 (Internal Mic, dijital) | layout 21 ile aynı |
| 0x14 | 0x90170130 (Speaker) + `01470c02` EAPD | layout 21 |
| 0x17 | 0x90170131 (Speaker, aynı association 3) | GA403W Windows dökümü + Linux GU605 fixup |
| 0x19 | 0x03a11050 (Headset Mic) | Linux ALC285_FIXUP_ASUS_GU605_SPI_2_HEADSET_MIC |
| 0x1b | 0x03a11c30 | Linux (aynı fixup) |
| 0x21 | 0x03211020 (HP) + `02170c02` | layout 21 |

## PathMap
- Output Speaker: `[0x14 → 0x02]` ve `[0x17 → 0x02]` (aynı DAC; Linux `snd_hda_override_conn_list(0x17, {0x02})`)
- Output Headphone: `[0x21 → 0x03]`
- Input Mic: `[0x08 → 0x23 → 0x12]`, Headset: `[0x07 → 0x24 → 0x19]`

AppleHDA tek DAC'ı iki pin'e paylaştırmayı PathMap'te iki ayrı alt yol olarak kabul ediyor mu: layout 33 iki
farklı DAC kullanıyor; aynı DAC için örnek bulunmalı (ALC285 layout 31/71 incelenecek). Doğrulanmadı.
