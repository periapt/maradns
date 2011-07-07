#!/usr/bin/perl
use strict;
use warnings;
use POSIX qw(setuid setgid);

# You may not see anything in syslog without this.
$| |= 1;

use AnyEvent;
use IO::Async::Loop;
use DateTime;
use AnyEvent::Socket;
use Readonly;

my $cv = AnyEvent->condvar;

my $timer = AnyEvent->timer(
    after => 0,
    interval=>61, # one minute would be tooo boring
    cb => sub {
        my $time = DateTime->now;
        print "Anyone there? $time\n";
        $cv->send
    },
);

my $usr1 = AnyEvent->signal(
    signal=>'USR1',
    cb => sub {
        my $time = DateTime->now;
        print "Ouch! $time\n";
    },
);

my $usr2 = AnyEvent->signal(
    signal=>'USR2',
    cb => sub {
        my $time = DateTime->now;
        print "Stop that or I will blow your computer up! $time\n";
    },
);

my $hup = AnyEvent->signal(
    signal=>'HUP',
    cb => sub {
        exit 42;
    },
);

my $listen = tcp_server(
    undef, # localhost
    8888, # port
    sub {
        my ($fh, $host, $port) = @_;
        my $time = DateTime->now;
        print "Message from $host:$port at $time\n";
        syswrite $fh, "Thanks. I am taking your request really, really seriously. Honest!\015\012";
    },
);

# Waiting to the last moment before entering a chroot
# environment is a cheating way of avoiding the restrictions
# of running in a chroot environment.
Readonly my $CHROOT => '/usr/share/doc/duende';
chdir $CHROOT;
chroot $CHROOT;
setpgrp;
setgid 1;
setuid 1;

AnyEvent->condvar->wait;

