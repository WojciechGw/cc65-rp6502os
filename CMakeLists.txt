cmake_minimum_required(VERSION 3.20)

project(MY-RP6502-PROJECT C ASM)

add_subdirectory(tools)

add_executable(razemos)
rp6502_asset(razemos help src/shell.hlp)
# XRAM
rp6502_asset(razemos 0x12000 assets/gfx/bin/wallpaper.bin)
rp6502_asset(razemos [font8x8]experiment2.bmp assets/fnt/[font8x8]experiment2.bmp)
# ROM: commands
#rp6502_asset(razemos crx.com src/extcmd/build/crx.com)
#rp6502_asset(razemos ctx.com src/extcmd/build/ctx.com)
#rp6502_asset(razemos date.com src/extcmd/build/date.com)
#rp6502_asset(razemos dir.com src/extcmd/build/dir.com)
#rp6502_asset(razemos drives.com src/extcmd/build/drives.com)
#rp6502_asset(razemos help.com src/extcmd/build/help.com)
#rp6502_asset(razemos hex.com src/extcmd/build/hex.com)
#rp6502_asset(razemos keyboard.com src/extcmd/build/keyboard.com)
#rp6502_asset(razemos label.com src/extcmd/build/label.com)
#rp6502_asset(razemos peek.com src/extcmd/build/peek.com)
#rp6502_asset(razemos roms.com src/extcmd/build/roms.com)
#rp6502_asset(razemos term.com src/extcmd/build/term.com)
#rp6502_asset(razemos view.com src/extcmd/build/view.com)
#rp6502_asset(razemos editor.com src/extcmd/build/editor.com)
# screen savers
#rp6502_asset(razemos ss-matrix.com src/extcmd/build/ss-matrix.com)
#rp6502_asset(razemos ss-noise.com src/extcmd/build/ss-noise.com)
# standalone apps
# rp6502_asset(razemos hass.rp6502 build/hass.rp6502)
rp6502_executable(razemos DATA 0x0200 RESET 0x0200)
target_sources(razemos PRIVATE src/shell.c)
target_link_options(razemos PRIVATE -C ${CMAKE_SOURCE_DIR}/src/shell.cfg)

add_executable(hass)
rp6502_asset(hass help src/hass.hlp)
# ROM: commands
rp6502_asset(hass manual[en].txt docs/hass-manual-en.txt)
rp6502_asset(hass manual[pl].txt docs/hass-manual-pl.txt)
rp6502_executable(hass DATA 0x0200 RESET 0x0200)
target_sources(hass PRIVATE src/hass.c)
target_link_options(hass PRIVATE -C ${CMAKE_SOURCE_DIR}/src/hass.cfg)
