cmake_minimum_required(VERSION 3.1)

add_executable(nostr_server
	nostr_server.cpp
)

target_link_libraries(nostr_server
	${STANDARD_LIBRARIES}
)
add_dependencies(nostr_server nostr_server_version)

