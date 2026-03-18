#!/usr/bin/perl

# Suite 06: Keep-Alive Connection Tests
# Port range: 9060-9069
# Tests: KA-001 through KA-003

use warnings;
use strict;

use Test::More;
use File::Basename qw(dirname);
use lib dirname(__FILE__) . '/lib';
use Test::HTTPLite;
use Time::HiRes qw(sleep);
use IO::Socket::INET;

plan tests => 3;

my $upstream_port = 9061;

my $t = Test::HTTPLite->new();
$t->run_daemon(\&Test::HTTPLite::echo_daemon, $upstream_port);
$t->waitforsocket("127.0.0.1:$upstream_port");

###############################################################################
# KA-001: Reuse within keep-alive window
# 10s keep-alive, send 2 requests 1s apart on same connection
###############################################################################

{
    my $t1 = Test::HTTPLite->new();
    $t1->write_config(9060, 10000, "127.0.0.1:${upstream_port}:5");
    $t1->run(9060);

    die "KA-001 SETUP: nginx failed to start"
        unless $t1->waitforsocket('127.0.0.1:9060', 5);

    my $s = $t1->http_start(
        "GET / HTTP/1.1\r\n"
        . "Host: 127.0.0.1\r\n"
        . "User-Agent: ka-test\r\n"
        . "Accept: */*\r\n"
        . "Connection: keep-alive\r\n"
        . "\r\n",
    );

    sleep 1;

    $s->print(
        "GET / HTTP/1.1\r\n"
        . "Host: 127.0.0.1\r\n"
        . "User-Agent: ka-test\r\n"
        . "Accept: */*\r\n"
        . "Connection: keep-alive\r\n"
        . "\r\n",
    );

    my $resp = $t1->http_end($s, timeout => 10);
    my @matches = defined $resp ? ($resp =~ /HTTP\/1\.[01] \d+/g) : ();
    is(scalar @matches, 2,
        'KA-001: 2 requests within keep-alive window - both succeed');
}

###############################################################################
# KA-002: Timeout after keep-alive expiry
# 2s keep-alive, send request, wait 3s, send another on new connection
###############################################################################

{
    my $t2 = Test::HTTPLite->new();
    $t2->write_config(9062, 2000, "127.0.0.1:${upstream_port}:5");
    $t2->run(9062);

    if (!$t2->waitforsocket('127.0.0.1:9062', 5)) {
        fail('KA-002: nginx failed to start');
    } else {
        my $resp1 = $t2->http(
            "GET / HTTP/1.1\r\n"
            . "Host: 127.0.0.1\r\n"
            . "Connection: close\r\n"
            . "\r\n",
            port => 9062,
        );
        my $ok1 = defined $resp1 && $resp1 =~ /HTTP\/1\.[01] 200/;

        # Wait for keep-alive to expire
        sleep 3;

        my $resp2 = $t2->http(
            "GET / HTTP/1.1\r\n"
            . "Host: 127.0.0.1\r\n"
            . "Connection: close\r\n"
            . "\r\n",
            port => 9062,
        );
        my $ok2 = defined $resp2 && $resp2 =~ /HTTP\/1\.[01] 200/;

        ok($ok1 && $ok2,
            'KA-002: request after keep-alive expiry - new upstream connection works')
            or diag("first=$ok1, second=$ok2");
    }
}

###############################################################################
# KA-003: Client idle timeout
# Open connection, send nothing, verify nginx doesn't crash
###############################################################################

{
    my $t3 = Test::HTTPLite->new();
    $t3->write_config(9063, 10000, "127.0.0.1:${upstream_port}:5");
    $t3->run(9063);

    if (!$t3->waitforsocket('127.0.0.1:9063', 5)) {
        fail('KA-003: nginx failed to start');
    } else {
        # Open a connection and send nothing
        my $idle = IO::Socket::INET->new(
            PeerAddr => '127.0.0.1',
            PeerPort => 9063,
            Proto    => 'tcp',
            Timeout  => 1,
        );

        sleep 3;

        # Nginx should still be alive (60s timeout not reached)
        my $alive = kill(0, $t3->{pids}[0]);
        ok($alive, 'KA-003: idle connection - nginx stays alive during idle period');

        $idle->close if $idle;
    }
}
