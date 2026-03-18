#!/usr/bin/perl

# Suite 08: Error Handling Tests
# Port range: 9080-9089
# Tests: ERR-001 through ERR-007

use warnings;
use strict;

use Test::More;
use File::Basename qw(dirname);
use lib dirname(__FILE__) . '/lib';
use Test::HTTPLite;
use Time::HiRes qw(sleep);

plan tests => 7;

my $listen_port   = 9080;
my $upstream_port = 9081;

my $t = Test::HTTPLite->new();
$t->run_daemon(\&Test::HTTPLite::echo_daemon, $upstream_port);
$t->waitforsocket("127.0.0.1:$upstream_port");

$t->write_config($listen_port, 10000, "127.0.0.1:${upstream_port}:5");
$t->run($listen_port);

die "nginx failed to start on port $listen_port"
    unless $t->waitforsocket("127.0.0.1:$listen_port", 5);

###############################################################################
# ERR-001: PUT method
# KNOWN BUG-004: silent failure, no response, client hangs
###############################################################################

{
    my $resp = $t->http(
        "PUT / HTTP/1.1\r\n"
        . "Host: 127.0.0.1\r\n"
        . "Content-Length: 5\r\n"
        . "Connection: close\r\n"
        . "\r\n"
        . "hello",
        timeout => 3,
    );
    my $got_response = defined $resp && $resp ne '';
    ok($got_response, 'ERR-001: PUT method - got a response')
        or diag('KNOWN BUG-004: unsupported method, no error response sent');
}

###############################################################################
# ERR-002: DELETE method
# KNOWN BUG-004
###############################################################################

{
    my $resp = $t->http(
        "DELETE /resource HTTP/1.1\r\n"
        . "Host: 127.0.0.1\r\n"
        . "Connection: close\r\n"
        . "\r\n",
        timeout => 3,
    );
    my $got_response = defined $resp && $resp ne '';
    ok($got_response, 'ERR-002: DELETE method - got a response')
        or diag('KNOWN BUG-004: unsupported method, silent failure');
}

###############################################################################
# ERR-003: HEAD method
# KNOWN BUG-004
###############################################################################

{
    my $resp = $t->http(
        "HEAD / HTTP/1.1\r\n"
        . "Host: 127.0.0.1\r\n"
        . "Connection: close\r\n"
        . "\r\n",
        timeout => 3,
    );
    my $got_response = defined $resp && $resp ne '';
    ok($got_response, 'ERR-003: HEAD method - got a response')
        or diag('KNOWN BUG-004: unsupported method, silent failure');
}

###############################################################################
# ERR-004: Random binary data
###############################################################################

{
    # Generate random binary data
    my $binary = '';
    for (1..500) { $binary .= chr(int(rand(256))); }

    my $resp = $t->http($binary, timeout => 3);
    sleep 0.3;

    my $alive = kill(0, $t->{pids}[0]);
    ok($alive, 'ERR-004: random binary data - nginx survives');
}

###############################################################################
# ERR-005: Empty request (zero bytes)
###############################################################################

{
    # Open connection, send nothing, close
    my $s = IO::Socket::INET->new(
        PeerAddr => '127.0.0.1',
        PeerPort => $listen_port,
        Proto    => 'tcp',
        Timeout  => 2,
    );
    if ($s) {
        sleep 0.5;
        $s->close;
    }
    sleep 0.3;

    my $alive = kill(0, $t->{pids}[0]);
    ok($alive, 'ERR-005: empty request - nginx survives');
}

###############################################################################
# ERR-006: Partial headers (no terminator)
###############################################################################

{
    my $resp = $t->http(
        "GET / HTTP/1.1\r\nHost: 127.0.0.1",
        timeout => 3,
    );
    sleep 0.3;

    my $alive = kill(0, $t->{pids}[0]);
    ok($alive, 'ERR-006: partial headers (no \\r\\n\\r\\n) - nginx survives');
}

###############################################################################
# ERR-007: Duplicate Content-Length
###############################################################################

{
    my $resp = $t->http(
        "POST / HTTP/1.1\r\n"
        . "Host: 127.0.0.1\r\n"
        . "Content-Length: 5\r\n"
        . "Content-Length: 100\r\n"
        . "Connection: close\r\n"
        . "\r\n"
        . "hello",
        timeout => 5,
    );
    sleep 0.3;

    my $alive = kill(0, $t->{pids}[0]);
    ok($alive, 'ERR-007: duplicate Content-Length - nginx survives');
}
