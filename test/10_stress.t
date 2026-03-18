#!/usr/bin/perl

# Suite 10: Stress Tests
# Port range: 9100-9109
# Tests: STRESS-001 through STRESS-005

use warnings;
use strict;

use Test::More;
use File::Basename qw(dirname);
use lib dirname(__FILE__) . '/lib';
use Test::HTTPLite;
use Time::HiRes qw(sleep time);

plan tests => 5;

my $listen_port   = 9100;
my $upstream_port = 9101;

my $t = Test::HTTPLite->new();
$t->run_daemon(\&Test::HTTPLite::echo_daemon, $upstream_port);
$t->waitforsocket("127.0.0.1:$upstream_port");

$t->write_config($listen_port, 10000, "127.0.0.1:${upstream_port}:30");
$t->run($listen_port);

die "nginx failed to start on port $listen_port"
    unless $t->waitforsocket("127.0.0.1:$listen_port", 5);

###############################################################################
# STRESS-001: 100 sequential GET requests
###############################################################################

{
    my $success = 0;
    for my $i (1..100) {
        my $resp = $t->http(
            "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n",
            timeout => 5,
        );
        $success++ if defined $resp && $resp =~ /HTTP\/1\.[01] 200/;
    }
    cmp_ok($success, '>=', 95,
        "STRESS-001: 100 sequential GETs - $success/100 succeeded");
}

###############################################################################
# STRESS-002: 100 sequential POST requests
###############################################################################

{
    my $body = 'X' x 512;
    my $success = 0;
    for my $i (1..100) {
        my $resp = $t->http(
            "POST / HTTP/1.1\r\n"
            . "Host: 127.0.0.1\r\n"
            . "Content-Length: 512\r\n"
            . "Connection: close\r\n"
            . "\r\n"
            . $body,
            timeout => 5,
        );
        $success++ if defined $resp && $resp =~ /HTTP\/1\.[01] 200/;
    }
    cmp_ok($success, '>=', 95,
        "STRESS-002: 100 sequential POSTs - $success/100 succeeded");
}

###############################################################################
# STRESS-003: 50 concurrent requests
###############################################################################

{
    my @children;
    for my $i (1..50) {
        my $pid = fork();
        if ($pid == 0) {
            my $r = $t->http(
                "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n",
                timeout => 10,
            );
            exit(defined $r && $r =~ /HTTP/ ? 0 : 1);
        }
        push @children, $pid;
    }

    my $success = 0;
    for my $pid (@children) {
        waitpid($pid, 0);
        $success++ if ($? >> 8) == 0;
    }

    cmp_ok($success, '>=', 40,
        "STRESS-003: 50 concurrent requests - $success/50 succeeded");
}

###############################################################################
# STRESS-004: Mixed traffic burst (GET + POST interleaved)
###############################################################################

{
    my $success = 0;
    for my $i (1..50) {
        my $resp;
        if ($i % 2 == 0) {
            my $body = 'Y' x 256;
            $resp = $t->http(
                "POST / HTTP/1.1\r\n"
                . "Host: 127.0.0.1\r\n"
                . "Content-Length: 256\r\n"
                . "Connection: close\r\n"
                . "\r\n"
                . $body,
                timeout => 5,
            );
        } else {
            $resp = $t->http(
                "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n",
                timeout => 5,
            );
        }
        $success++ if defined $resp && $resp =~ /HTTP\/1\.[01] 200/;
    }
    cmp_ok($success, '>=', 45,
        "STRESS-004: 50 mixed GET+POST requests - $success/50 succeeded");
}

###############################################################################
# STRESS-005: Sustained load (200 requests over 10 seconds)
###############################################################################

{
    my $success = 0;
    my $start = time();
    for my $i (1..200) {
        my $resp = $t->http(
            "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n",
            timeout => 5,
        );
        $success++ if defined $resp && $resp =~ /HTTP\/1\.[01] 200/;
        # Pace to ~20 req/s
        my $target = $start + ($i * 0.05);
        my $now = time();
        sleep($target - $now) if $target > $now;
    }
    my $alive = kill(0, $t->{pids}[0]);
    ok($alive && $success >= 180,
        "STRESS-005: 200 sustained requests - $success/200 succeeded, nginx alive=$alive");
}
