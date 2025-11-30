cmake_minimum_required(VERSION 3.13)

project(MY-RP6502-PROJECT C ASM)

add_subdirectory(tools)

add_executable(qasm)
rp6502_executable(qasm
    DATA 0x200
    RESET 0x200
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
    src/quit.s
)
