#!/usr/bin/env perl
# SPDX-License-Identifier: GPL-2.0-only
# GitBSLR is available under the same license as Git itself.

#this thing's sole purpose is to fail if GitBSLR is loaded into the process

chdir ".";
readlink("/dev/null");
open(my $fh, '>', $ARGV[0]);
print $fh "GitBSLR test\n";
