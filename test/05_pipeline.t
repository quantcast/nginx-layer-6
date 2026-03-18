#!/usr/bin/perl

# Suite 05: HTTP Pipelining Tests
# Port range: 9050-9059
# Tests: PIPE-001 through PIPE-008

use warnings;
use strict;

use Test::More;
use File::Basename qw(dirname);
use lib dirname(__FILE__) . '/lib';
use Test::HTTPLite;
use Time::HiRes qw(sleep);

plan tests => 8;

my $listen_port   = 9050;
my $upstream_port = 9051;

my $t = Test::HTTPLite->new();
$t->run_daemon(\&Test::HTTPLite::echo_daemon, $upstream_port);
$t->waitforsocket("127.0.0.1:$upstream_port");

$t->write_config($listen_port, 10000, "127.0.0.1:${upstream_port}:30");
$t->run($listen_port);

die "nginx failed to start on port $listen_port"
    unless $t->waitforsocket("127.0.0.1:$listen_port", 5);

# --- Helpers ---

sub build_pipelined_gets {
    my ($count) = @_;
    my $req = '';
    for (1..$count) {
        $req .= "GET / HTTP/1.1\r\n"
              . "Host: 127.0.0.1\r\n"
              . "User-Agent: pipe-test\r\n"
              . "Accept: */*\r\n"
              . "Connection: keep-alive\r\n"
              . "\r\n";
    }
    return $req;
}

sub build_pipelined_posts {
    my ($count) = @_;
    my $req = '';
    for my $i (1..$count) {
        my $body = "body${i}";
        my $len  = length $body;
        $req .= "POST / HTTP/1.1\r\n"
              . "Host: 127.0.0.1\r\n"
              . "User-Agent: pipe-test\r\n"
              . "Accept: */*\r\n"
              . "Content-Length: ${len}\r\n"
              . "Content-Type: application/x-www-form-urlencoded\r\n"
              . "Connection: keep-alive\r\n"
              . "\r\n"
              . $body;
    }
    return $req;
}

sub count_responses {
    my ($data) = @_;
    return 0 unless defined $data;
    my @matches = ($data =~ /HTTP\/1\.[01] \d+/g);
    return scalar @matches;
}

###############################################################################
# PIPE-001: 3 pipelined GETs
###############################################################################

{
    my $resp = $t->http(build_pipelined_gets(3), timeout => 5);
    my $count = count_responses($resp);
    is($count, 3, 'PIPE-001: 3 pipelined GETs - got 3 responses');
}

###############################################################################
# PIPE-002: 10 pipelined GETs
###############################################################################

{
    my $resp = $t->http(build_pipelined_gets(10), timeout => 10);
    my $count = count_responses($resp);
    is($count, 10, 'PIPE-002: 10 pipelined GETs - got 10 responses');
}

###############################################################################
# PIPE-003: 100 pipelined GETs
###############################################################################

{
    my $resp = $t->http(build_pipelined_gets(100), timeout => 30);
    my $count = count_responses($resp);
    is($count, 100, 'PIPE-003: 100 pipelined GETs - got 100 responses');
}

###############################################################################
# PIPE-004: 1000 pipelined GETs
###############################################################################

{
    my $resp = $t->http(build_pipelined_gets(1000), timeout => 60);
    my $count = count_responses($resp);
    is($count, 1000, 'PIPE-004: 1000 pipelined GETs - got 1000 responses');
}

###############################################################################
# PIPE-005: 3 pipelined POSTs
###############################################################################

{
    my $resp = $t->http(build_pipelined_posts(3), timeout => 5);
    my $count = count_responses($resp);
    is($count, 3, 'PIPE-005: 3 pipelined POSTs - got 3 responses');
}

###############################################################################
# PIPE-006: 10 pipelined POSTs
###############################################################################

{
    my $resp = $t->http(build_pipelined_posts(10), timeout => 10);
    my $count = count_responses($resp);
    is($count, 10, 'PIPE-006: 10 pipelined POSTs - got 10 responses');
}

###############################################################################
# PIPE-007: Mixed GET+POST pipeline
###############################################################################

{
    my $req = "GET / HTTP/1.1\r\n"
            . "Host: 127.0.0.1\r\n"
            . "User-Agent: pipe-test\r\n"
            . "Accept: */*\r\n"
            . "Connection: keep-alive\r\n"
            . "\r\n"
            . "POST / HTTP/1.1\r\n"
            . "Host: 127.0.0.1\r\n"
            . "User-Agent: pipe-test\r\n"
            . "Accept: */*\r\n"
            . "Content-Length: 9\r\n"
            . "Content-Type: application/x-www-form-urlencoded\r\n"
            . "Connection: keep-alive\r\n"
            . "\r\n"
            . "mixedtest"
            . "GET / HTTP/1.1\r\n"
            . "Host: 127.0.0.1\r\n"
            . "User-Agent: pipe-test\r\n"
            . "Accept: */*\r\n"
            . "Connection: keep-alive\r\n"
            . "\r\n";

    my $resp = $t->http($req, timeout => 5);
    my $count = count_responses($resp);
    my $has_body = defined $resp && $resp =~ /mixedtest/;
    ok($count == 3 && $has_body,
        'PIPE-007: mixed GET+POST pipeline - 3 responses with correct body')
        or diag("count=$count, has_body=$has_body");
}

###############################################################################
# PIPE-008: Pipelined with inter-segment delays
###############################################################################

{
    # Send 2 requests with a 200ms gap between them on the same connection
    my $s = $t->http_start(
        "GET / HTTP/1.1\r\n"
        . "Host: 127.0.0.1\r\n"
        . "User-Agent: pipe-test\r\n"
        . "Accept: */*\r\n"
        . "Connection: keep-alive\r\n"
        . "\r\n",
    );

    sleep 0.2;

    $s->print(
        "GET / HTTP/1.1\r\n"
        . "Host: 127.0.0.1\r\n"
        . "User-Agent: pipe-test\r\n"
        . "Accept: */*\r\n"
        . "Connection: keep-alive\r\n"
        . "\r\n",
    );

    my $resp = $t->http_end($s, timeout => 5);
    my $count = count_responses($resp);
    is($count, 2,
        'PIPE-008: pipelined with inter-segment delay - 2 responses');
}
