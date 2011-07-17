#include <iostream>
#include <fstream>

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include "common.hpp"
#include "core.hpp"

using namespace yappi::core;
namespace po = boost::program_options;
namespace fs = boost::filesystem;

int main(int argc, char* argv[]) {
    std::vector<std::string> exports;
    po::options_description mandatory, options("Allowed options"), combined;
    po::positional_options_description positional;
    po::variables_map config;

    mandatory.add_options()
        ("listen", po::value< std::vector<std::string> >());
    
    positional.add("listen", -1);

    options.add_options()
        ("help", "show this message")
        ("export", po::value< std::vector<std::string> >(&exports), "endpoints to publish events from schedulers")
        ("watermark", po::value<uint64_t>()->default_value(1000), "maximum number of messages to keep on client disconnects")
        ("pid", po::value<fs::path>()->default_value("/var/run/yappi.pid"), "location of a pid file")
        ("daemonize", "daemonize on start")
        ("fresh", "do not try to recover tasks");

    combined.add(mandatory).add(options);

    try {
        po::store(
            po::command_line_parser(argc, argv).
                options(combined).
                positional(positional).
                run(),
            config);
        po::notify(config);
    } catch(const po::unknown_option& e) {
        std::cerr << "Error: " << e.what() << "." << std::endl;
    }

    if(config.count("help")) {
        std::cout << "Usage: " << argv[0] << " endpoint-list [options]" << std::endl;
        std::cout << options;
        return EXIT_SUCCESS;
    }

    if(!config.count("listen")) {
        std::cout << "Error: no endpoints specified." << std::endl;
        std::cout << "Try '" << argv[0] << " --help' for more information." << std::endl;
        return EXIT_FAILURE;
    }

    // Daemonizing, if needed
    if(config.count("daemonize")) {
        if(daemon(0, 0) < 0) {
            std::cout << "Error: daemonization failed." << std::endl;
            return EXIT_FAILURE;
        }

        // Setting up the syslog
        openlog(core_t::identity, LOG_PID | LOG_NDELAY, LOG_USER);
        setlogmask(LOG_UPTO(LOG_DEBUG));
        
        // Write the pidfile
        fs::ofstream file;
        file.exceptions(std::ofstream::badbit | std::ofstream::failbit);

        try {
            file.open(config["pid"].as<fs::path>(),
                std::ofstream::out | std::ofstream::trunc);
        } catch(const std::ofstream::failure& e) {
            syslog(LOG_ERR, "main: failed to write %s", config["pid"].as<fs::path>().string().c_str());
            return EXIT_FAILURE;
        }     

        file << getpid();
        file.close();
    }

    syslog(LOG_INFO, "main: yappi is starting");
    core_t* core;

    // Initializing the core
    try {
        core = new core_t(
            config["listen"].as< std::vector<std::string> >(),
            exports,
            config["watermark"].as<uint64_t>(),
            config.count("fresh"));
    } catch(const std::runtime_error& e) {
        syslog(LOG_ERR, "main: runtime error - %s", e.what());
        return EXIT_FAILURE;
    } catch(const zmq::error_t& e) {
        syslog(LOG_ERR, "main: network error - %s", e.what());
        return EXIT_FAILURE;
    }

    // This call blocks
    core->run();

    // Cleanup
    delete core;
    fs::remove(config["pid"].as<fs::path>());
    syslog(LOG_INFO, "main: yappi has terminated");
    
    return EXIT_SUCCESS;
}
