## Process this file with automake to produce Makefile.in
## Copyright (c) 2018-2023 Philip Kendall, Stuart Brady, Sergio Baldoví,
##   Fredrick Meunier, Alistair Cree

## This program is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation; either version 2 of the License, or
## (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License along
## with this program; if not, write to the Free Software Foundation, Inc.,
## 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
##
## Author contact information:
##
## E-mail: philip-fuse@shadowmagic.org.uk

fuse_SOURCES += \
                peripherals/dck.c \
                peripherals/expansion_bus.c \
                peripherals/if1.c \
                peripherals/if2.c \
                peripherals/joystick.c \
                peripherals/kempmouse.c \
                peripherals/multiface.c \
                peripherals/printer.c \
                peripherals/scld.c \
                peripherals/speccyboot.c \
                peripherals/ula.c \
                peripherals/ulaplus.c \
                peripherals/usource.c \
                peripherals/ttx2000s.c \
                peripherals/sound/ay.c \
                peripherals/sound/covox.c \
                peripherals/sound/fuller.c \
                peripherals/sound/general_sound.c \
                peripherals/sound/gs_z80.c \
                peripherals/sound/melodik.c \
                peripherals/sound/sp0256.c \
                peripherals/sound/specdrum.c \
                peripherals/sound/uspeech.c \
                peripherals/disk/beta.c \
                peripherals/disk/crc.c \
                peripherals/disk/didaktik.c \
                peripherals/disk/disciple.c \
                peripherals/disk/disk.c \
                peripherals/disk/fdd.c \
                peripherals/disk/opus.c \
                peripherals/disk/plusd.c \
                peripherals/disk/trdos.c \
                peripherals/disk/upd_fdc.c \
                peripherals/disk/wd_fdc.c \
                peripherals/ide/divide.c \
                peripherals/ide/divmmc.c \
                peripherals/ide/divxxx.c \
                peripherals/ide/ide.c \
                peripherals/ide/simpleide.c \
                peripherals/ide/zxatasp.c \
                peripherals/ide/zxcf.c \
                peripherals/ide/zxmmc.c \
                peripherals/spectranet.c \
                peripherals/spectranext_esxdos.c

if BUILD_SPECCYBOOT
fuse_SOURCES += peripherals/nic/enc28j60.c
endif

if BUILD_SPECTRANET
fuse_SOURCES += \
                peripherals/flash/am29f010.c \
                peripherals/nic/w5100.c \
                peripherals/nic/w5100_socket.c \
                peripherals/nic/dns_resolver.c \
                peripherals/nic/dns_processor.c \
                peripherals/nic/engines/engine_argv.c \
                peripherals/nic/engines/engine_fs.c \
                peripherals/nic/controller_job.c \
                peripherals/nic/controller_job.h \
                peripherals/nic/spectranext_controller_layout.h \
                peripherals/nic/engines/cp.c \
                peripherals/nic/engines/engine_utf8.c \
                peripherals/nic/engines/json.c \
                peripherals/nic/engines/xpath.c \
                peripherals/nic/engines/jsonpath/ast.c \
                peripherals/nic/engines/jsonpath/lexer.c \
                peripherals/nic/engines/jsonpath/matcher.c \
                peripherals/nic/engines/jsonpath/parser.c \
                peripherals/nic/engines/parson.c \
                peripherals/nic/match.c \
                peripherals/nic/match_compat.c \
                peripherals/nic/stun.c \
                peripherals/nic/gre.c \
                peripherals/nic/lwip.c \
                peripherals/nic/icmp.c \
                3rdparty/lwip/src/core/def.c \
                3rdparty/lwip/src/core/init.c \
                3rdparty/lwip/src/core/inet_chksum.c \
                3rdparty/lwip/src/core/ip.c \
                3rdparty/lwip/src/core/mem.c \
                3rdparty/lwip/src/core/memp.c \
                3rdparty/lwip/src/core/netif.c \
                3rdparty/lwip/src/core/pbuf.c \
                3rdparty/lwip/src/core/raw.c \
                3rdparty/lwip/src/core/stats.c \
                3rdparty/lwip/src/core/sys.c \
                3rdparty/lwip/src/core/tcp.c \
                3rdparty/lwip/src/core/tcp_in.c \
                3rdparty/lwip/src/core/tcp_out.c \
                3rdparty/lwip/src/core/timeouts.c \
                3rdparty/lwip/src/core/ipv4/icmp.c \
                3rdparty/lwip/src/core/ipv4/ip4.c \
                3rdparty/lwip/src/core/ipv4/ip4_addr.c \
                peripherals/nic/spectranext_controller.c \
                peripherals/nic/spectranext_stdout.c \
                peripherals/fs/xfs.c \
                peripherals/fs/xfs_compat.c \
                peripherals/fs/xfs_worker.c \
                peripherals/fs/xfs_fs.c \
                peripherals/fs/xfs_overlay.c \
                peripherals/fs/xfs_romfs.c \
                peripherals/fs/ramfs.c \
                peripherals/fs/xfs_https.c \
                peripherals/fs/http_downloader.c \
                peripherals/fs/xfs_https_compat.c \
                peripherals/fs/xfs_sftp.c \
                peripherals/http/httpc.c \
                peripherals/http/http_sck.c \
                peripherals/security/tls.c \
                peripherals/security/ssh.c
endif

noinst_HEADERS += \
                  peripherals/nic/match.h \
                  peripherals/nic/match_compat.h \
                  peripherals/nic/match_layout.h \
                  peripherals/nic/stun.h \
                  peripherals/nic/gre.h \
                  peripherals/nic/icmp.h \
                  peripherals/nic/spectranext_strings.h \
                  peripherals/nic/match_lwip/lwipopts.h \
                  peripherals/nic/match_lwip/arch/cc.h \
                  peripherals/dck.h \
                  peripherals/expansion_bus.h \
                  peripherals/if1.h \
                  peripherals/if2.h \
                  peripherals/joystick.h \
                  peripherals/kempmouse.h \
                  peripherals/multiface.h \
                  peripherals/printer.h \
                  peripherals/scld.h \
                  peripherals/speccyboot.h \
                  peripherals/spectranet.h \
                  peripherals/spectranext_esxdos.h \
                  peripherals/ula.h \
                  peripherals/ulaplus.h \
                  peripherals/usource.h \
                  peripherals/ttx2000s.h \
                  peripherals/sound/ay.h \
                  peripherals/sound/covox.h \
                  peripherals/sound/fuller.h \
                  peripherals/sound/general_sound.h \
                  peripherals/sound/gs_z80.h \
                  peripherals/sound/melodik.h \
                  peripherals/sound/sp0256.h \
                  peripherals/sound/specdrum.h \
                  peripherals/sound/uspeech.h \
                  peripherals/disk/beta.h \
                  peripherals/disk/crc.h \
                  peripherals/disk/didaktik.h \
                  peripherals/disk/disciple.h \
                  peripherals/disk/disk.h \
                  peripherals/disk/fdd.h \
                  peripherals/disk/opus.h \
                  peripherals/disk/plusd.h \
                  peripherals/disk/trdos.h \
                  peripherals/disk/upd_fdc.h \
                  peripherals/disk/wd_fdc.h \
                  peripherals/ide/divide.h \
                  peripherals/ide/divmmc.h \
                  peripherals/ide/divxxx.h \
                  peripherals/ide/ide.h \
                  peripherals/ide/simpleide.h \
                  peripherals/ide/zxatasp.h \
                  peripherals/ide/zxcf.h \
                  peripherals/ide/zxmmc.h \
                  peripherals/flash/am29f010.h \
                  peripherals/nic/enc28j60.h \
                  peripherals/nic/w5100.h \
                  peripherals/nic/w5100_internals.h \
                  peripherals/fs/http_downloader.h \
                  peripherals/security/tls.h \
                  peripherals/security/ssh.h \
                  peripherals/security/mbedtls_config.h
