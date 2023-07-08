#include "nostr_server.h"

#include "config.h"
#include "app.h"
#include "relay_bot.h"


#include <nostr_server_version.h>
#include <shared/ini_config.h>
#include <coroserver/http_server.h>
#include <coroserver/http_ws_server.h>
#include <coroserver/io_context.h>
#include <coroserver/signal.h>
#include <leveldb/cache.h>
#include <docdb/json.h>

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

static std::unique_ptr<leveldb::Cache> leveldb_cache;


void read_leveldb_options(const ondra_shared::IniConfig::Section &ini, leveldb::Options &opts) {
    leveldb_cache = std::unique_ptr<leveldb::Cache>(leveldb::NewLRUCache(ini["rlu_cache_mb"].getUInt(8)*1024*1024));
    opts.block_cache = leveldb_cache.get();
    opts.max_file_size=  ini["max_file_size_mb"].getUInt(2)*1024*1024;
    opts.create_if_missing =  ini["create_if_missing"].getBool(true);
    opts.write_buffer_size = ini["write_buffer_size_mb"].getUInt(4)*1024*1024;
    opts.max_open_files = ini["max_open_files"].getUInt(1000);
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
    auto cfgpath = defcfg.parent_path();

    auto main = cfg["server"];
    auto db = cfg["database"];
    auto ssl = cfg["ssl"];
    auto desc = cfg["description"];
    auto options = cfg["options"];
    auto replication = cfg["replication"];
    auto metrics = cfg["metrics"];
    auto relaybot = cfg["relaybot"];

    nostr_server::Config outcfg;
    outcfg.listen_addr = main["listen"].getString("localhost:10000");
    outcfg.threads = main["threads"].getUInt(4);
    auto doc_root_path = cfgpath.parent_path() / "www";
    auto db_root_path = cfgpath.parent_path() / "data";
    outcfg.web_document_root = main["web_document_root"].getPath(doc_root_path);

    outcfg.database_path = db["path"].getPath(db_root_path);
    read_leveldb_options(db,outcfg.leveldb_options);

    std::string cert_chain = ssl["cert_chain_file"].getPath();
    std::string priv_key = ssl["priv_key_file"].getPath();
    std::string_view ssl_listen_addr = ssl["listen"].getString("localhost:10001");

    if (!cert_chain.empty() && !priv_key.empty() && !ssl_listen_addr.empty()) {
        outcfg.cert.emplace();
        outcfg.cert->load_cert(std::string(cert_chain));
        outcfg.cert->load_priv_key(std::string(priv_key));
        outcfg.ssl_listen_addr.append(ssl_listen_addr);
    }

    outcfg.description.name = desc["name"].getString();
    outcfg.description.desc = desc["description"].getString();
    outcfg.description.contact = desc["contact"].getString();
    outcfg.description.pubkey = desc["pubkey"].getString();

    outcfg.options.pow = options["pow"].getUInt(0);
    outcfg.options.event_rate_limit = options["event_rate_limit"].getUInt(6);
    outcfg.options.event_rate_window = options["event_rate_window"].getUInt(60);
    outcfg.options.auth = options["auth"].getBool(false);
    outcfg.options.block_strangers = options["block_strangers"].getBool(false);
    outcfg.options.foreign_relaying = options["foreign_relaying"].getBool(false);
    outcfg.options.replicators = options["replicators"].getString();
    outcfg.options.read_only= options["read_only"].getBool(false);


    outcfg.replication_config.private_key = replication["private_key"].getString();
    outcfg.replication_config.this_relay_url = replication["this_relay_url"].getString();

    for (const auto &kv : replication) {
        nostr_server::ReplicationTask task;

        if (kv.first.getView().compare(0,4,"out:") == 0) {
            task.relay_url = std::string(kv.first.getView().substr(4));
            task.inbound = false;
        } else if (kv.first.getView().compare(0,3,"in:") == 0) {
            task.relay_url = std::string(kv.first.getView().substr(3));
            task.inbound = true;
        } else {
            continue;
        }
        if (task.relay_url.compare(0,5,"ws://") != 0 && task.relay_url.compare(0,6,"wss://") != 0) {
            throw std::invalid_argument("Replication - relay url is invalid");
        }
        if (!kv.second.getBool()) {
           std::string fltfile = kv.second.getPath();
           std::ifstream fltf(fltfile, std::ios::in);
           if (!fltf) throw std::runtime_error("Failed to open filter file: " + fltfile);
           std::istream_iterator<char> iter(fltf);
           std::istream_iterator<char> end;
           auto json = docdb::Structured::from_json(iter, end);
           if (json.contains<docdb::Structured::Array>()) {
               const auto &a = json.array();
               for (const auto &x: a) {
                   task.filters.push_back(nostr_server::Filter::create(x));
               }
           } else {
               task.filters.push_back(nostr_server::Filter::create(json));
           }
        }

        outcfg.replication_config.tasks.push_back(std::move(task));
    }

    outcfg.metric.auth = metrics["auth"].getString();
    outcfg.metric.enable = metrics["enable"].getBool();

    outcfg.botcfg.nsec = relaybot["private_key"].getString();
    outcfg.botcfg.admin = relaybot["admin_pubkey"].getString();
    outcfg.botcfg.this_relay_url = relaybot["this_relay_url"].getString();
    outcfg.botcfg.groups = relaybot["groups"].getString();




    return outcfg;
}

int main(int argc, char **argv) {
    coroserver::ssl::Context::initSSL();
    try {
        auto cfg = init_cfg(argc, argv);

        auto addrs = coroserver::PeerName::lookup(cfg.listen_addr,"");

        if (cfg.cert.has_value()) {
            auto secure_addrs = coroserver::PeerName::lookup(cfg.ssl_listen_addr,"");
            for (auto &x: secure_addrs) {
                x.set_group_id(1);
                addrs.push_back(std::move(x));
            }
        }
        coroserver::ContextIO ctx = coroserver::ContextIO::create(cfg.threads);
        auto listener = ctx.accept(std::move(addrs));
        coroserver::http::Server::RequestFactory rf;
        if (cfg.cert.has_value()) {
            auto sslctx = coroserver::ssl::Context::init_server();
            sslctx.set_certificate(*cfg.cert);
            rf = coroserver::http::Server::secure(sslctx, 1);
        }
        coroserver::http::Server server(rf);
        auto app = std::make_shared<nostr_server::App>(cfg);
        app->init_handlers(server);

        nostr_server::RelayBot::run_bot(app.get(),cfg.botcfg).detach();

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
