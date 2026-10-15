cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(imx95_m7_wolfboot_openamp)
target_sources(app PRIVATE src/main.c)

# wolfCrypt's own benchmark, not a hand-rolled one: it is the same program the
# A55 container runs, so the two cores' numbers are directly comparable and in
# the same units, and there is no bespoke timing code here to get wrong.
target_include_directories(app PRIVATE
    ${ZEPHYR_WOLFSSL_MODULE_DIR}/wolfcrypt/benchmark)

target_sources(app PRIVATE
    ${ZEPHYR_WOLFSSL_MODULE_DIR}/wolfcrypt/benchmark/benchmark.c)

# Select the algorithm set the same way the A55 container does, by argument.
# Two things are needed for that, and NO_MAIN_DRIVER is deliberately not one of
# them: it would remove wolfcrypt_benchmark_main() and the option tables along
# with main(). Rename main() instead, and ask wolfSSL to leave the parsing in -
# its Zephyr block defines MAIN_NO_ARGS by default because most applications
# have no argv to give it.
zephyr_compile_definitions(WOLFSSL_ZEPHYR_MAIN_ARGS)
set_source_files_properties(
    ${ZEPHYR_WOLFSSL_MODULE_DIR}/wolfcrypt/benchmark/benchmark.c
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PROPERTIES COMPILE_DEFINITIONS "main=wolfcrypt_bench_unused_main")
