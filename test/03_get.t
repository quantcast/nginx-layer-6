#!/usr/bin/perl

# Suite 03: Basic GET Request Tests
# Port range: 9030-9039
# Tests: GET-001 through GET-004

use warnings;
use strict;

use Test::More;
use File::Basename qw(dirname);
use lib dirname(__FILE__) . '/lib';
use Test::HTTPLite;

plan tests => 4;

my $listen_port   = 9030;
my $upstream_port = 9031;

my $t = Test::HTTPLite->new();
$t->run_daemon(\&Test::HTTPLite::echo_daemon, $upstream_port);
$t->waitforsocket("127.0.0.1:$upstream_port");

$t->write_config($listen_port, 10000, "127.0.0.1:${upstream_port}:5");
$t->run($listen_port);

die "nginx failed to start on port $listen_port"
    unless $t->waitforsocket("127.0.0.1:$listen_port", 5);

###############################################################################
# GET-001: Single GET request
###############################################################################

{
    my $resp = $t->http(
        "GET / HTTP/1.1\r\n"
        . "Host: 127.0.0.1:$listen_port\r\n"
        . "User-Agent: httplite-test/1.0\r\n"
        . "Accept: */*\r\n"
        . "Connection: keep-alive\r\n"
        . "\r\n",
        nresponses => 1,
    );
    like($resp, qr/HTTP\/1\.[01] 200/, 'GET-001: single GET returns 200');
}

###############################################################################
# GET-002: GET with standard headers
###############################################################################

{
    my $resp = $t->http(
        "GET / HTTP/1.1\r\n"
        . "Host: 127.0.0.1:$listen_port\r\n"
        . "User-Agent: httplite-test/1.0\r\n"
        . "Accept: */*\r\n"
        . "Cache-Control: no-cache\r\n"
        . "Connection: keep-alive\r\n"
        . "\r\n",
        nresponses => 1,
    );
    like($resp, qr/HTTP\/1\.[01] 200/,
        'GET-002: GET with standard headers returns 200');
}

###############################################################################
# GET-003: Response body forwarding (POST echo)
###############################################################################

{
    my $resp = $t->http(
        "POST / HTTP/1.1\r\n"
        . "Host: 127.0.0.1:$listen_port\r\n"
        . "User-Agent: httplite-test/1.0\r\n"
        . "Accept: */*\r\n"
        . "Content-Length: 11\r\n"
        . "Content-Type: application/x-www-form-urlencoded\r\n"
        . "Connection: keep-alive\r\n"
        . "\r\n"
        . "testecho123",
        nresponses => 1,
    );
    like($resp, qr/testecho123/,
        'GET-003: upstream echo body forwarded to client');
}

###############################################################################
# GET-004: 10 sequential GETs on separate connections
###############################################################################

{
    my $all_ok = 1;
    for my $i (1..10) {
        my $resp = $t->http(
            "GET / HTTP/1.1\r\n"
            . "Host: 127.0.0.1:$listen_port\r\n"
            . "User-Agent: httplite-test/1.0\r\n"
            . "Accept: */*\r\n"
            . "Connection: keep-alive\r\n"
            . "\r\n",
            nresponses => 1,
        );
        if (!defined $resp || $resp !~ /HTTP\/1\.[01] 200/) {
            $all_ok = 0;
            diag("Failed at request #$i");
            last;
        }
    }
    ok($all_ok, 'GET-004: 10 sequential GETs all return 200');
}
