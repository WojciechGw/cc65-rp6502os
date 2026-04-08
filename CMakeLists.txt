cmake_minimum_required(VERSION 3.20)

project(MY-RP6502-PROJECT C ASM)

add_subdirectory(tools)

add_executable(shell)
rp6502_asset(shell help src/shell.hlp)
# XRAM
rp6502_asset(shell 0x12000 assets/gfx/bin/wallpaper.bin)
rp6502_asset(shell [font8x8]experiment2.bmp assets/fnt/[font8x8]experiment2.bmp)
# ROM: commands
#rp6502_asset(shell crx.com src/extcmd/build/crx.com)
#rp6502_asset(shell ctx.com src/extcmd/build/ctx.com)
#rp6502_asset(shell date.com src/extcmd/build/date.com)
#rp6502_asset(shell dir.com src/extcmd/build/dir.com)
#rp6502_asset(shell drives.com src/extcmd/build/drives.com)
#rp6502_asset(shell hass.com src/extcmd/build/hass.com)
#rp6502_asset(shell help.com src/extcmd/build/help.com)
#rp6502_asset(shell keyboard.com src/extcmd/build/keyboard.com)
#rp6502_asset(shell label.com src/extcmd/build/label.com)
#rp6502_asset(shell term.com src/extcmd/build/term.com)
#rp6502_asset(shell view.com src/extcmd/build/view.com)
#rp6502_asset(shell editor.com src/extcmd/build/editor.com)
# screen savers
#rp6502_asset(shell ss-matrix.com src/extcmd/build/ss-matrix.com)
#rp6502_asset(shell ss-noise.com src/extcmd/build/ss-noise.com)
rp6502_executable(shell DATA 0x0200 RESET 0x0200)
target_sources(shell PRIVATE src/shell.c)
target_link_options(shell PRIVATE -C ${CMAKE_SOURCE_DIR}/src/shell.cfg)

#add_executable(shellboot)
#rp6502_asset(shellboot 0x12000 assets/bin/wallpaper.bin)
#rp6502_executable(shellboot DATA 0x0200 RESET 0x0200)
#target_sources(shellboot PRIVATE src/shellboot.c)
#target_link_options(shell PRIVATE rp6502)
