cmake_minimum_required(VERSION 3.13)

project(MY-RP6502-PROJECT C ASM)

add_subdirectory(tools)

add_executable(ext-mass)
rp6502_executable(ext-mass DATA 0x9A00 RESET 0x9A00)
target_sources(ext-mass PRIVATE src/ext-mass.c)
target_link_options(ext-mass PRIVATE -C ${CMAKE_CURRENT_LIST_DIR}/tools/rp6502_9A00.cfg)

add_executable(qasm)
rp6502_executable(qasm
    DATA 0x0200
    RESET 0x0200
    ${CMAKE_CURRENT_SOURCE_DIR}/src/qasm.hlp
)
target_sources(qasm PRIVATE
    src/qasm.c
    src/qasm_cpustate.s
    src/test.s
    src/run.s
)

add_executable(test)
rp6502_executable(test
    DATA 0x0200
    RESET 0x0200
    ${CMAKE_CURRENT_SOURCE_DIR}/src/test.hlp
)
target_sources(test PRIVATE
    src/test.c
    src/test.s
)

add_executable(shell)
rp6502_executable(shell
    DATA 0x0200
    RESET 0x0200
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shell.hlp
)
target_sources(shell PRIVATE
    src/shell.c
)
target_link_options(shell PRIVATE -C ${CMAKE_SOURCE_DIR}/src/shell.cfg)
