find_path(PQXX_HEADER_PATH
NAMES pqxx/pqxx
PATHS
	${CMAKE_INSTALL_PREFIX}/include
	/usr/local/pgsql/include
	/usr/local/include
	/usr/include
DOC "Path to pqxx/pqxx header file. Do not include the 'pqxx' directory in this value."
NO_DEFAULT_PATH
)

find_library(PQXX_LIBRARY
NAMES libpqxx pqxx
PATHS
	${CMAKE_INSTALL_PREFIX}/lib
	${CMAKE_INSTALL_PREFIX}/bin
	/usr/local/pgsql/lib
	/usr/local/lib
	/usr/lib
DOC "Location of libpqxx library"
NO_DEFAULT_PATH
)

if (PQXX_HEADER_PATH AND PQXX_LIBRARY)
	set(PQXX_FOUND 1 CACHE INTERNAL "PQXX found" FORCE)
	set(PQXX_INCLUDE_DIRECTORIES ${PQXX_HEADER_PATH})
	set(PQXX_LIBRARIES ${PQXX_LIBRARY})
	message("PQXX FOUND")
else (PQXX_HEADER_PATH AND PQXX_LIBRARY)
	message("PQXX NOT FOUND")
	if (NOT PQXX_HEADER_PATH)
		message("PQXX HEADER MISSING")
	endif (NOT PQXX_HEADER_PATH)

	if (NOT PQXX_LIBRARY)
		message("PQXX LIB MISSING")
	endif (NOT PQXX_LIBRARY)
endif (PQXX_HEADER_PATH AND PQXX_LIBRARY)


include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(PQXX DEFAULT_MSG PQXX_HEADER_PATH PQXX_LIBRARY)
mark_as_advanced(PQXX_HEADER_PATH PQXX_LIBRARY)
