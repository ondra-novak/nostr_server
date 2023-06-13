cmake_minimum_required(VERSION 3.1)

add_executable(nostr_server
	main.cpp
	app.cpp
	peer.cpp
	signature.cpp
	fulltext.cpp	
)

target_link_libraries(nostr_server
    coroserver
    leveldb
    secp256k1
    ssl
    crypto    
    unac
	${STANDARD_LIBRARIES}
)
add_dependencies(nostr_server nostr_server_version)

