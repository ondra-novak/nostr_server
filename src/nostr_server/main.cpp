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
#include <shared/stdLogFile.h>

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
    auto log = cfg["log"];

    auto log_level = log["level"].getString("progress");
    auto log_file = log["file"].getPath();
    auto log_rot_count = log["rotate_count"].getUInt(7);
    auto log_rot_interval = log["rotate_interval"].getUInt(86400);

    auto log_level_enm = ondra_shared::LogLevelToStrTable().fromString(log_level);

    if (log_file.empty()) {
        auto l = new ondra_shared::StdLogProviderFactory(log_level_enm);
        l->setDefault();
    } else {
        auto l = new ondra_shared::StdLogFileRotating(log_file,log_level_enm, log_rot_count, log_rot_interval);
        l->setDefault();
    }

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
    outcfg.options.whitelisting = options["whitelisting"].getBool(true);
    outcfg.options.replicators = options["replicators"].getString();
    outcfg.options.read_only= options["read_only"].getBool(false);
    outcfg.options.media_max_size = options["max_file_size_kb"].getUInt(256)*1024;

    outcfg.private_key = replication["private_key"].getString("replicator_01");

#if 0
    for (const auto &item: replication) {
        std::string_view n = item.first.getView();
        if (n.compare(0,5,"task_") == 0) {
            outcfg.replication_config.push_back(nostr_server::ReplicationTask{
                    std::string(n.substr(5)),
                    std::string(item.second.getString())});
        }
    }
#endif

    outcfg.metric.auth = metrics["auth"].getString();
    outcfg.metric.enable = metrics["enable"].getBool();

    outcfg.botcfg.nsec = relaybot["private_key"].getString();
    outcfg.botcfg.admin = relaybot["admin_pubkey"].getString();
    outcfg.botcfg.this_relay_url = relaybot["this_relay_url"].getString();
    outcfg.botcfg.groups = relaybot["groups"].getString();




    return outcfg;
}

class Logger {
public:
    using TraceEvent = coroserver::http::TraceEvent;
    using ServerRequest = coroserver::http::ServerRequest;

    void operator()(TraceEvent ev, ServerRequest &req) {
        using namespace nostr_server;
        switch (ev) {
            case TraceEvent::open: _lg.info("New connection");
                                   return;
            case TraceEvent::load: _start_time = std::chrono::system_clock::now();
                                   _cntr = req.get_counters();
                                   return;
            case TraceEvent::exception: try {
                                            throw;
                                        } catch (std::exception &e) {
                                            _lg.error("$1", e.what());
                                        } catch (...) {
                                            _lg.error("[HTTP] Unknown exception");
                                        }
                                        [[fallthrough]];


            case TraceEvent::finish:{
                                        auto t = std::chrono::system_clock::now();
                                        auto dur = (t - _start_time).count();
                                        auto c = req.get_counters();
                                        auto r = c.read - _cntr.read;
                                        auto w = c.write - _cntr.write;
                                        _lg.progress("[HTTP] $1 $2 $3 $4s, r: $5KiB, w: $6KiB",
                                              coroserver::http::strMethod[req.get_method()],
                                                req.get_url(),
                                             req.get_status(),
                                            dur * 0.000000001,
                                            r * 0.001,
                                            w * 0.001
                                        );

                                    }
                                    return;
            case TraceEvent::close: _lg.info("Closed");
                                    return;
            case TraceEvent::logger: {
                auto &logger = req.get_logger_info();
                switch(static_cast<PeerServerity>(logger.serverity)) {
                    default:
                    case PeerServerity::debug: _lg.debug("$1", logger.message); break;
                    case PeerServerity::warn: _lg.warning("$1", logger.message); break;
                    case PeerServerity::progress: _lg.progress("$1", logger.message); break;
                    case PeerServerity::error: _lg.error("$1", logger.message); break;
                }
                return;
            }

        }

    }

    Logger() {}
    Logger(const Logger &):_lg(gen_id()) {}
    Logger(Logger &&) = default;
protected:
    ondra_shared::LogObject _lg;
    coroserver::IStream::Counters _cntr;
    std::chrono::system_clock::time_point _start_time;
    static std::string gen_id() {
        static std::atomic<std::size_t> counter = 0;
        auto id = counter++;
        char buff[100];
        sprintf(buff,"CONN:%lu", static_cast<unsigned long>(id));
        return buff;



    }

};


int main(int argc, char **argv) {
    coroserver::ssl::Context::initSSL();
    using namespace ondra_shared;
    try {
        auto cfg = init_cfg(argc, argv);
        coroserver::ContextIO ctx = coroserver::ContextIO::create(cfg.threads);
        cocls::future<void> task;
        try {

            logProgress("------------- START ----------------");


            auto addrs = coroserver::PeerName::lookup(cfg.listen_addr,"");

            if (cfg.cert.has_value()) {
                auto secure_addrs = coroserver::PeerName::lookup(cfg.ssl_listen_addr,"");
                for (auto &x: secure_addrs) {
                    x.set_group_id(1);
                    addrs.push_back(std::move(x));
                }
            }
            auto listener = ctx.accept(std::move(addrs));
            coroserver::http::Server::RequestFactory rf;
            if (cfg.cert.has_value()) {
                auto sslctx = coroserver::ssl::Context::init_server();
                sslctx.set_certificate(*cfg.cert);
                rf = coroserver::http::Server::secure(sslctx, 1);
            }
            coroserver::http::Server server(rf);
            logProgress("Opening database");
            auto app = std::make_shared<nostr_server::App>(cfg);
            logProgress("Database opened");
            app->init_handlers(server);

    //        nostr_server::RelayBot::run_bot(app.get(),cfg.botcfg).detach();

            task << [&]{return server.start(std::move(listener),Logger());};

            logProgress("Server listening at: $1", cfg.listen_addr);

            coroserver::SignalHandler hndl(ctx);
            hndl({SIGINT, SIGTERM}).wait();

        } catch (std::exception &e) {
            logFatal("$1", e.what());
        }
        logProgress("Server is exiting...");
        ctx.stop();
        if (task.joinable()) task.join();
        logProgress("Server exit");


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
