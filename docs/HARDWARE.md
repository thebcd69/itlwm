# GU605MY donanım haritası (DSDT.aml'den çıkarıldı)

Kaynak: `EFI/OC/ACPI/DSDT.aml` (Windows'tan alınmış), `iasl -d` 20230628 ile açıldı.

## Amfi ACPI cihazı
`\_SB.PC00.SPI0.SPK1`, `_HID "CSC3556"`, `_SUB "10431C63"`, `_UID 1`, `_STA 0x0F`.
Tek ACPI düğümü iki amfiyi (2x SpiSerialBusV2) tanımlar → Linux'ta serial-multi-instantiate.

| _CRS idx | Kaynak | Değer |
|---|---|---|
| 0 | SpiSerialBusV2 | CS 0, 4 MHz (0x3D0900), mode 0 (CPOL low, CPHA first), 8 bit, 4-wire, CS active-low, `\_SB.PC00.SPI0` → **AMP1** |
| 1 | SpiSerialBusV2 | CS 1, aynı ayarlar → **AMP2** |
| GpioIo #0 | Output, PullUp | pin 0x0082 → **GPP_E_2** (SPI0 CS1 GPIO chip-select, bkz. `cs-gpios`) |
| GpioIo #1 | Output, PullDown | pin 0x0181 → **GPP_D_1** (`reset-gpios`, iki amfi ortak) |
| GpioIo #2 | Input, PullUp | pin 0x0168 → **GPP_B_8** (`spk-id-gpios`, iki amfi ortak) |
| GpioIo #3 | Input, PullUp, debounce 0x64 | pin 0x0182 → **GPP_D_2** |
| GpioInt | Edge, ActiveBoth, Shared, PullUp | pin 0x0182 → **GPP_D_2** (amfi IRQ, ortak) |

`_DSD`:
- `cirrus,dev-index` = {0, 1} → CS0 = index 0 = "AMP1", CS1 = index 1 = "AMP2"
- `cirrus,boost-type` = {0, 0} (internal boost; HDA sürücüsü bu özelliği okumuyor)
- `reset-gpios` = {SPK1, 1, 0, 0} x2 → GpioIo #1, active-high (ACPI) → Linux mantıksal 1 = fiziksel HIGH = reset bırakılmış
- `spk-id-gpios` = {SPK1, 2, 0, 0} x2 → GpioIo #2 (GPP_B_8) okunur → `spkid0` / `spkid1`

`\_SB.PC00.SPI0._DSD`: `cs-gpios` = {0, SPK1, 0, 0, 0} → CS0 = denetleyicinin native CS0'ı,
CS1 = SPK1 GpioIo #0 = **GPP_E_2 (GPIO ile sürülen chip-select, active-low)**.

## SPI denetleyicisi
`\_SB.PC00.SPI0` `_ADR 0x001E0002` → PCI 00:1e.2, 8086:7E27 (Meteor Lake-P LPSS GSPI0), subsys 1043:1C63.
macOS ioreg: `SPI0@1E,2` IOPCIDevice (sürücüsüz). `_PS0` boş, `_PS3` SOD3.

## GPIO denetleyicisi
`\_SB.GPI0` `_HID` = INTC1083 (MTL-P; PCHS!=4, ARLM!=1 varsayımı). `_CRS` MMIO: SBRG+0xD10000 (com0),
+0xD20000 (com1), +0xD30000 (com3), +0xD40000 (com4), +0xD50000 (com5), her biri 64 KiB.
SBRG GNVS'den gelir → çalışma anında GPI0'ın IODeviceMemory'sinden okunmalı (kext böyle yapıyor).

BIOS GPCM tablosu ile Linux `pinctrl-meteorlake.c` (mtlp_communities) birebir uyumlu:

| ACPI pin | Grup (gpio_base) | Pad | Community (MMIO idx) | PADCFG DW0 ofseti |
|---|---|---|---|---|
| 0x082 | GPP_E (0x80) | GPP_E_2 | com1 (memidx 1) | 0x790 + 2*0x10 = **0x7B0** |
| 0x181 | GPP_D (0x180) | GPP_D_1 | com5 (memidx 4) | 0x790 + 1*0x10 = **0x7A0** |
| 0x168 | GPP_B (0x160) | GPP_B_8 | com5 (memidx 4) | 0x600 + 8*0x10 = **0x680** |
| 0x182 | GPP_D (0x180) | GPP_D_2 | com5 (memidx 4) | 0x790 + 2*0x10 = **0x7B0** |

MTL-P: PAD_OWN 0x0B0, PADCFGLOCK 0x110, HOSTSW_OWN 0x140, GPI_IS 0x200, GPI_IE 0x210; pad başına 16 bayt (4 DW).
PADCFG DW0: bit0 GPIOTXSTATE, bit1 GPIORXSTATE, bit8 GPIOTXDIS, bit9 GPIORXDIS, bit10-13 PMODE (0=GPIO).

## HDA
HDEF 00:1f.3 8086:7E28, codec ALC285 (10EC0285) subsys 10431C63.
