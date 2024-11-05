/*
 * Copyright (c) 2024 SUSE LLC
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


#include <iostream>
#include <regex>

#include <snapper/AppUtil.h>

#include "../proxy/errors.h"
#include "../utils/text.h"

#include "BackupConfig.h"
#include "GlobalOptions.h"
#include "TheBigThing.h"
#include "cmd.h"


namespace snapper
{

    using namespace std;


    void
    help_transfer()
    {
	cout << "  " << _("Transfer:") << '\n'
	     << "\t" << _("snbk transfer [numbers]") << '\n'
	     << '\n';
    }


    void
    command_transfer(const GlobalOptions& global_options, GetOpts& get_opts, BackupConfigs& backup_configs,
		     ProxySnappers* snappers)
    {
	static const regex num_regex("[0-9]+", regex::extended);

	ParsedOpts opts = get_opts.parse("transfer", GetOpts::no_options);

	vector<unsigned int> nums;

	while (get_opts.has_args())
	{
	    string arg = get_opts.pop_arg();

	    if (!regex_match(arg, num_regex))
		SN_THROW(Exception(_("Failed to parse number.")));

	    nums.push_back(stoi(arg));
	}

	unsigned int errors = 0;

	for (const BackupConfig& backup_config : backup_configs)
	{
	    if (!global_options.quiet())
		cout << sformat(_("Running transfer for backup config '%s'."),
				backup_config.name.c_str()) << endl;

	    try
	    {
		TheBigThings the_big_things(backup_config, snappers, global_options.verbose());

		if (nums.empty())
		{
		    the_big_things.transfer(backup_config, global_options.quiet(), global_options.quiet());
		}
		else
		{
		    for (unsigned int num : nums)
		    {
			TheBigThings::iterator it = the_big_things.find(num);
			if (it == the_big_things.end())
			{
			    string error = sformat(_("Snapshot number %d not found."), num);
			    SN_THROW(Exception(error));
			}

			it->transfer(backup_config, the_big_things, global_options.quiet());
		    }
		}
	    }
	    catch (const DBus::ErrorException& e)
	    {
		SN_CAUGHT(e);

		cerr << error_description(e) << endl;

		++errors;
	    }
	    catch (const Exception& e)
	    {
		SN_CAUGHT(e);

		cerr << e.what() << '\n';

		cerr << sformat(_("Running transfer for backup config '%s' failed."),
				backup_config.name.c_str()) << endl;

		++errors;
	    }
	}

	if (errors != 0)
	{
	    string error = sformat(_("Running transfer failed for %d of %ld backup config.",
				     "Running transfer failed for %d of %ld backup configs.",
				     backup_configs.size()), errors, backup_configs.size());

	    SN_THROW(Exception(error));
	}
    }

}
