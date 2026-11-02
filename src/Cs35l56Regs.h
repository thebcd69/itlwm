// CS35L56 register map / constants.
// Ported verbatim from Linux include/sound/cs35l56.h (torvalds/linux master,
// fetched 2026-11-02) unless noted. Values must stay identical to upstream.
#pragma once

// --- Control port registers (include/sound/cs35l56.h) ---
#define CS35L56_DEVID                     0x0000000
#define CS35L56_REVID                     0x0000004
#define CS35L56_RELID                     0x000000C
#define CS35L56_OTPID                     0x0000010
#define CS35L56_ASP1_ENABLES1             0x0004800
#define CS35L56_ASP1_CONTROL1             0x0004804
#define CS35L56_ASP1_CONTROL2             0x0004808
#define CS35L56_ASP1_CONTROL3             0x000480C
#define CS35L56_ASP1_FRAME_CONTROL1       0x0004810
#define CS35L56_ASP1_FRAME_CONTROL5       0x0004820
#define CS35L56_ASP1_DATA_CONTROL1        0x0004830
#define CS35L56_ASP1_DATA_CONTROL5        0x0004840
#define CS35L56_DACPCM1_INPUT             0x0004C00
#define CS35L56_DACPCM2_INPUT             0x0004C08
#define CS35L56_ASP1TX1_INPUT             0x0004C20
#define CS35L56_ASP1TX2_INPUT             0x0004C24
#define CS35L56_ASP1TX3_INPUT             0x0004C28
#define CS35L56_ASP1TX4_INPUT             0x0004C2C
#define CS35L56_DSP1RX1_INPUT             0x0004C40
#define CS35L56_DSP1RX2_INPUT             0x0004C44
#define CS35L56_SWIRE_DP3_CH1_INPUT       0x0004C70
#define CS35L56_SWIRE_DP3_CH2_INPUT       0x0004C74
#define CS35L56_SWIRE_DP3_CH3_INPUT       0x0004C78
#define CS35L56_SWIRE_DP3_CH4_INPUT       0x0004C7C
#define CS35L56_IRQ1_CFG                  0x000E000
#define CS35L56_IRQ1_STATUS               0x000E004
#define CS35L56_IRQ1_EINT_1               0x000E010
#define CS35L56_IRQ1_EINT_2               0x000E014
#define CS35L56_IRQ1_EINT_4               0x000E01C
#define CS35L56_IRQ1_EINT_8               0x000E02C
#define CS35L56_IRQ1_EINT_18              0x000E054
#define CS35L56_IRQ1_EINT_20              0x000E05C
#define CS35L56_IRQ1_MASK_1               0x000E090
#define CS35L56_IRQ1_MASK_2               0x000E094
#define CS35L56_IRQ1_MASK_4               0x000E09C
#define CS35L56_IRQ1_MASK_8               0x000E0AC
#define CS35L56_IRQ1_MASK_18              0x000E0D4
#define CS35L56_IRQ1_MASK_20              0x000E0DC
#define CS35L56_DSP_MBOX_1_RAW            0x0011000
#define CS35L56_DSP_VIRTUAL1_MBOX_1       0x0011020
#define CS35L56_DSP_VIRTUAL1_MBOX_2       0x0011024
#define CS35L56_DSP_RESTRICT_STS1         0x00190F0
#define CS35L56_OTP_MEM_53                0x00300D4

// --- DSP memory (include/sound/cs35l56.h) ---
#define CS35L56_DSP1_XMEM_PACKED_0        0x2000000
#define CS35L56_DSP1_XMEM_UNPACKED24_0    0x2800000
#define CS35L56_DSP1_FW_VER               0x2800010
#define CS35L56_DSP1_HALO_STATE           0x28021E0   // rev B0
#define CS35L56_B2_DSP1_HALO_STATE        0x2803D20   // rev B2+
#define CS35L56_DSP1_PM_CUR_STATE         0x2804308   // rev B0
#define CS35L56_B2_DSP1_PM_CUR_STATE      0x2804678   // rev B2+
#define CS35L56_DSP1_CORE_BASE            0x2B80000
#define CS35L56_DSP1_YMEM_PACKED_0        0x2C00000
#define CS35L56_DSP1_YMEM_UNPACKED24_0    0x3400000
#define CS35L56_MAIN_RENDER_USER_MUTE     0x3400024
#define CS35L56_MAIN_RENDER_USER_VOLUME   0x340002C
#define CS35L56_MAIN_POSTURE_NUMBER       0x3400094
#define CS35L56_PROTECTION_STATUS         0x34000D8
#define CS35L56_TRANSDUCER_ACTUAL_PS      0x3400150
#define CS35L56_DSP1_PMEM_0               0x3800000
#define CS35L56_DSP1_PMEM_5114            0x3804FE8   // regmap max_register

// --- Field masks ---
#define CS35L56_DEVID_MASK                0x00FFFFFF
#define CS35L56_AREVID_MASK               0x000000F0
#define CS35L56_MTLREVID_MASK             0x0000000F
#define CS35L56_REVID_B0                  0x000000B0
#define CS35L56_ASP_RX2_EN_SHIFT          17
#define CS35L56_ASP_RX1_EN_SHIFT          16
#define CS35L56_AMP_SHORT_ERR_EINT1_MASK  0x80000000
#define CS35L56_OTP_BOOT_DONE_MASK        0x00000002
#define CS35L56_TEMP_ERR_EINT1_MASK       0x80000000
#define CS35L56_RESTRICTED_MASK           0x7
#define CS35L56_MAIN_RENDER_USER_MUTE_MASK      1
#define CS35L56_MAIN_RENDER_USER_VOLUME_MIN     (-400)   // 0.25 dB steps => -100 dB
#define CS35L56_MAIN_RENDER_USER_VOLUME_MAX     48       // +12 dB (we never go above 0 dB)
#define CS35L56_MAIN_RENDER_USER_VOLUME_MASK    0x0000FFC0
#define CS35L56_MAIN_RENDER_USER_VOLUME_SHIFT   6
#define CS35L56_FIRMWARE_MISSING          (1u << 0)
#define CS35L56_HALO_STATE_SHUTDOWN       1
#define CS35L56_HALO_STATE_BOOT_DONE      2
#define CS35L56_PS0                       0
#define CS35L56_PS3                       3

// --- Mailbox commands ---
#define CS35L56_MBOX_CMD_PING                    0x0A000000
#define CS35L56_MBOX_CMD_AUDIO_PLAY              0x0B000001
#define CS35L56_MBOX_CMD_AUDIO_PAUSE             0x0B000002
#define CS35L56_MBOX_CMD_AUDIO_REINIT            0x0B000003
#define CS35L56_MBOX_CMD_HIBERNATE_NOW           0x02000001
#define CS35L56_MBOX_CMD_WAKEUP                  0x02000002
#define CS35L56_MBOX_CMD_PREVENT_AUTO_HIBERNATE  0x02000003
#define CS35L56_MBOX_CMD_ALLOW_AUTO_HIBERNATE    0x02000004
#define CS35L56_MBOX_CMD_SHUTDOWN                0x02000005
#define CS35L56_MBOX_CMD_SYSTEM_RESET            0x02000007

// --- Timings (us) ---
#define CS35L56_MBOX_TIMEOUT_US           5000
#define CS35L56_MBOX_POLL_US              250
#define CS35L56_PS0_POLL_US               500
#define CS35L56_PS0_TIMEOUT_US            50000
#define CS35L56_PS3_POLL_US               500
#define CS35L56_PS3_TIMEOUT_US            300000
#define CS35L56_CONTROL_PORT_READY_US     2200
#define CS35L56_HALO_STATE_POLL_US        1000
#define CS35L56_HALO_STATE_TIMEOUT_US     250000
#define CS35L56_RESET_PULSE_MIN_US        1100
#define CS35L56_WAKE_HOLD_TIME_US         1000
#define CS35L56_SPI_RESET_TO_PORT_READY_US (CS35L56_CONTROL_PORT_READY_US + 2500)

// --- HALO DSP (drivers/firmware/cirrus/cs_dsp.c) ---
#define HALO_SCRATCH1                     0x005c0
#define HALO_CCM_CORE_CONTROL             0x41000

// --- wmfw / WMDR (include/linux/firmware/cirrus/wmfw.h) ---
#define WMFW_HALO                 4
#define WMFW_ABSOLUTE             0xf0
#define WMFW_ALGORITHM_DATA       0xf2
#define WMFW_METADATA             0xfc
#define WMFW_NAME_TEXT            0xfe
#define WMFW_INFO_TEXT            0xff
#define WMFW_ADSP2_XM             5
#define WMFW_ADSP2_YM             6
#define WMFW_HALO_PM_PACKED       0x10
#define WMFW_HALO_XM_PACKED       0x11
#define WMFW_HALO_YM_PACKED       0x12
#define WMFW_ADSP2_XM_LONG        0xf405
#define WMFW_ADSP2_YM_LONG        0xf406
#define WMFW_HALO_XM_PACKED_LONG  0xf411
#define WMFW_HALO_YM_PACKED_LONG  0xf412
