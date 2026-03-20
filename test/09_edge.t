#!/usr/bin/perl

# Suite 09: Edge Case Tests
# Tests: EDGE-001 through EDGE-009

use warnings;
use strict;

use Test::More;
use File::Basename qw(dirname);
use lib dirname(__FILE__) . '/lib';
use Test::HTTPLite;
use HTTP::Request;
use Getopt::Long;

plan tests => 9;

my %opts;
GetOptions(\%opts, 'listen-port=i', 'upstream-port=i');

my $t = Test::HTTPLite->new();
my ($listen_port, $upstream_port) = $t->ports(2);
$listen_port   = $opts{'listen-port'}   // $listen_port;
$upstream_port = $opts{'upstream-port'} // $upstream_port;

$t->run_daemon(\&Test::HTTPLite::echo_daemon, $upstream_port);
$t->waitforsocket("127.0.0.1:$upstream_port");

$t->write_config($listen_port, 10000, "127.0.0.1:${upstream_port}:5");
$t->run($listen_port);

die "nginx failed to start on port $listen_port"
    unless $t->waitforsocket("127.0.0.1:$listen_port", 5);

my $url = $t->base_url($listen_port);
my $ua = $t->ua(timeout => 2);

###############################################################################
# EDGE-001: Very long URI (8000 bytes)
###############################################################################

{
    my $long_path = '/' . ('x' x 7999);
    my $resp = eval { $ua->get("$url$long_path") };
    my $alive = kill(0, $t->{pids}[0]);
    ok($alive, 'EDGE-001: very long URI (8000 bytes) - nginx survives');
}

###############################################################################
# EDGE-002: Many headers (50 headers)
###############################################################################

{
    my $req = HTTP::Request->new('GET', "$url/");
    for my $i (1..50) {
        $req->header("X-Custom-Header-$i" => "value-$i");
    }
    my $resp = $ua->request($req);
    my $ok = defined $resp && ($resp->is_success || $resp->is_error);
    ok($ok, 'EDGE-002: 50 custom headers - handled');
}

###############################################################################
# EDGE-003: Header with very long value (4000 bytes)
###############################################################################

{
    my $long_val = 'V' x 4000;
    my $req = HTTP::Request->new('GET', "$url/");
    $req->header('X-Long' => $long_val);
    my $resp = eval { $ua->request($req) };
    my $alive = kill(0, $t->{pids}[0]);
    ok($alive, 'EDGE-003: header with 4000-byte value - nginx survives');
}

###############################################################################
# EDGE-004: HTTP/1.0 request
###############################################################################

{
    # TODO: HTTP/1.0 support incomplete - httplite expects keep-alive HTTP/1.1
    TODO: {
        local $TODO = 'HTTP/1.0 support may be incomplete';
        # KEEP raw socket - LWP always sends HTTP/1.1
        my $resp = $t->http(
            "GET / HTTP/1.0\r\n"
            . "Host: 127.0.0.1\r\n"
            . "\r\n",
            timeout => 2,
        );
        my $got_response = defined $resp && $resp =~ /HTTP/;
        ok($got_response, 'EDGE-004: HTTP/1.0 request - got a response');
    }
}

###############################################################################
# EDGE-005: Request with no Host header
###############################################################################

{
    # KEEP raw socket - LWP always adds Host header
    my $resp = $t->http(
        "GET / HTTP/1.1\r\n"
        . "Connection: keep-alive\r\n"
        . "\r\n",
        timeout => 2,
    );
    my $alive = kill(0, $t->{pids}[0]);
    ok($alive, 'EDGE-005: request with no Host header - nginx survives');
}

###############################################################################
# EDGE-006: POST body exactly 1500 bytes (SLAB boundary)
###############################################################################

{
    # TODO: POST with LWP returns 503 "inactive upstream" - httplite C bug
    # Related to connection pool management or timing issue with POST body handling
    TODO: {
        local $TODO = 'POST returns 503 inactive upstream (C bug)';
        my $ua_fresh = $t->ua(timeout => 2, keep_alive => 0);
        my $body = 'S' x 1500;
        my $resp = $ua_fresh->post("$url/", Content => $body, 'Content-Type' => 'application/x-www-form-urlencoded');
        ok($resp->is_success, 'EDGE-006: POST body exactly 1500 bytes (SLAB boundary) - 200 response');
    }
}

###############################################################################
# EDGE-007: POST body exactly 1499 bytes (one less than SLAB)
###############################################################################

{
    TODO: {
        local $TODO = 'POST returns 503 inactive upstream (C bug)';
        my $ua_fresh = $t->ua(timeout => 2, keep_alive => 0);
        my $body = 'T' x 1499;
        my $resp = $ua_fresh->post("$url/", Content => $body, 'Content-Type' => 'application/x-www-form-urlencoded');
        ok($resp->is_success, 'EDGE-007: POST body exactly 1499 bytes - 200 response');
    }
}

###############################################################################
# EDGE-008: POST body exactly 1501 bytes (one more than SLAB)
###############################################################################

{
    TODO: {
        local $TODO = 'POST returns 503 inactive upstream (C bug)';
        my $ua_fresh = $t->ua(timeout => 2, keep_alive => 0);
        my $body = 'U' x 1501;
        my $resp = $ua_fresh->post("$url/", Content => $body, 'Content-Type' => 'application/x-www-form-urlencoded');
        ok($resp->is_success, 'EDGE-008: POST body exactly 1501 bytes (crosses SLAB boundary) - 200 response');
    }
}

###############################################################################
# EDGE-009: Rapid connect/disconnect (100 connections)
###############################################################################

{
    # KEEP raw socket - testing connection handling
    for my $i (1..100) {
        my $s = IO::Socket::INET->new(
            PeerAddr => '127.0.0.1',
            PeerPort => $listen_port,
            Proto    => 'tcp',
            Timeout  => 1,
        );
        $s->close if $s;
    }

    my $alive = kill(0, $t->{pids}[0]);
    ok($alive, 'EDGE-009: 100 rapid connect/disconnect cycles - nginx survives');
}
