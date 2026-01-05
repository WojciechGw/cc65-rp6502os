cmake_minimum_required(VERSION 3.13)

project(MY-RP6502-PROJECT C ASM)

add_subdirectory(tools)

add_executable(ext-mass)
rp6502_executable(ext-mass DATA 0x9000 RESET 0x9000)
target_sources(ext-mass PRIVATE src/ext-mass.c)
target_link_options(ext-mass PRIVATE -C ${CMAKE_CURRENT_LIST_DIR}/src/ext0x9000.cfg)

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
