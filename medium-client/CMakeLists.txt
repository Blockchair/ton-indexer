# cmake_minimum_required(VERSION 3.0.2 FATAL_ERROR)

# add_library(lite-client-common lite-client-common.cpp lite-client-common.h)
# target_link_libraries(lite-client-common PUBLIC tdutils tdactor adnllite tl_api tl_lite_api tl-lite-utils ton_crypto ton_block)
# add_cxx_compiler_flag("-lpq")

# set(libpqxx_DIR "/home/lorg/Downloads/libqpxx648/libpqxx-6.4.8/cmake")
# include(FindPQXX.cmake)
# find_package(PQXX REQUIRED)

# find_library(PQXX_LIBRARY
# NAMES libpqxx pqxx
# PATHS
#     ${CMAKE_INSTALL_PREFIX}/lib/${CMAKE_LIBRARY_ARCHITECTURE}
#     /usr/local/pgsql/lib/${CMAKE_LIBRARY_ARCHITECTURE}
#     /usr/local/lib/${CMAKE_LIBRARY_ARCHITECTURE}
#     /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}
#     ${CMAKE_INSTALL_PREFIX}/lib
#     ${CMAKE_INSTALL_PREFIX}/bin
#     /usr/local/pgsql/lib
#     /usr/local/lib
#     /usr/lib
# DOC "Location of libpqxx library"
# NO_DEFAULT_PATH
# )


find_package(PQXX)

# add_cxx_compiler_flag("-lpqxx")
# add_cxx_compiler_flag("-lpq")

# -I /usr/local/include/pqxx/ -L /usr/local/lib/ -I /usr/local/pgsql/include/ -L /usr/local/pgsql/lib/

# add_cxx_compiler_flag("-I/usr/local/include/pqxx/")
# add_cxx_compiler_flag("-L/usr/local/lib/")

add_library(medium-client-db database.cpp database.h)

add_executable(medium-client medium-client.cpp medium-client.hpp medium-client.h beggars.hpp flow-control.hpp tx-parsers.h tx-parsers.cpp)
add_executable(transaction-tester tester.cpp medium-client.hpp medium-client.h beggars.hpp flow-control.hpp tx-parsers.h tx-parsers.cpp)
# target_compile_options(medium-client PRIVATE -lpq -lpqxx)
target_include_directories(medium-client PUBLIC ${PQXX_INCLUDE_DIRECTORIES})
# target_include_directories(medium-client-db PUBLIC "/usr/include/postgresql/") # interesting artifact
# target_include_directories(medium-client PUBLIC "/usr/include/postgresql/") # interesting artifact
# target_include_directories(medium-client PUBLIC "/usr/local/lib/")
# target_include_directories(medium-client-db PRIVATE "/usr/local/include/pqxx/")
# target_include_directories(medium-client PRIVATE "/usr/local/include/pqxx/")

target_link_libraries(medium-client tdutils tdactor adnllite tl_api tl_lite_api tl-lite-utils ton_crypto ton_block terminal git medium-client-db ${PQXX_LIBRARIES})
target_link_libraries(transaction-tester tdutils tdactor adnllite tl_api tl_lite_api tl-lite-utils ton_crypto ton_block terminal git ${PQXX_LIBRARIES})
add_cxx_compiler_flag("-lpq") # rewrite with target_ at some point later down the line

install(TARGETS medium-client RUNTIME DESTINATION bin)
