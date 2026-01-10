cmake_minimum_required(VERSION 3.13)

project(MY-RP6502-PROJECT C ASM)

add_subdirectory(tools)

add_executable(ext-hass)
rp6502_executable(ext-hass DATA 0x9000 RESET 0x9000)
target_sources(ext-hass PRIVATE src/ext-hass.c)
target_link_options(ext-hass PRIVATE -C ${CMAKE_CURRENT_LIST_DIR}/src/ext0x9000.cfg)

add_executable(qasm)
rp6502_executable(qasm
    DATA 0x0200
    RESET 0x0200
    ${CMAKE_CURRENT_SOURCE_DIR}/src/archive/qasm.hlp
)
target_sources(qasm PRIVATE
    src/archive/qasm.c
    src/archive/qasm_cpustate.s
    src/archive/test.s
    src/archive/run.s
)

add_executable(test)
rp6502_executable(test
    DATA 0x0200
    RESET 0x0200
    ${CMAKE_CURRENT_SOURCE_DIR}/src/archive/test.hlp
)
target_sources(test PRIVATE
    src/archive/test.c
    src/archive/test.s
)

add_executable(shell)
rp6502_executable(shell DATA 0x0200 RESET 0x0200 ${CMAKE_CURRENT_SOURCE_DIR}/src/shell.hlp)
target_sources(shell PRIVATE src/shell.c)
target_link_options(shell PRIVATE -C ${CMAKE_SOURCE_DIR}/src/shell.cfg)

add_executable(midi)
rp6502_executable(midi DATA 0x0200 RESET 0x0200)
target_sources(midi PRIVATE src/midi.c)

add_executable(midia)
rp6502_executable(midia DATA 0x0200 RESET 0x0200)
target_sources(midia PRIVATE assets/midia.s)

add_executable(miditest)
rp6502_executable(miditest DATA 0x0200 RESET 0x0200)
target_sources(miditest PRIVATE src/miditest.c)

add_executable(miditest2)
rp6502_executable(miditest2 DATA 0x0200 RESET 0x0200)
target_sources(miditest2 PRIVATE src/miditest2.c)

add_executable(drv-midiout)
rp6502_executable(drv-midiout DATA 0x0200 RESET 0x0200 IRQ 0x0524)
target_sources(drv-midiout PRIVATE
 src/drv-midiout.c
 src/drv-midiout.s
 #src/drv-midiout-vectors.s
)
target_link_options(drv-midiout PRIVATE -C ${CMAKE_SOURCE_DIR}/src/drv-midiout.cfg)

add_executable(editor)
rp6502_executable(editor DATA 0x0200 RESET 0x0200)
target_sources(editor PRIVATE src/ext-editor.c)