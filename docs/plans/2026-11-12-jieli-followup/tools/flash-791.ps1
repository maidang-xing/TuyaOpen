# Flash one of the archived AC791 (WL82) images with the vendor isd_download tool.
#
#   .\flash-791.ps1 -Image dvp|dvp640|spi|touch [-Wait]
#
# Images live in ..\images and are copied over the staging tools\app.bin only
# after their SHA256 matches the recorded value, so the flash never depends on
# whatever the last build left behind. The board must expose either
# "WL82 UDISK" (running app, no key press) or "WL82 UBOOT1.00" (UPDATE+RESET).
# Logs land in ..\logs\flash-791-<image>-<timestamp>.log.

param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('dvp', 'dvp640', 'dvphead', 'spi', 'spiprev', 'spiprev80', 'spidiag', 'spiresync', 'spivendor', 'spistream', 'spisimple', 'spimute', 'spitiming', 'spilut', 'spifast', 'spiwait', 'spiframe', 'spifinal', 'spiband', 'spistitch', 'spicarry', 'spidup', 'spitriple', 'panelbands', 'spifreeze', 'spite', 'spitimeout', 'spidisptime', 'spirobust', 'spiqvga', 'spiqvgadbg', 'spiqvgaout', 'spiqvgadec', 'spivga4fps', 'spivga4fpstidy', 'spivga4fpstidy2', 'spifalsehead', 'spipadstable', 'spi48m', 'spilineirq', 'spilineirqdma', 'spiupstream', 'spiupstream3fb', 'spitddsensor', 'talprobe', 'audiovad', 'audiokws', 'taltokens', 'touch', 'spitddd', 'dvptdd', 'dvp2', 'dvpdiag', 'dvpbisect', 'dvpfix', 'dvpclear', 'dvpstage', 'dvpprobe', 'dvpdemo', 'dvpdecouple', 'dvpnodisp', 'dvpstatic', 'dvpfinal', 'dvpfill', 'spiyuv', 'spibase')]
    [string]$Image,

    # isd_download needs the app's USB device to be enumerated when it opens
    # it; a crashing or hung app shows up for a fraction of a second per boot.
    [int]$Attempts = 8,

    [switch]$Wait
)

$ErrorActionPreference = 'Stop'

$root = Split-Path $PSScriptRoot -Parent

$images = @{
    dvp = @{
        File = 'dvp-320x240.bin'
        Sha  = 'af87a582b978aee8b459f1b07cc0e6228b0cee4bd0de26d2eff568ff05c7043c'
        Dir  = 'D:\tuya_proj\TuyaOpen\.worktrees\05-camera\examples\peripherals\camera\output_display\.build\jieli-staging\build\cpu\wl82\tools'
        Note = 'DVP 320x240@25 live preview, conv timing, review fixes'
    }
    dvptdd = @{
        File = 'dvp-4fe36c41.bin'
        Sha  = '4fe36c410c63d0341efb16d9d49cc0c8ebbda044df8b75b7a9ca466c21963dde'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'output_display dvp_lcd profile with boot markers, checked against TDL camera path'
    }
    dvp2 = @{
        File = 'dvp-v2-4ddcf72f.bin'
        Sha  = '4ddcf72f47943a0de5539e41cb3383e0ffca2f42f0984c0377dc96e6f06006da'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'clean rebuild of TKL/TDD DVP v2 (platform a4aab851 / app 53ad1852), 422,604 B'
    }
    dvpdiag = @{
        File = 'dvp-v2-diag-a2a32bbf.bin'
        Sha  = 'a2a32bbfc09fbc826aa64fb8a36b0473bddbefa67d95de37d2e20d608c1654c3'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'DVP v2 diagnostic: prints SET_FMT/GET_OVERLAY fields, no cache op/convert/deliver'
    }
    dvpbisect = @{
        File = 'dvp-v2-bisect-43dfa09e.bin'
        Sha  = '43dfa09edc119b4e00c5e17f0b6e17852dca94585dca8a390ef498178b8568d7'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'DVP v2 bisect: frames 1-20 cache op, 21-40 +convert, 41+ +deliver; markers name the faulting stage'
    }
    dvpfix = @{
        File = 'dvp-v2-fix-a5fe2492.bin'
        Sha  = 'a5fe249269c77cac07651ff92eb4ed4f7e81e9d8beab42183673ce98e4fce2cc'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'DVP v2 + optnone conversion + layout probe + TE gpio_cfg zero-init'
    }
    dvpclear = @{
        File = 'dvp-v2-clearfirst-9fa2b577.bin'
        Sha  = '9fa2b5772a9aca7cf07a1595686de372cc610a84ab2016979083889a558e30c6'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'DVP v2: CLEAR_OVERLAY before reading, vendor-demo order; phases A/B/C markers'
    }
    dvpstage = @{
        File = 'dvp-v2-stage-a4e97b15.bin'
        Sha  = 'a4e97b153f7a25a8669d588d93d2866ef63e3bcf9de131fedd8017def45bbb77'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'DVP v2: memcpy frame to staging + CLEAR before converting (release vendor buffer early)'
    }
    dvpprobe = @{
        File = 'dvp-v2-probe-f9040c98.bin'
        Sha  = 'f9040c98b66a02225f35a0d97c3ceb85ae1614c9219251acd237eb140958b579'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'DVP v2 probe: P2 full-buffer read, P3 memcpy, P4 convert(stage), P5 stage+convert+deliver'
    }
    dvpdemo = @{
        File = 'dvp-v2-demo-ed732288.bin'
        Sha  = 'ed732288bbf64a010e00587f09755edc635d8684e8fd74af56d43bfcd480fbc5'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'DVP v2 vendor-demo overlay mode + volatile byte reads + aligned u32 stores + staged copy'
    }
    dvpdecouple = @{
        File = 'dvp-v2-decouple-a4b06822.bin'
        Sha  = 'a4b06822fce59435c799cfbddc189dd4b7a4251431defe9145b53d44145a7e06'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'DVP v2 decoupled (queue reset on start): capture task GET+stage-copy+CLEAR, cam_proc converts/delivers'
    }
    dvpnodisp = @{
        File = 'dvp-v2-nodisp-41664aaa.bin'
        Sha  = '41664aaa287b5c705ddb9637eb6be34adc592709bf1ce84af692fcfbb507c3c8'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'DVP v2 decoupled, display init skipped: isolates the camera path from the EMI/panel hang'
    }
    dvpstatic = @{
        File = 'dvp-v2-static-e07ebdb7.bin'
        Sha  = 'e07ebdb79fd2dca8dafa1523e66e210e31691e153139a22044fe2099fd8ba188'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'DVP v2 hybrid mode with a 153,600-byte 32-aligned BSS overlay buffer (vendor storage class)'
    }
    dvpfinal = @{
        File = 'dvp-v2-final-2af653d1.bin'
        Sha  = '2af653d1a07e7c8adf7c6aa9def980665a4925bf00cafc4a1d2e1323eb9245bb'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'DVP v2 final: static overlay buffer + decoupled capture/convert, periodic diagnostics removed'
    }
    dvpfill = @{
        File = 'dvp-v2-fill-ed1b448e.bin'
        Sha  = 'ed1b448ef767c17a703ae9bfc933437da6c6f17bc15fd4430351eea2e528bc38'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'DVP v2 final + 320x240 -> 320x480 row-doubled preview'
    }
    spiyuv = @{
        File = 'spi-yuvbi3-ac4f2c39.bin'
        Sha  = 'ac4f2c39b86c1af9f07fb7470799b6610538f719fb038b3b87a72b144bb9909b'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'SPI VGA colour bisect (platform 6562c41): YUYV/UYVY every 10 frames; page-1 AWB + page-0 PLL readback'
    }
    spibase = @{
        File = 'spi-base2-1444db37.bin'
        Sha  = '1444db37f6c410a4037edd88596950654f66209250f00c162f450635eb7c8f74'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'SPI VGA baseline: fixed GC0310 table, geometry guard, display TE fix, DVP statics out of the SPI build'
    }
    dvphead = @{
        File = 'dvp-head-68a633ea.bin'
        Sha  = '68a633ea49b27401331b57ed0f9ee3e2ec591f2b232268a041b305c9e8d37626'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'DVP profile rebuilt from PR heads 0f15617d/6c36e54f (411,433 B), never flashed'
    }
    dvp640 = @{
        File = 'dvp-640x480-10fps-fbe11b1c.bin'
        Sha  = 'fbe11b1cf96ce02ef0e07fe7c1b8bfc84ba218ff44eac798fed7963f1d267fd8'
        Dir  = 'D:\tuya_proj\TuyaOpen\.worktrees\05-camera\examples\peripherals\camera\output_display\.build\jieli-staging\build\cpu\wl82\tools'
        Note = 'DVP 640x480@10 fallback preview (full field of view)'
    }
    spi = @{
        File = 'spi-i2cscan.bin'
        Sha  = '97f3e2d97a93f4d6798cf235ada522ef818a9dda4abf0cad7ae9d24974f6942b'
        Dir  = 'D:\tuya_proj\TuyaOpen\.worktrees\05-camera\examples\peripherals\camera\jieli_probe\.build\jieli-staging\build\cpu\wl82\tools'
        Note = 'SPI camera I2C full scan, iic1=PH0/PH1 fix'
    }
    spiprev = @{
        File = 'spi-preview-8ab7fca0.bin'
        Sha  = '8ab7fca034bdc0cd02c40afde11a0936ec0ed3520a7ca97a169302e49ca2edaf'
        Dir  = 'D:\tuya_proj\TuyaOpen\.worktrees\05-camera\examples\peripherals\camera\output_display\.build\jieli-staging\build\cpu\wl82\tools'
        Note = 'SPI GC0310 640x480 packed-YUYV preview, line-end tag check, SPI2 40 MHz'
    }
    spiprev80 = @{
        File = 'spi-preview-386f4512.bin'
        Sha  = '386f4512d2ff6839bc5b91b51bd0f346a7dfa9df87439987e85cbb8a907492bb'
        Dir  = 'D:\tuya_proj\TuyaOpen\.worktrees\05-camera\examples\peripherals\camera\output_display\.build\jieli-staging\build\cpu\wl82\tools'
        Note = 'SPI GC0310 640x480 packed-YUYV preview, line-end tag check, SPI2 80 MHz'
    }
    spidiag = @{
        File = 'spi-diag-d9c34da9.bin'
        Sha  = 'd9c34da91c1587cfeae8a3857787f6de97b4904abdf3559e9bd5022303d8d1c3'
        Dir  = 'D:\tuya_proj\TuyaOpen\.worktrees\05-camera\examples\peripherals\camera\output_display\.build\jieli-staging\build\cpu\wl82\tools'
        Note = 'SPI preview + line-accept/reject counters and observed end tags'
    }
    spiresync = @{
        File = 'spi-resync-485b5845.bin'
        Sha  = '485b58455882b8a70edab7117bbac1a78e7eccb8055fa5ef37a69104ea571463'
        Dir  = 'D:\tuya_proj\TuyaOpen\.worktrees\05-camera\examples\peripherals\camera\output_display\.build\jieli-staging\build\cpu\wl82\tools'
        Note = 'SPI preview, line length derived from the next tag (shift-tolerant)'
    }
    spivendor = @{
        File = 'spi-vendor-16242c7c.bin'
        Sha  = '16242c7c5518471c66f565a712f415ef3bce418bb600fc3bc3bd305cf9efd054'
        Dir  = 'D:\tuya_proj\TuyaOpen\.worktrees\05-camera\examples\peripherals\camera\output_display\.build\jieli-staging\build\cpu\wl82\tools'
        Note = 'SPI preview, whole-frame block + fixed 1288 stride (vendor unpack), ret prints'
    }
    spistream = @{
        File = 'spi-stream-9168d937.bin'
        Sha  = '9168d937a463006f8f6c985e2204ad4d0368c1118a82927d663162e1dbbc5652'
        Dir  = 'D:\tuya_proj\TuyaOpen\.worktrees\05-camera\examples\peripherals\camera\output_display\.build\jieli-staging\build\cpu\wl82\tools'
        Note = 'SPI preview, two-frame staging buffer, head+tail validated frame scan'
    }
    spisimple = @{
        File = 'spi-simple-02f5265b.bin'
        Sha  = '02f5265bdafa8d7997f7bc4fc03ac7896475e061ea2ac40a596b7944a6a89085'
        Dir  = 'D:\tuya_proj\TuyaOpen\.worktrees\05-camera\examples\peripherals\camera\output_display\.build\jieli-staging\build\cpu\wl82\tools'
        Note = 'SPI preview, lenient tag scan (continuous frames), 40 MHz, stats only'
    }
    spimute = @{
        File = 'spi-mute-ch1-40154bdc.bin'
        Sha  = '40154bdc6ea486d9727d5e5c9ca2dfa93c9f4be6f6400c0ab91959182ba9f235'
        Dir  = 'D:\tuya_proj\TuyaOpen\.worktrees\05-camera\examples\peripherals\camera\output_display\.build\jieli-staging\build\cpu\wl82\tools'
        Note = 'SPI preview + PA kept muted at boot + XCLK on CH1 (vendor value)'
    }
    spitiming = @{
        File = 'spi-timing-1b6c7e62.bin'
        Sha  = '1b6c7e62a71d0d634706189878472be31b0ddfde91534a40c7cd11b827e74595'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'SPI preview + PA mute + CH1, conv vs flush timing split'
    }
    spilut = @{
        File = 'spi-lut-b50c0d62.bin'
        Sha  = 'b50c0d6285ef7081531adc9cc913d498df40c9936c7e97c9fd0553bc3a8e103e'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'SPI preview + PA mute + CH1 + table-driven YUV converter'
    }
    spifast = @{
        File = 'spi-fast-b0e44c19.bin'
        Sha  = 'b0e44c19123aa387db6bb375b7e2d5d8a759d5f4b1bee3b1ece2e1d509d7d5a4'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'SPI preview + PA mute + CH1 + hoisted YUYV fast path'
    }
    spiwait = @{
        File = 'spi-wait-9b91b249.bin'
        Sha  = '9b91b249453e9a0b97d61f99e7edd616a63e3dd4d991e1d7901cd0ff3385e0dc'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'SPI preview + PA mute + CH1 + fast YUYV + sem/fb wait split'
    }
    spiframe = @{
        File = 'spi-frame-75599543.bin'
        Sha  = '75599543f901055c3cf11453a6c319489576d616c08b2e47947fd3e41d8e6d96'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'SPI preview + PA mute + CH1 + head/tail validated frame decoder'
    }
    spifinal = @{
        File = 'spi-final-c32ca2e6.bin'
        Sha  = 'c32ca2e653de439378900e349888c89dbc5c09543ddb0704e84762de4896499c'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'SPI preview final: PA boot-mute, CH1 XCLK, fast YUYV converter, tolerant parser'
    }
    spiband = @{
        File = 'spi-band-471260f3.bin'
        Sha  = '471260f331e54c4ca8ed320b443f74751fe163a9d213f0fc2dee4e20c266213e'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'spi-final + GC0310 HB 0x06=0x80 (vendor tear-line advice)'
    }
    spistitch = @{
        File = 'spi-stitch-881b18fd.bin'
        Sha  = '881b18fdf7b50db3ad4635b0ff9b3b494127bd843087562d5873f26b275c3c27'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'SPI preview: boundary line stitched, false line heads rejected, HB 0x55'
    }
    spicarry = @{
        File = 'spi-carry-daa55f5f.bin'
        Sha  = 'daa55f5f24a0dbd53619c4f9097329d2e0b8b223757d78bb93ee05c99a2654a4'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'SPI preview: tolerant lines + boundary line carried across blocks'
    }
    spidup = @{
        File = 'spi-dup-c8a40c1d.bin'
        Sha  = 'c8a40c1d7b9d1570c4cb2a3703a5a5d964f030deb0f3268af5c84791bfb76ff3'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'SPI preview: bad rows duplicated instead of painted, frames still delivered'
    }
    spitriple = @{
        File = 'spi-triple-febf0e6a.bin'
        Sha  = 'febf0e6a4c7b72da614ef59c50e90ba9427f38cf2c089c954700de40edf6b55e'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'SPI preview + 3 display buffers (tearing test)'
    }
    panelbands = @{
        File = 'panel-bands-639262d0.bin'
        Sha  = '639262d062035644bd9ae9dfcb65153088eda582d1b356a0b1d3d0fd240b8489'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'panel-only diagnostic: static red/green bands, no camera data'
    }
    spifreeze = @{
        File = 'spi-freeze-f8cfa0b9.bin'
        Sha  = 'f8cfa0b97f5053fa7af67706b0a62fc1fc6a2cf922a573562927622f701334be'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'diagnostic: freeze on the first camera frame'
    }
    spite = @{
        File = 'spi-te-61aa1234.bin'
        Sha  = '61aa1234c22e456452827b756036d91e1218df5ddd610545aa04604c41dd379b'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'SPI preview + TE (PC0) synchronised flush'
    }
    spitimeout = @{
        File = 'spi-timeout-b595629b.bin'
        Sha  = 'b595629be01bb0bc92336f3b79239bd8e8155e8b8e986f2d30c5dc145555a119'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'SPI preview + TE flush + drop mixed (over-period) frame assemblies'
    }
    spidisptime = @{
        File = 'spi-disptime-87bf859a.bin'
        Sha  = '87bf859a003766e60ae840fbff71e693622d50b5982105516c5a5031fcb6dc43'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'SPI preview + TE + frame timeout + 8080 TE/transfer timing prints'
    }
    spirobust = @{
        File = 'spi-robust-071f1ee8.bin'
        Sha  = '071f1ee844480dd23ea282ffd95319841b96e62c936305aaaa961ff45683fdd3'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'spitimeout + thread/buffer lifecycle robustness (review fixes)'
    }
    spiqvga = @{
        File = 'spi-qvga-85da9e9c.bin'
        Sha  = '85da9e9cf786a5eb643e25b4888dd0a7cbc392652a2197c3f6579fd582452a5a'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'SPI preview at 320x240 (GC0310 crop window 320x240)'
    }
    spiqvgadbg = @{
        File = 'spi-qvgadbg-b9956762.bin'
        Sha  = 'b9956762bfcc0ed058d4dcd40a8ba412ab4b0e83f01fa76bae34dff755d9d45e'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'QVGA config + first-packet 0x80 tag spacing probe'
    }
    spiqvgaout = @{
        File = 'spi-qvgaout-db4dd238.bin'
        Sha  = 'db4dd238e96f5bce98440b72843b9b08aeb2b142ad92dfd3d590f1d7ade0e3d7'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'QVGA config + GC0310 output window 328x248 + crop 320x240'
    }
    spiqvgadec = @{
        File = 'spi-qvga-decimate-0979fd35.bin'
        Sha  = '0979fd351af89ca3919367241e3e243f7461d65bb736e64fd148234f194a1d30'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'QVGA preview by host 2:1 decimation of the fixed VGA sensor stream'
    }
    spivga4fps = @{
        File = 'spi-vga4fps-1060c40a.bin'
        Sha  = '1060c40a99316631f0e3ad082846c14e49fee26b900384a60432b2315d86b507'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'VGA 640x480, GC0310 VTS 1590 (~4 fps), clean-frame-only delivery'
    }
    spivga4fpstidy = @{
        File = 'spi-vga4fps-49759776.bin'
        Sha  = '49759776986e9c0edfa8fdde76c3c4db259a4b6289bbe546e5c8592d55fe8caa'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'VGA 4fps clean-frame build after code tidy (stats label)'
    }
    spivga4fpstidy2 = @{
        File = 'spi-vga4fps-c05d2600.bin'
        Sha  = 'c05d2600d98de70ab3452d98604af7f249fde11923f4fae02ca1ae6a16c6d245'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'SPI VGA 4fps clean-frame build after tidy (corrected config choice)'
    }
    spifalsehead = @{
        File = 'spi-vga4fps-a5d9c263.bin'
        Sha  = 'a5d9c2637459ecb3d71011353ab7d92f6db589bdec5d7bc3d855ec7410bc3be1'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'SPI VGA 4fps; line start validated by framing, false 0x80 heads skipped'
    }
    spipadstable = @{
        File = 'spi-vga-pad-31d4021f.bin'
        Sha  = '31d4021f8bde2068a2f5df8011483228c4e5feeeedad4e25a48300598054ac18'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'SPI VGA, vendor VTS, false-head validation + last-row padding'
    }
    spi48m = @{
        File = 'spi-48m-3cb59a80.bin'
        Sha  = '3cb59a804f0d94d456b5cefe19cfced595b8a2feaa28e77fc5f77326560d1553'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'SPI VGA with GC0310 48M pclk branch (link clock x2)'
    }
    spilineirqdma = @{
        File = 'spi-lineirq-dma-922ffbd9.bin'
        Sha  = '922ffbd9fdd6d4f3150253da64b4e9eeaa73355ff933c39b4f0e6c2e6035e7cb'
        Dir  = 'D:\tuya_proj\TuyaOpen\.worktrees\05-camera\examples\peripherals\camera\output_display\.build\jieli-staging\build\cpu\wl82\tools'
        Note = 'SPI preview, per-line IRQ + DMA row copy (vendor ISR scheme), 640x480@4'
    }
    spilineirq = @{
        File = 'spi-lineirq-f251a227.bin'
        Sha  = 'f251a227c78bb64cb42ec5c8889c6ecb4e01a529e396a9e1dbe4a1040be14d9d'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'SPI VGA via vendor-style per-line IRQ receive (JIELI_SPI_LINE_IRQ=1)'
    }
    spiupstream = @{
        File = 'spi-upstream-99a2db6e.bin'
        Sha  = '99a2db6e8e516805b7a1c9809c0dde1f5e675777e04a7fd3d93de75f061f58c2'
        Dir  = 'D:\tuya_proj\TuyaOpen\.worktrees\05-camera\examples\peripherals\camera\output_display\.build\jieli-staging\build\cpu\wl82\tools'
        Note = 'SPI VGA 4fps with upstream example_camera.c unchanged; 2 FBs (config macro bug)'
    }
    spiupstream3fb = @{
        File = 'spi-upstream3fb-8c39c3b8.bin'
        Sha  = '8c39c3b807065a5ea0cafc673f6f9f8e46129f8f31c4b7046c0ee35239b44e85'
        Dir  = 'D:\tuya_proj\TuyaOpen\.worktrees\05-camera\examples\peripherals\camera\output_display\.build\jieli-staging\build\cpu\wl82\tools'
        Note = 'SPI VGA 4fps, upstream example, yuv_order from TDD, 3 FBs'
    }
    spitddd = @{
        File = 'spi-tdd-bda963a3.bin'
        Sha  = 'bda963a345edea63b55cee98e0d21716c5fb33eaa0ce3b3f9fd3c08fe568d6e1'
        Dir  = 'D:/tuya_proj/TuyaOpen/.worktrees/05-camera/examples/peripherals/camera/output_display/.build/jieli-staging/build/cpu/wl82/tools'
        Note = 'output_display spi_tdd profile with raw boot markers (TDD GC0310 bring-up)'
    }
    spitddsensor = @{
        File = 'spi-tddsensor-5eafd609.bin'
        Sha  = '5eafd6094120326ad70a6c4bbd32459d13cf9a12fa660fbb04fba318004fb761'
        Dir  = 'D:\tuya_proj\TuyaOpen\.worktrees\05-camera\examples\peripherals\camera\output_display\.build\jieli-staging\build\cpu\wl82\tools'
        Note = 'SPI VGA, GC0310 configured by TDD over IIC1 (no video1), 24 MHz table, 3 FBs'
    }
    talprobe = @{
        File = 'tal-probe-minimal-e3532fb5.bin'
        Sha  = 'e3532fb5807fbb4ea899973dc01e5905f490f218477cb5672170efe6a5c1f9e7'
        Dir  = 'D:\tuya_proj\TuyaOpen\.worktrees\02-voice\tal-probe-minimal'
        Note = 'TAL audio engine minimal probe (624,112 B): keyword_tflite init + PCM, boot markers'
    }
    audiovad = @{
        File = 'audio-vad-ef600bb8.bin'
        Sha  = 'ef600bb86f224403d51e32a72f0ff3ca3f4188d2167ba9a8d7c91835435c8390'
        Dir  = 'D:\tuya_proj\TuyaOpen\.worktrees\05-camera\examples\multimedia\audio_vad\.build\jieli-staging\build\cpu\wl82\tools'
        Note = 'audio_vad on audio/tal-engine (stop-timeout guard, join handshake, re-init hysteresis)'
    }
    audiokws = @{
        File = 'audio-kws-de71c3cc.bin'
        Sha  = 'de71c3ccf7137c5683aa28b9717616e218265ca4bd9d03450e0b25fb38ee2ce3'
        Dir  = 'D:\tuya_proj\TuyaOpen\.worktrees\05-camera\examples\multimedia\audio_kws\.build\jieli-staging\build\cpu\wl82\tools'
        Note = 'audio_kws on audio/tal-engine (76-byte result struct, forward_pcm 1=hit, 44 KiB arena)'
    }
    taltokens = @{
        File = 'tal-tokens-f0a4675a.bin'
        Sha  = 'f0a4675a7a129c6051f700e3b7f19c1622afe27a7ca3a53092ed55cfed7a5127'
        Dir  = 'D:\tuya_proj\TuyaOpen\.worktrees\02-voice\apps\tuya.ai\your_chat_bot\.build\jieli-staging\build\cpu\wl82\tools'
        Note = 'vendor tal_probe_tokens demo: nhty (你好涂鸦) model + SAPI PCM, 40 token classes, batch 15'
    }
    touch = @{
        File = 'touch-54a61aa5.bin'
        Sha  = '54a61aa539f11be81cd960017da29e7681cd66a056d219706ffa1b970ab629ad'
        Dir  = 'D:\tuya_proj\TuyaOpen\.worktrees\05-camera\examples\peripherals\camera\output_display\.build\jieli-staging\build\cpu\wl82\tools'
        Note = 'touch product rollback (verified bootable)'
    }
}

$spec  = $images[$Image]
$src   = Join-Path $root ("images\" + $spec.File)
$tools = $spec.Dir
$dst   = Join-Path $tools 'app.bin'

if (-not (Test-Path $src)) {
    throw "missing archived image $src"
}
$sha = (Get-FileHash $src -Algorithm SHA256).Hash.ToLower()
if ($sha -ne $spec.Sha) {
    throw "archived image SHA mismatch: expected $($spec.Sha), got $sha"
}

Write-Host "[flash-791] $Image : $($spec.Note)"
Write-Host "[flash-791] image $($spec.File) $sha"

function Get-JieliDevice {
    Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
        Where-Object { $_.InstanceId -match 'VID_4C4A|VID_3654|VID_4155' } |
        Select-Object -First 1
}

if ($Wait) {
    Write-Host '[flash-791] waiting for a JIELI USB device (Ctrl+C to abort) ...'
    while (-not (Get-JieliDevice)) { Start-Sleep -Seconds 2 }
}

$dev = Get-JieliDevice
if (-not $dev) {
    throw 'no JIELI USB device present (WL82 UDISK or WL82 UBOOT1.00)'
}
Write-Host "[flash-791] device: $($dev.FriendlyName)"

Copy-Item $src $dst -Force
$dstSha = (Get-FileHash $dst -Algorithm SHA256).Hash.ToLower()
if ($dstSha -ne $spec.Sha) {
    throw "staged app.bin SHA mismatch after copy: $dstSha"
}

$logDir = Join-Path $root 'logs'
New-Item -ItemType Directory -Force $logDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$log = Join-Path $logDir "flash-791-$Image-$stamp.log"

Push-Location $tools
try {
    # A crashing (or half-initialised) app enumerates for a fraction of a
    # second per boot, so a single isd_download run usually starts while the
    # device is gone and exits "Device Offline". Re-check for the device
    # before every attempt and retry; the download itself then holds the chip
    # in update mode once one attempt lands. The 2026-11-17 VAD recovery
    # succeeded on the first synchronised attempt after two plain failures.
    $attempts = [Math]::Max(1, $Attempts)
    for ($i = 1; $i -le $attempts; $i++) {
        if (-not (Get-JieliDevice)) {
            Start-Sleep -Milliseconds 250
            continue
        }
        Write-Host "[flash-791] attempt $i/$attempts"
        & .\isd_download.exe isd_config.ini -gen2 -tonorflash -dev wl82 -boot 0x1c02000 `
            -div1 -wait 5000 -uboot uboot.boot -app app.bin cfg_tool.bin -res cfg -reboot 2000 -extend-bin 2>&1 |
            Tee-Object -FilePath $log | Select-Object -Last 12
        if ($LASTEXITCODE -eq 0) {
            break
        }
        Write-Host "[flash-791] attempt $i failed (exit $LASTEXITCODE), retrying ..."
        Start-Sleep -Milliseconds 300
    }
    if ($LASTEXITCODE -ne 0) {
        throw "isd_download exit $LASTEXITCODE after $attempts attempts (log: $log)"
    }
} finally {
    Pop-Location
}

Write-Host "[flash-791] done, log: $log"



















