/*
 * Copyright (c) [2012-2015] Novell, Inc.
 * Copyright (c) [2018-2025] SUSE LLC
 *
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, contact Novell, Inc.
 *
 * To contact Novell about this file by physical or electronic mail, you may
 * find current contact information at www.novell.com.
 */


#include <cstdlib>
#include <syslog.h>
#include <getopt.h>
#include <csignal>
#include <iostream>
#include <string>

#include <snapper/Logger.h>
#include <snapper/Enum.h>
#include <dbus/DBusMainLoop.h>

#include "MetaSnapper.h"
#include "Client.h"
#include "Background.h"
#include "Types.h"


using namespace std;


namespace snapper
{

    template <> struct EnumInfo<LoggerType> { static const std::vector<std::string> names; };

    const vector<string> EnumInfo<LoggerType>::names({ "none", "stdout", "logfile", "syslog" });

}


const seconds idle_time(60);
const seconds snapper_cleanup_time(30);

LoggerType logger_type = LoggerType::LOGFILE;
bool log_debug = false;


class MyMainLoop : public DBus::MainLoop
{
public:

    MyMainLoop(DBusBusType type);
    ~MyMainLoop();

    void method_call(DBus::Message& message) override;
    void signal(DBus::Message& message) override;
    void client_disconnected(const string& name) override;
    void periodic() override;
    milliseconds periodic_timeout() override;

private:

    Backgrounds backgrounds;
    Clients clients;

};


MyMainLoop::MyMainLoop(DBusBusType type)
    : MainLoop(type), clients(backgrounds)
{
}


MyMainLoop::~MyMainLoop()
{
}


void
MyMainLoop::method_call(DBus::Message& msg)
{
    y2deb("method call sender:'" << msg.get_sender() << "' path:'" << msg.get_path() <<
	  "' interface:'" << msg.get_interface() << "' member:'" << msg.get_member() << "'");

    reset_idle_count();

    if (msg.is_method_call(DBUS_INTERFACE_INTROSPECTABLE, "Introspect"))
    {
	Client::introspect(*this, msg);
    }
    else
    {
	boost::unique_lock<boost::shared_mutex> lock(big_mutex);

	const string name = msg.get_sender();

	Clients::iterator client = clients.find(name);
	if (client == clients.end())
	{
	    y2deb("client connected invisible '" << name << "'");
	    add_client_match(name);
	    client = clients.add(name, get_unix_userid(msg));
	    set_idle_timeout(seconds(-1));
	}

	client->add_method_call_task(*this, msg);
    }
}


void
MyMainLoop::signal(DBus::Message& msg)
{
    y2deb("signal sender:'" << msg.get_sender() << "' path:'" << msg.get_path() <<
	  "' interface:'" << msg.get_interface() << "' member:'" << msg.get_member() << "'");
}


void
MyMainLoop::client_disconnected(const string& name)
{
    y2deb("client disconnected '" << name << "'");

    remove_client_match(name);

    boost::unique_lock<boost::shared_mutex> lock(big_mutex);

    Clients::iterator client = clients.find(name);
    if (client != clients.end())
    {
	client->zombie = true;
	client->method_call_thread.interrupt();
	client->files_transfer_thread.interrupt();
    }

    reset_idle_count();
}


void
MyMainLoop::periodic()
{
    boost::unique_lock<boost::shared_mutex> lock(big_mutex);

    clients.remove_zombies();

    if (clients.empty() && backgrounds.empty())
	set_idle_timeout(idle_time);

    for (MetaSnappers::iterator it = meta_snappers.begin(); it != meta_snappers.end(); ++it)
    {
	if (it->is_loaded() && it->unused_for() > snapper_cleanup_time)
	    it->unload();
    }
}


milliseconds
MyMainLoop::periodic_timeout()
{
    boost::unique_lock<boost::shared_mutex> lock(big_mutex);

    if (clients.has_zombies())
	return seconds(1);

    if (!backgrounds.empty())
	return seconds(1);

    for (MetaSnappers::const_iterator it = meta_snappers.begin(); it != meta_snappers.end(); ++it)
	if (it->is_loaded() && it->use_count() == 0)
	    return seconds(1);

    return seconds(-1);
}


void usage() __attribute__ ((__noreturn__));

void
usage()
{
    cerr << "Try 'snapperd --help' for more information." << endl;
    exit(EXIT_FAILURE);
}


void help() __attribute__ ((__noreturn__));

void
help()
{
    cout << "usage: snapperd [--options]" "\n"
	 << "\n";

    cout << "    Options:" "\n"
	 << "\t--logger-type <type>\t\tSet logger type." "\n"
	 << "\t--debug, -d\t\t\tTurn on debugging." "\n"
	 << endl;

    exit(EXIT_SUCCESS);
}


int
main(int argc, char** argv)
{
    const struct option options[] = {
	{ "stdout",		no_argument,		0,	's' },
	{ "logger-type",	required_argument,	0,	'l' },
	{ "debug",		no_argument,		0,	'd' },
	{ "help",		no_argument,		0,	'h' },
	{ 0, 0, 0, 0 }
    };

    while (true)
    {
	int option_index = 0;
	int c = getopt_long(argc, argv, "+sl:dh", options, &option_index);
	if (c == -1)
	    break;

	switch (c)
	{
	    case 's':
		logger_type = LoggerType::STDOUT;
		break;

	    case 'l':
	    {
		if (!toValue(optarg, logger_type, false))
		{
		    cerr << "unknown logger-type" "\n";
		    usage();
		}
	    }
	    break;

	    case 'd':
		log_debug = true;
		break;

	    case 'h':
		help();

	    default:
		usage();
	}
    }

    if (optind < argc)
    {
	cerr << "snapperd: unrecognized option '" << argv[optind] << "'" "\n";
	usage();
    }

    umask(0027);

    switch (logger_type)
    {
	case LoggerType::NONE:
	    break;

	case LoggerType::STDOUT:
	    set_logger(get_stdout_logger());
	    break;

	case LoggerType::LOGFILE:
	    set_logger(get_logfile_logger());
	    break;

	case LoggerType::SYSLOG:
	    set_logger(get_syslog_logger("snapperd", LOG_PID, LOG_DAEMON));
	    break;
    }

    if (log_debug)
	set_logger_tresshold(LogLevel::DEBUG);
    else
	set_logger_tresshold(LogLevel::MILESTONE);

    signal(SIGPIPE, SIG_IGN);

    dbus_threads_init_default();

    MyMainLoop mainloop(DBUS_BUS_SYSTEM);

    mainloop.set_idle_timeout(idle_time);

    y2mil("Requesting DBus name");

    try
    {
	mainloop.request_name(SERVICE, DBUS_NAME_FLAG_REPLACE_EXISTING);
    }
    catch (const Exception& e)
    {
	SN_CAUGHT(e);

	y2err("Failed to request DBus name");

	return EXIT_FAILURE;
    }

    y2mil("Loading snapper configs");

    try
    {
	meta_snappers.init();
    }
    catch (const Exception& e)
    {
	SN_CAUGHT(e);

	y2err("Failed to load snapper configs");
    }

    y2mil("Listening for method calls and signals");

    mainloop.run();

    y2mil("Exiting");

    meta_snappers.unload();

    return EXIT_SUCCESS;
}
