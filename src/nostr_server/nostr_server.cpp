#include "nostr_server.h"

#include <nostr_server_version.h>
#include <iostream>
#include <cstdlib>

int main(int argc, char **argv) {
    std::cout << "Version: " << PROJECT_NOSTR_SERVER_VERSION << std::endl;
    return 0;
}
