#include "nostr_server.h"

#include "config.h"
#include "app.h"

#include <nostr_server_version.h>
#include <shared/ini_config.h>
#include <coroserver/http_server.h>
#include <coroserver/http_ws_server.h>
#include <coroserver/io_context.h>
#include <coroserver/signal.h>

#include <csignal>
#include <iostream>
#include <cstdlib>
#include <filesystem>


std::filesystem::path getDefaultConfigPath(const char *argv0) {
    std::filesystem::path bin;
    if (argv0[0] == '/') bin = argv0;
    else {
            auto curdir = std::filesystem::current_path();
            bin = curdir / argv0;
            bin = std::filesystem::weakly_canonical(bin);
    }
    return (bin.parent_path().parent_path() / "conf" / bin.filename()).replace_extension(".conf");
}

static void show_help(const char *argv0) {
    std::cout << "Usage: " << argv0 <<  " [-h|-f <config_path>]\n\n"
            "-h           show help\n"
            "-f <path>    path to configuration file\n";
    exit(0);
}

nostr_server::Config init_cfg(int argc, char **argv) {
    auto defcfg = getDefaultConfigPath(argv[0]);
    const char *params = "hf:";
    int opt = getopt(argc,argv,params);
    while (opt != -1) {
        switch (opt) {
            case 'h': show_help(argv[0]);break;
            case 'f': defcfg = optarg; break;
            default: throw std::invalid_argument("Unknown option, use -h for help");
        }
        opt = getopt(argc,argv,params);
    }

    ondra_shared::IniConfig cfg;
    cfg.load(defcfg);

    auto main = cfg["server"];


    nostr_server::Config outcfg;
    outcfg.listen_addr = main.mandatory["listen"].getString();
    outcfg.threads = main.mandatory["threads"].getUInt();
    auto doc_root_path = std::filesystem::path(main["listen"].getCurPath()).parent_path() / "www";
    outcfg.web_document_root = main["document_root"].getPath(doc_root_path);
    return outcfg;
}

int main(int argc, char **argv) {
    try {
        auto cfg = init_cfg(argc, argv);

        auto addrs = coroserver::PeerName::lookup(cfg.listen_addr,"");
        coroserver::ContextIO ctx = coroserver::ContextIO::create(cfg.threads);
        auto listener = ctx.accept(std::move(addrs));
        coroserver::http::Server server;
        auto app = std::make_shared<nostr_server::App>(cfg);
        app->init_handlers(server);

        auto task = server.start(std::move(listener),coroserver::http::DefaultLogger([](std::string_view line){
            std::cout << line << std::endl;
        }));

        coroserver::SignalHandler hndl(ctx);
        hndl({SIGINT, SIGTERM}).wait();

        ctx.stop();
        task.join();


    } catch (std::invalid_argument &e) {
        std::cerr << "Invalid command line: " << e.what();
        std::cerr << "Use -h for help" << std::endl;
        return 1;
    } catch (ondra_shared::IniConfig::ConfigLoadError &e) {
        std::cerr << e.what() << std::endl;
        std::cerr << "Use -h for help" << std::endl;
        return 2;
    } catch (std::exception &e) {
        std::cerr << "Fatal: " << e.what() << std::endl;
        return 3;
    }


    return 0;
}
