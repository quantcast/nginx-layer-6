#!/usr/bin/perl

# Suite 11: Response Validation Tests
# Port range: 9110-9119
# Tests: RESP-001 through RESP-005

use warnings;
use strict;

use Test::More;
use File::Basename qw(dirname);
use lib dirname(__FILE__) . '/lib';
use Test::HTTPLite;
use Time::HiRes qw(sleep);

plan tests => 5;

my $listen_port   = 9110;
my $upstream_port = 9111;

my $t = Test::HTTPLite->new();
$t->run_daemon(\&Test::HTTPLite::echo_daemon, $upstream_port);
$t->waitforsocket("127.0.0.1:$upstream_port");

$t->write_config($listen_port, 10000, "127.0.0.1:${upstream_port}:5");
$t->run($listen_port);

die "nginx failed to start on port $listen_port"
    unless $t->waitforsocket("127.0.0.1:$listen_port", 5);

###############################################################################
# RESP-001: Response contains HTTP status line
###############################################################################

{
    my $resp = $t->http(
        "GET / HTTP/1.1\r\n"
        . "Host: 127.0.0.1\r\n"
        . "Connection: keep-alive\r\n"
        . "\r\n",
        nresponses => 1,
    );
    like($resp, qr{^HTTP/1\.[01] \d{3}},
        'RESP-001: response contains HTTP status line');
}

###############################################################################
# RESP-002: Response contains Content-Length header
###############################################################################

{
    my $resp = $t->http(
        "GET / HTTP/1.1\r\n"
        . "Host: 127.0.0.1\r\n"
        . "Connection: keep-alive\r\n"
        . "\r\n",
        nresponses => 1,
    );
    like($resp, qr/Content-Length:\s*\d+/i,
        'RESP-002: response contains Content-Length header');
}

###############################################################################
# RESP-003: POST echo body matches exactly
###############################################################################

{
    my $body = 'exact_match_test_1234567890';
    my $len  = length $body;
    my $resp = $t->http(
        "POST / HTTP/1.1\r\n"
        . "Host: 127.0.0.1\r\n"
        . "Content-Length: $len\r\n"
        . "Connection: keep-alive\r\n"
        . "\r\n"
        . $body,
        timeout => 5, nresponses => 1,
    );
    like($resp, qr/\Q$body\E/,
        'RESP-003: POST echo body matches exactly');
}

###############################################################################
# RESP-004: POST echo preserves binary-safe content
###############################################################################

{
    # Body with special characters (but no NULs since HTTP body)
    my $body = "line1\tfield2\r\nline2\tfield4\n\x01\x02\x03";
    my $len  = length $body;
    my $resp = $t->http(
        "POST / HTTP/1.1\r\n"
        . "Host: 127.0.0.1\r\n"
        . "Content-Length: $len\r\n"
        . "Connection: keep-alive\r\n"
        . "\r\n"
        . $body,
        timeout => 5, nresponses => 1,
    );
    my $has_body = defined $resp && index($resp, $body) >= 0;
    ok($has_body, 'RESP-004: POST echo preserves special characters');
}

###############################################################################
# RESP-005: Response header terminator present (\r\n\r\n)
###############################################################################

{
    my $resp = $t->http(
        "GET / HTTP/1.1\r\n"
        . "Host: 127.0.0.1\r\n"
        . "Connection: keep-alive\r\n"
        . "\r\n",
        nresponses => 1,
    );
    like($resp, qr/\r\n\r\n/,
        'RESP-005: response contains header terminator (\\r\\n\\r\\n)');
}
