#!/usr/bin/perl

# Suite 04: Basic POST Request Tests
# Tests: POST-001 through POST-008

use warnings;
use strict;

use Test::More;
use File::Basename qw(dirname);
use lib dirname(__FILE__) . '/lib';
use Test::HTTPLite;
use Getopt::Long;

plan tests => 8;

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

###############################################################################
# POST-001: Small POST body (5 bytes)
###############################################################################

{
    my $ua  = $t->ua();
    my $url = $t->base_url();
    my $resp = $ua->post("$url/", Content => 'hello', 'Content-Type' => 'application/x-www-form-urlencoded');
    ok($resp->is_success && $resp->decoded_content =~ /hello/,
        'POST-001: small POST body (5 bytes) echoed back');
}

###############################################################################
# POST-002: POST body 1024 bytes
###############################################################################

{
    my $ua   = $t->ua();
    my $url  = $t->base_url();
    my $body = 'A' x 1024;
    my $resp = $ua->post("$url/", Content => $body, 'Content-Type' => 'application/x-www-form-urlencoded');
    ok($resp->is_success, 'POST-002: POST body 1024 bytes - 200 response');
}

###############################################################################
# POST-003: POST body spanning multiple SLABs (3000 bytes)
# BUG-POST003: 503 "Trying to send to an inactive upstream" for large bodies
###############################################################################

{
    my $ua   = $t->ua(timeout => 10);
    my $url  = $t->base_url();
    my $body = 'B' x 3000;
    my $resp = $ua->post("$url/", Content => $body, 'Content-Type' => 'application/x-www-form-urlencoded');

TODO: {
    local $TODO = 'BUG-POST003: Multi-SLAB POST bodies cause upstream connection issues';
    ok($resp->is_success,
        'POST-003: POST body 3000 bytes (multi-SLAB) - 200 response');
}
}

###############################################################################
# POST-004: POST without Content-Length
# KNOWN BUG-008: c->send() return not checked
###############################################################################

{
    my $resp = $t->http(
        "POST / HTTP/1.1\r\n"
        . "Host: 127.0.0.1\r\n"
        . "User-Agent: test\r\n"
        . "Accept: */*\r\n"
        . "Connection: keep-alive\r\n"
        . "\r\n"
        . "hello",
        timeout => 5, nresponses => 1,
    );
    my $got_411 = defined $resp && $resp =~ /HTTP\/1\.[01] 411/;
    ok($got_411, 'POST-004: POST without Content-Length returns 411')
        or diag('KNOWN BUG-008: 411 may not have been delivered. '
                . 'Got: ' . (defined $resp ? substr($resp, 0, 80) : '<undef>'));
}

###############################################################################
# POST-005: POST with Content-Length: -1
###############################################################################

{
    my $resp = $t->http(
        "POST / HTTP/1.1\r\n"
        . "Host: 127.0.0.1\r\n"
        . "Content-Length: -1\r\n"
        . "Connection: keep-alive\r\n"
        . "\r\n"
        . "hello",
        timeout => 3, nresponses => 1,
    );
    my $handled = !defined $resp
        || $resp eq ''
        || $resp =~ /HTTP\/1\.[01] [45]/;
    ok($handled,
        'POST-005: POST with Content-Length: -1 - handled (closed or error)');
}

###############################################################################
# POST-006: POST with Content-Length: abc
###############################################################################

{
    my $resp = $t->http(
        "POST / HTTP/1.1\r\n"
        . "Host: 127.0.0.1\r\n"
        . "Content-Length: abc\r\n"
        . "Connection: keep-alive\r\n"
        . "\r\n"
        . "hello",
        timeout => 3, nresponses => 1,
    );
    my $handled = !defined $resp
        || $resp eq ''
        || $resp =~ /HTTP\/1\.[01] [45]/;
    ok($handled,
        'POST-006: POST with Content-Length: abc - handled');
}

###############################################################################
# POST-007: POST with Content-Length: 0
# BUG-POST007: Server hangs on zero-length POST body, no response sent
###############################################################################

{
    my $ua   = $t->ua(timeout => 3);
    my $url  = $t->base_url();
    my $resp = $ua->post("$url/", Content => '', 'Content-Type' => 'application/x-www-form-urlencoded');

TODO: {
    local $TODO = 'BUG-POST007: Zero-length POST body causes timeout';
    ok($resp->is_success,
        'POST-007: POST with Content-Length: 0 - 200 response');
}
}

###############################################################################
# POST-008: POST with Content-Length > actual body
###############################################################################

{
    my $resp = $t->http(
        "POST / HTTP/1.1\r\n"
        . "Host: 127.0.0.1\r\n"
        . "Content-Length: 1000\r\n"
        . "Connection: keep-alive\r\n"
        . "\r\n"
        . "0123456789",
        timeout => 3,
    );
    # Server should wait for more body; with short timeout we get nothing
    my $no_response = !defined $resp || $resp eq '';
    ok($no_response,
        'POST-008: POST with Content-Length > actual body - server waits (timeout)');
}
