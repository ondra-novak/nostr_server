cmake_minimum_required(VERSION 3.1)

add_executable(nostr_server
	main.cpp
	app.cpp
	peer.cpp
	signature.cpp
	fulltext.cpp
	rate_limiter.cpp
	telemetry_def.cpp
	bech32.cpp
#	relay_bot.cpp
	filter.cpp
	event.cpp
)

target_link_libraries(nostr_server
    coroserver
    telemetry
    ${LEVELDB_LIB}
    ${SECP256K1_LIBRARIES}
    ${LIBSSL_LIBRARIES}
    ${UNAC_LIBRARIES}
	${STANDARD_LIBRARIES}
)
add_dependencies(nostr_server nostr_server_version)

