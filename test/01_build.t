#!/usr/bin/perl

# Suite 01: Build & Startup Tests
# Port range: 9010-9019
# Tests: BUILD-001, BUILD-002, BUILD-003

use warnings;
use strict;

use Test::More;
use File::Basename qw(dirname);
use lib dirname(__FILE__) . '/lib';
use Test::HTTPLite;

plan tests => 3;

my $t = Test::HTTPLite->new();
my $upstream_port = 9011;

###############################################################################
# BUILD-001: nginx binary exists and is executable
###############################################################################

ok(-x $t->{binary}, 'BUILD-001: nginx binary exists and is executable');

###############################################################################
# BUILD-002: nginx starts with httplite config
###############################################################################

$t->run_daemon(\&Test::HTTPLite::echo_daemon, $upstream_port);
$t->waitforsocket("127.0.0.1:$upstream_port");

$t->write_config(9010, 10000, "127.0.0.1:${upstream_port}:5");
$t->run(9010);

ok($t->waitforsocket('127.0.0.1:9010', 5),
    'BUILD-002: nginx starts and listens on port 9010');

###############################################################################
# BUILD-003: nginx exits cleanly on SIGQUIT
###############################################################################

SKIP: {
    skip 'nginx not running', 1 unless $t->waitforsocket('127.0.0.1:9010', 1);

    my $pid = $t->{pids}[0];
    kill POSIX::SIGQUIT, $pid;

    # Wait up to 3s for clean exit
    my $exited = 0;
    for (1..60) {
        if (waitpid($pid, POSIX::WNOHANG) != 0) {
            $exited = 1;
            last;
        }
        Time::HiRes::sleep(0.05);
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
