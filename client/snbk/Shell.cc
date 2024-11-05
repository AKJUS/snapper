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


#include "snapper/SnapperDefines.h"
#include "snapper/Exception.h"

#include "Shell.h"


namespace snapper
{

    namespace
    {

	// TODO move to libsnapper?

	string
	quote(const vector<string>& strs)
	{
	    string ret;

	    for (vector<string>::const_iterator it = strs.begin(); it != strs.end(); ++it)
	    {
		if (it != strs.begin())
		    ret.append(" ");
		ret.append(SystemCmd::quote(*it));
	    }

	    return ret;
	}


	string
	quote(const SystemCmd::Args& args)
	{
	    return quote(args.get_values());
	}

    }


    SystemCmd::Args
    shellify(const Shell& shell, const SystemCmd::Args& args1)
    {
	switch (shell.mode)
	{
	    case Shell::Mode::DIRECT:
	    {
		return args1;
	    }

	    case Shell::Mode::SSH:
	    {
		SystemCmd::Args args2 = { SSH_BIN };
		args2 << shell.ssh_options << quote(args1);
		return args2;
	    }
	}

	SN_THROW(Exception("invalid shell mode"));
	__builtin_unreachable();
    }


    SystemCmd::Args
    shellify_pipe(const SystemCmd::Args& args1, const Shell& shell2, const SystemCmd::Args& args2)
    {
	string tmp1 = quote(args1);
	string tmp2 = quote(args2);

	switch (shell2.mode)
	{
	    case Shell::Mode::DIRECT:
	    {
		return SystemCmd::Args { SH_BIN, "-c", tmp1 + " | " + tmp2 };
	    }

	    case Shell::Mode::SSH:
	    {
		return SystemCmd::Args { SH_BIN, "-c", tmp1 + " | " + SSH_BIN " " +
		    quote(shell2.ssh_options) + " " + quote(tmp2) };
	    }
	}

	SN_THROW(Exception("invalid shell mode"));
	__builtin_unreachable();
    }

}
