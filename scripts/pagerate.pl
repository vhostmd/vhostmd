#!/usr/bin/perl
# Copyright (c) 2009 Novell, Inc.  All rights reserved.
#
# Author: Pat Campbell <plc@novell.com>
# pagerate.pl:  Prints predefined set of disk page rates 
#               Set currently consists of:
#                    Page In Rate 
#                    Page Fault Rate
#
use strict;

my %vmstat;               # holds current copy of vmstat
my $pgpgin1 = 0;
my $pgpgin2 = 0;
my $pgpgout1 = 0;
my $pgpgout2 = 0;


# Get vmstat into an associative array
sub getvmstat
{
    open(VMSTAT, "< /proc/vmstat") || die "no /proc/vmstat";
    while(<VMSTAT>) {
        /^(.*)\s+(\d+)$/;
        $vmstat{$1} = $2;
    }
}

getvmstat();
$pgpgin1 = $vmstat{'pgpgin'};
$pgpgout1 = $vmstat{'pgpgout'};

sleep(1);

getvmstat();
$pgpgin2 = $vmstat{'pgpgin'};
$pgpgout2 = $vmstat{'pgpgout'};

printf("%f,%f", $pgpgin2 - $pgpgin1, $pgpgout2 - $pgpgout1);
