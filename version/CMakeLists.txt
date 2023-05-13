add_custom_target(nostr_server_version
    ${CMAKE_COMMAND} -D SRC=${CMAKE_SOURCE_DIR}/version/version.h.in
          -D DST=${CMAKE_BINARY_DIR}/src/nostr_server_version.h
          -D ROOT=${CMAKE_SOURCE_DIR}
          -D GIT_EXECUTABLE=${GIT_EXECUTABLE}
          -P ${CMAKE_CURRENT_LIST_DIR}/GenerateVersionHeader.cmake
          )
