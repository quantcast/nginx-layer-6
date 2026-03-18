#!/usr/bin/perl

# Suite 09: Edge Case Tests
# Port range: 9090-9099
# Tests: EDGE-001 through EDGE-009

use warnings;
use strict;

use Test::More;
use File::Basename qw(dirname);
use lib dirname(__FILE__) . '/lib';
use Test::HTTPLite;
use Time::HiRes qw(sleep);

plan tests => 9;

my $listen_port   = 9090;
my $upstream_port = 9091;

my $t = Test::HTTPLite->new();
$t->run_daemon(\&Test::HTTPLite::echo_daemon, $upstream_port);
$t->waitforsocket("127.0.0.1:$upstream_port");

$t->write_config($listen_port, 10000, "127.0.0.1:${upstream_port}:5");
$t->run($listen_port);

die "nginx failed to start on port $listen_port"
    unless $t->waitforsocket("127.0.0.1:$listen_port", 5);

###############################################################################
# EDGE-001: Very long URI (8000 bytes)
###############################################################################

{
    my $long_path = '/' . ('x' x 7999);
    my $resp = $t->http(
        "GET $long_path HTTP/1.1\r\n"
        . "Host: 127.0.0.1\r\n"
        . "Connection: keep-alive\r\n"
        . "\r\n",
        timeout => 5,
    );
    sleep 0.3;
    my $alive = kill(0, $t->{pids}[0]);
    ok($alive, 'EDGE-001: very long URI (8000 bytes) - nginx survives');
}

###############################################################################
# EDGE-002: Many headers (50 headers)
###############################################################################

{
    my $headers = '';
    for my $i (1..50) {
        $headers .= "X-Custom-Header-$i: value-$i\r\n";
    }
    my $resp = $t->http(
        "GET / HTTP/1.1\r\n"
        . "Host: 127.0.0.1\r\n"
        . $headers
        . "Connection: keep-alive\r\n"
        . "\r\n",
        timeout => 5, nresponses => 1,
    );
    my $ok = defined $resp && $resp =~ /HTTP\/1\.[01] [2-5]/;
    ok($ok, 'EDGE-002: 50 custom headers - handled');
}

###############################################################################
# EDGE-003: Header with very long value (4000 bytes)
###############################################################################

{
    my $long_val = 'V' x 4000;
    my $resp = $t->http(
        "GET / HTTP/1.1\r\n"
        . "Host: 127.0.0.1\r\n"
        . "X-Long: $long_val\r\n"
        . "Connection: keep-alive\r\n"
        . "\r\n",
        timeout => 5,
    );
    sleep 0.3;
    my $alive = kill(0, $t->{pids}[0]);
    ok($alive, 'EDGE-003: header with 4000-byte value - nginx survives');
}

###############################################################################
# EDGE-004: HTTP/1.0 request
###############################################################################

{
    my $resp = $t->http(
        "GET / HTTP/1.0\r\n"
        . "Host: 127.0.0.1\r\n"
        . "\r\n",
        timeout => 5,
    );
    my $got_response = defined $resp && $resp =~ /HTTP/;
    ok($got_response, 'EDGE-004: HTTP/1.0 request - got a response');
}

###############################################################################
# EDGE-005: Request with no Host header
###############################################################################

{
    my $resp = $t->http(
        "GET / HTTP/1.1\r\n"
        . "Connection: keep-alive\r\n"
        . "\r\n",
        timeout => 5,
    );
    sleep 0.3;
    my $alive = kill(0, $t->{pids}[0]);
    ok($alive, 'EDGE-005: request with no Host header - nginx survives');
}

###############################################################################
# EDGE-006: POST body exactly 1500 bytes (SLAB boundary)
###############################################################################

{
    my $body = 'S' x 1500;
    my $resp = $t->http(
        "POST / HTTP/1.1\r\n"
        . "Host: 127.0.0.1\r\n"
        . "Content-Length: 1500\r\n"
        . "Connection: keep-alive\r\n"
        . "\r\n"
        . $body,
        timeout => 10, nresponses => 1,
    );
    like($resp, qr/HTTP\/1\.[01] 200/,
        'EDGE-006: POST body exactly 1500 bytes (SLAB boundary) - 200 response');
}

###############################################################################
# EDGE-007: POST body exactly 1499 bytes (one less than SLAB)
###############################################################################

{
    my $body = 'T' x 1499;
    my $resp = $t->http(
        "POST / HTTP/1.1\r\n"
        . "Host: 127.0.0.1\r\n"
        . "Content-Length: 1499\r\n"
        . "Connection: keep-alive\r\n"
        . "\r\n"
        . $body,
        timeout => 10, nresponses => 1,
    );
    like($resp, qr/HTTP\/1\.[01] 200/,
        'EDGE-007: POST body exactly 1499 bytes - 200 response');
}

###############################################################################
# EDGE-008: POST body exactly 1501 bytes (one more than SLAB)
###############################################################################

{
    my $body = 'U' x 1501;
    my $resp = $t->http(
        "POST / HTTP/1.1\r\n"
        . "Host: 127.0.0.1\r\n"
        . "Content-Length: 1501\r\n"
        . "Connection: keep-alive\r\n"
        . "\r\n"
        . $body,
        timeout => 10, nresponses => 1,
    );
    like($resp, qr/HTTP\/1\.[01] 200/,
        'EDGE-008: POST body exactly 1501 bytes (crosses SLAB boundary) - 200 response');
}

###############################################################################
# EDGE-009: Rapid connect/disconnect (100 connections)
###############################################################################

{
    for my $i (1..100) {
        my $s = IO::Socket::INET->new(
            PeerAddr => '127.0.0.1',
            PeerPort => $listen_port,
            Proto    => 'tcp',
            Timeout  => 1,
        );
        $s->close if $s;
    }
    sleep 0.5;

    my $alive = kill(0, $t->{pids}[0]);
    ok($alive, 'EDGE-009: 100 rapid connect/disconnect cycles - nginx survives');
}
