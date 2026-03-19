#!/usr/bin/perl

# Suite 01: Build & Startup Tests
# Tests: BUILD-001, BUILD-002, BUILD-003

use warnings;
use strict;

use Test::More;
use File::Basename qw(dirname);
use lib dirname(__FILE__) . '/lib';
use Test::HTTPLite;
use Getopt::Long;

plan tests => 3;

my %opts;
GetOptions(\%opts, 'listen-port=i', 'upstream-port=i');

my $t = Test::HTTPLite->new();
my ($listen_port, $upstream_port) = $t->ports(2);
$listen_port   = $opts{'listen-port'}   // $listen_port;
$upstream_port = $opts{'upstream-port'} // $upstream_port;

###############################################################################
# BUILD-001: nginx binary exists and is executable
###############################################################################

ok(-x $t->{binary}, 'BUILD-001: nginx binary exists and is executable');

###############################################################################
# BUILD-002: nginx starts with httplite config
###############################################################################

$t->run_daemon(\&Test::HTTPLite::echo_daemon, $upstream_port);
$t->waitforsocket("127.0.0.1:$upstream_port");

$t->write_config($listen_port, 10000, "127.0.0.1:${upstream_port}:5");
$t->run($listen_port);

ok($t->waitforsocket("127.0.0.1:$listen_port", 5),
    'BUILD-002: nginx starts and listens on configured port');

###############################################################################
# BUILD-003: nginx exits cleanly on SIGQUIT
###############################################################################

SKIP: {
    skip 'nginx not running', 1 unless $t->waitforsocket("127.0.0.1:$listen_port", 1);

    my $pid = $t->{pids}[0];
    kill POSIX::SIGQUIT, $pid;

    # Wait up to 3s for clean exit
    my $exited = 0;
    for (1..60) {
        if (waitpid($pid, POSIX::WNOHANG) != 0) {
            $exited = 1;
            last;
        }
        select(undef, undef, undef, 0.05);
    }

    if ($exited) {
        my $log = $t->read_file('error.log') // '';
        my $segfault = ($log =~ /segfault|signal 11|SIGSEGV/i);
        ok(!$segfault, 'BUILD-003: nginx exits cleanly on SIGQUIT (no segfault)');
    } else {
        kill POSIX::SIGKILL, $pid;
        fail('BUILD-003: nginx did not exit within 3s of SIGQUIT');
    }

    # Remove from pids since we handled it
    $t->{pids} = [ grep { $_ != $pid } @{$t->{pids}} ];
}
